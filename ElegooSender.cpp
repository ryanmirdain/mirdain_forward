#include "ElegooSender.h"

#include <QDateTime>
#include <QHttpMultiPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QUuid>

#include <utility>

ElegooSender::ElegooSender(const QJsonObject &connection)
{
    uploadUrl = connectionUrl(connection, "http", 3030);
    uploadUrl.setPath("/uploadFile/upload");
    confirmationTimeout.setSingleShot(true);
    connect(&confirmationTimeout, &QTimer::timeout, this, [this] {
        finish("Uploaded, but print confirmation timed out. Check the printer before retrying.");
    });
}

void ElegooSender::send(PrinterFile input)
{
    file = std::move(input);
    md5 = file.gcodeMd5;
    uuid = QUuid::createUuid().toString(QUuid::Id128).toUtf8();
    upload();
}

void ElegooSender::finish(const QString &error)
{
    confirmationTimeout.stop();
    discovery.disconnect(this);
    discovery.close();
    socket.disconnect(this);
    socket.close();
    if (error.isEmpty()) emit completed();
    else emit failed(error);
}

void ElegooSender::upload()
{
    const auto chunk = file.data.mid(offset, 1024 * 1024);
    auto *form = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    const auto field = [form](const char *name, const QByteArray &value) {
        QHttpPart part;
        part.setHeader(QNetworkRequest::ContentDispositionHeader, QString("form-data; name=\"%1\"").arg(name));
        part.setBody(value);
        form->append(part);
    };
    field("Check", "1");
    field("S-File-MD5", md5);
    field("Offset", QByteArray::number(offset));
    field("Uuid", uuid);
    field("TotalSize", QByteArray::number(file.data.size()));
    QHttpPart filePart;
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
        QString("form-data; name=\"File\"; filename=\"%1\"").arg(file.filename));
    filePart.setHeader(QNetworkRequest::ContentTypeHeader, "application/octet-stream");
    filePart.setBody(chunk);
    form->append(filePart);
    QNetworkRequest request(uploadUrl);
    request.setTransferTimeout(180000);
    auto *reply = network.post(request, form);
    form->setParent(reply);
    emit progress(QString("Uploading %1%…").arg(offset * 100 / file.data.size()));
    connect(reply, &QNetworkReply::finished, this, [this, reply, size = chunk.size()] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            finish("Upload failed: " + reply->errorString());
            return;
        }
        const auto result = QJsonDocument::fromJson(reply->readAll()).object();
        if (result.value("code").toString() != "000000") {
            finish("Printer rejected upload: " + QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact)));
            return;
        }
        offset += size;
        if (offset < file.data.size()) upload();
        else {
            emit progress("Upload complete - Preparing to print…");
            // Give the printer time to index and parse the completed file.
            QTimer::singleShot(1500, this, [this] { startPrint(); });
        }
    });
}

void ElegooSender::sendCommand(int command, const QJsonObject &parameters)
{
    commandId = QUuid::createUuid().toString(QUuid::Id128).left(16);
    const QJsonObject data{{"Cmd", command}, {"Data", parameters}, {"RequestID", commandId},
        {"MainboardID", mainboardId}, {"TimeStamp", QDateTime::currentMSecsSinceEpoch()}, {"From", 1}};
    const QJsonObject message{{"Id", mainboardId}, {"Topic", "sdcp/request/" + mainboardId}, {"Data", data}};
    socket.sendTextMessage(QString::fromUtf8(QJsonDocument(message).toJson(QJsonDocument::Compact)));
}

void ElegooSender::connectPrinter()
{
    connect(&socket, &QWebSocket::connected, this, [this] { sendCommand(0); });
    connect(&socket, &QWebSocket::errorOccurred, this, [this] {
        finish("Uploaded, but the print connection failed: " + socket.errorString());
    });
    connect(&socket, &QWebSocket::disconnected, this, [this] {
        finish("Uploaded, but the connection closed before print confirmation. Check the printer before retrying.");
    });
    connect(&socket, &QWebSocket::textMessageReceived, this, [this](const QString &text) {
        const auto message = QJsonDocument::fromJson(text.toUtf8()).object();
        if (message.contains("Status") && !startSent) {
            const auto flags = message.value("Status").toObject().value("CurrentStatus").toArray();
            bool idle = !flags.isEmpty();
            for (const auto &flag : flags) idle = idle && flag.toInt() == 0;
            if (!idle) {
                finish("Uploaded, but the printer is busy. The current job was left running.");
                return;
            }
            startSent = true;
            emit progress("Starting print…");
            const int platformType = file.bedType.contains("Textured", Qt::CaseInsensitive) ? 0 : 1;
            sendCommand(128, {{"Filename", "/local/" + file.filename}, {"StartLayer", 0},
                {"Calibration_switch", 1}, {"PrintPlatformType", platformType},
                {"Tlp_Switch", 0}, {"slot_map", QJsonArray{}}});
        }
        const auto data = message.value("Data").toObject();
        if (startSent && data.value("Cmd").toInt() == 128 && data.value("RequestID").toString() == commandId) {
            const auto ack = data.value("Data").toObject().value("Ack").toInt(-1);
            if (ack == 0) finish();
            else finish(QString("Uploaded, but the printer rejected start (Ack %1).").arg(ack));
        }
    });
    QUrl url = uploadUrl;
    url.setScheme("ws");
    url.setPath("/websocket");
    socket.open(url);
}

void ElegooSender::startPrint()
{
    emit progress("Connecting to printer controls…");
    connect(&discovery, &QUdpSocket::connected, this, [this] { discovery.write("M99999"); });
    connect(&discovery, &QUdpSocket::errorOccurred, this, [this] {
        finish("Uploaded, but printer discovery failed: " + discovery.errorString());
    });
    connect(&discovery, &QUdpSocket::readyRead, this, [this] {
        QByteArray bytes(discovery.pendingDatagramSize(), Qt::Uninitialized);
        discovery.readDatagram(bytes.data(), bytes.size());
        mainboardId = QJsonDocument::fromJson(bytes).object().value("Data").toObject().value("MainboardID").toString();
        if (mainboardId.isEmpty()) {
            finish("Uploaded, but the printer did not return its MainboardID.");
            return;
        }
        discovery.disconnect(this);
        discovery.close();
        connectPrinter();
    });
    confirmationTimeout.start(15000);
    discovery.connectToHost(uploadUrl.host(), 3000);
}
