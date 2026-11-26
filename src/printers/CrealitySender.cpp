#include "printers/CrealitySender.h"
#include "printers/UploadProgress.h"

#include <QHttpMultiPart>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QUuid>

#include <utility>

namespace {
// K1-family firmware stores G-code under /usr/data; K2-family and newer under /mnt/UDISK.
bool isK1Family(const QString &model)
{
    static const QStringList models{"K1", "K1 Max", "K1 Max_CFS-C", "K1 SE", "K1 SE_CFS-C", "K1C", "K1C_CFS-C", "K1_CFS-C"};
    return models.contains(model);
}
}

std::unique_ptr<PrinterSendJob> CrealitySender::createJob(const QJsonObject &connection) const
{
    return std::make_unique<CrealitySendJob>(connection, autoLevelBedEnabled());
}

CrealitySendJob::CrealitySendJob(const QJsonObject &connection, bool autoLevelBed)
    : PrinterSendJob(autoLevelBed)
{
    origin = connectionUrl(connection, "http", 80);
    control = connectionUrl(connection, "ws", 9999, "ws_port");
    control.setPath("/");
    timeout.setSingleShot(true);
    connect(&timeout, &QTimer::timeout, this, [this] {
        finish(phase == Phase::CheckIdle
            ? QString("The Creality printer did not report its status. Check that it is on and reachable.")
            : QString("Uploaded, but print confirmation timed out. Check the printer before retrying."));
    });
    connect(&socket, &QWebSocket::textMessageReceived, this, &CrealitySendJob::receive);
    connect(&socket, &QWebSocket::connected, this, [this] {
        if (phase == Phase::CheckIdle) {
            socket.sendTextMessage(R"({"method":"get","params":{"reqPrinterPara":1}})");
            return;
        }
        emit progress("Starting print…");
        startSent = true;
        const QJsonObject start{{"method", "set"}, {"params", QJsonObject{{"opGcodeFile", "printprt:" + gcodeRoot + file.filename}}}};
        socket.sendTextMessage(QString::fromUtf8(QJsonDocument(start).toJson(QJsonDocument::Compact)));
    });
    connect(&socket, &QWebSocket::errorOccurred, this, [this] {
        // The status connection is closed for the upload, and K1 firmware drops the connection after a start.
        if (phase == Phase::Uploading || (phase == Phase::Starting && startSent)) return;
        finish((phase == Phase::Starting ? "Uploaded, but the print connection failed: " : "Creality connection failed: ")
            + socket.errorString());
    });
    connect(&socket, &QWebSocket::disconnected, this, [this] {
        // K1-family firmware closes the WebSocket right after accepting a start command.
        if (phase == Phase::Starting && startSent) finish();
    });
}

void CrealitySendJob::finish(const QString &error)
{
    if (done) return;
    done = true;
    timeout.stop();
    socket.disconnect(this);
    socket.close();
    if (error.isEmpty()) emit completed();
    else emit failed(error);
}

void CrealitySendJob::send(PrinterFile input)
{
    file = std::move(input);
    file.filename = file.filename.chopped(6).left(48) + "-" + QUuid::createUuid().toString(QUuid::Id128).left(8) + ".gcode";
    readModel();
}

void CrealitySendJob::readModel()
{
    emit progress("Connecting to Creality printer…");
    auto url = origin;
    url.setPath("/info");
    QNetworkRequest request(url);
    request.setTransferTimeout(15000);
    auto *reply = network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            finish("Creality printer info request failed: " + reply->errorString());
            return;
        }
        const auto model = QJsonDocument::fromJson(reply->readAll()).object().value("model").toString();
        gcodeRoot = (isK1Family(model) ? "/usr/data" : "/mnt/UDISK") + QString("/printer_data/gcodes/");
        openControl(Phase::CheckIdle);
    });
}

void CrealitySendJob::openControl(Phase next)
{
    phase = next;
    timeout.start(next == Phase::CheckIdle ? 15000 : 30000);
    socket.open(control);
}

// Status messages carry "state": 1 printing and 5 paused; 0 idle, 2 finished, 3 failed, and 4 stopped are idle.
void CrealitySendJob::receive(const QString &text)
{
    if (text.contains("heart_beat")) {
        socket.sendTextMessage("ok"); // K1-family firmware disconnects clients that do not acknowledge heartbeats.
        return;
    }
    const auto status = QJsonDocument::fromJson(text.toUtf8()).object();
    if (!status.contains("state")) return;
    const int state = status.value("state").toVariant().toInt();
    if (phase == Phase::CheckIdle) {
        timeout.stop();
        if (state == 1 || state == 5) {
            finish("Creality printer is busy. The current job was left running.");
            return;
        }
        phase = Phase::Uploading;
        socket.close();
        upload();
        return;
    }
    if (startSent && state == 1) finish();
}

void CrealitySendJob::upload()
{
    auto *form = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    QHttpPart part;
    part.setHeader(QNetworkRequest::ContentDispositionHeader,
        QString("form-data; name=\"file\"; filename=\"%1\"").arg(file.filename));
    part.setHeader(QNetworkRequest::ContentTypeHeader, "application/octet-stream");
    part.setBody(file.data);
    form->append(part);
    auto url = origin;
    url.setPath("/upload/" + file.filename);
    QNetworkRequest request(url);
    auto *reply = network.post(request, form);
    form->setParent(reply);
    reportUploadProgress(this, reply, "Creality G-code", file.data.size());
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError) {
            finish(QString("Creality upload failed (HTTP %1): %2").arg(status).arg(reply->errorString()));
            return;
        }
        openControl(Phase::Starting);
    });
}
