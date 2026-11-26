#include "printers/MksSender.h"
#include "printers/UploadProgress.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QUrlQuery>
#include <QUuid>

#include <utility>

std::unique_ptr<PrinterSendJob> MksSender::createJob(const QJsonObject &connection) const
{
    return std::make_unique<MksSendJob>(connection, autoLevelBedEnabled());
}

MksSendJob::MksSendJob(const QJsonObject &connection, bool autoLevelBed)
    : PrinterSendJob(autoLevelBed)
{
    origin = connectionUrl(connection, "http", 80);
    consoleUrl = connectionUrl(connection, "tcp", 8080, "console_port");
    consoleTimeout.setSingleShot(true);
    consoleTimeout.setInterval(10000);
    connect(&consoleTimeout, &QTimer::timeout, this, [this] {
        console.abort();
        emit failed("The MKS Wi-Fi console did not answer within 10 seconds.");
    });
    connect(&console, &QTcpSocket::connected, this, &MksSendJob::sendNextCommand);
    connect(&console, &QTcpSocket::errorOccurred, this, [this] {
        consoleTimeout.stop();
        emit failed("MKS Wi-Fi console connection failed: " + console.errorString());
    });
    connect(&console, &QTcpSocket::readyRead, this, [this] {
        received += console.readAll();
        qsizetype end;
        while ((end = received.indexOf('\n')) >= 0) {
            const auto line = QString::fromUtf8(received.left(end)).trimmed();
            received.remove(0, end + 1);
            replyLines << line;
            if (line.startsWith("ok")) sendNextCommand();
        }
    });
}

void MksSendJob::runCommands(const QStringList &commands, std::function<void(const QStringList &)> next)
{
    pending = commands;
    replyLines.clear();
    afterCommands = std::move(next);
    if (console.state() == QAbstractSocket::ConnectedState) sendNextCommand();
    else console.connectToHost(consoleUrl.host(), consoleUrl.port());
}

void MksSendJob::sendNextCommand()
{
    if (pending.isEmpty()) {
        consoleTimeout.stop();
        std::exchange(afterCommands, nullptr)(replyLines);
        return;
    }
    consoleTimeout.start();
    console.write((pending.takeFirst() + "\n").toUtf8());
}

void MksSendJob::send(PrinterFile input)
{
    file = std::move(input);
    file.filename = file.filename.chopped(6).left(24) + "-" + QUuid::createUuid().toString(QUuid::Id128).left(6) + ".gcode";
    emit progress("Checking printer through MKS Wi-Fi…");
    runCommands({"M27"}, [this](const QStringList &reply) {
        // Marlin-style firmware answers "SD printing byte N/M" while printing and "Not SD printing" otherwise.
        for (const auto &line : reply) {
            if (line.startsWith("SD printing byte", Qt::CaseInsensitive) && !line.contains(" 0/")) {
                emit failed("The printer is printing from its SD card. The current job was left running.");
                return;
            }
        }
        console.disconnectFromHost(); // Reconnected for the start; an idle console may be dropped during the upload.
        upload();
    });
}

void MksSendJob::upload()
{
    auto url = origin;
    url.setPath("/upload");
    url.setQuery(QUrlQuery{{"X-Filename", file.filename}});
    QNetworkRequest request(url); // No transfer timeout: SD writes over the module are slow.
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/octet-stream");
    auto *reply = network.post(request, file.data);
    reportUploadProgress(this, reply, "MKS G-code", file.data.size());
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError) {
            emit failed(QString("MKS upload failed (HTTP %1): %2").arg(status).arg(reply->errorString()));
            return;
        }
        const auto result = QJsonDocument::fromJson(reply->readAll()).object();
        if (result.contains("err") && result.value("err").toInt() != 0) {
            emit failed(QString("MKS rejected the upload (err %1).").arg(result.value("err").toInt()));
            return;
        }
        emit progress("Starting print…");
        // The firmware does not answer commands immediately after an upload.
        QTimer::singleShot(1500, this, [this] {
            runCommands({"M23 " + file.filename, "M24"}, [this](const QStringList &) {
                console.disconnectFromHost();
                emit completed();
            });
        });
    });
}
