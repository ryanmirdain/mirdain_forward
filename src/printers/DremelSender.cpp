#include "printers/DremelSender.h"
#include "printers/UploadProgress.h"

#include <QHttpMultiPart>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QUuid>

#include <utility>

std::unique_ptr<PrinterSendJob> DremelSender::createJob(const QJsonObject &connection) const
{
    return std::make_unique<DremelSendJob>(connection, autoLevelBedEnabled());
}

DremelSendJob::DremelSendJob(const QJsonObject &connection, bool autoLevelBed)
    : PrinterSendJob(autoLevelBed)
{
    origin = connectionUrl(connection, "http", 80);
    confirmation.setInterval(1000);
    confirmation.setSingleShot(true);
    connect(&confirmation, &QTimer::timeout, this, &DremelSendJob::confirmStart);
}

QNetworkRequest DremelSendJob::request(const QString &path) const
{
    auto url = origin;
    url.setPath(path);
    QNetworkRequest request(url);
    request.setTransferTimeout(30000);
    return request;
}

void DremelSendJob::command(const QByteArray &body, const QString &action, std::function<void(const QJsonObject &)> next)
{
    auto commandRequest = request("/command");
    commandRequest.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    auto *reply = network.post(commandRequest, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply, action, next = std::move(next)] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError) {
            emit failed(QString("Dremel %1 failed (HTTP %2): %3").arg(action).arg(status).arg(reply->errorString()));
            return;
        }
        next(QJsonDocument::fromJson(reply->readAll()).object());
    });
}

void DremelSendJob::send(PrinterFile input)
{
    file = std::move(input);
    // The printer's file list shows short names; keep a readable stem with a unique suffix.
    file.filename = file.filename.chopped(6).left(24) + "-" + QUuid::createUuid().toString(QUuid::Id128).left(6) + ".gcode";
    emit progress("Checking Dremel printer…");
    command("GETPRINTERSTATUS", "status request", [this](const QJsonObject &status) {
        const auto state = status.value("status").toString();
        if (state != "ready") {
            emit failed("Dremel printer is busy or not ready (" + (state.isEmpty() ? QString("unknown") : state)
                + "). The current job was left running.");
            return;
        }
        if (status.value("door_open").toInt() == 1) {
            emit failed("The Dremel printer's door is open. Close it before sending a print.");
            return;
        }
        upload();
    });
}

void DremelSendJob::upload()
{
    auto *form = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    QHttpPart part;
    part.setHeader(QNetworkRequest::ContentDispositionHeader,
        QString("form-data; name=\"print_file\"; filename=\"%1\"").arg(file.filename));
    part.setHeader(QNetworkRequest::ContentTypeHeader, "application/octet-stream");
    part.setBody(file.data);
    form->append(part);
    auto upload = request("/print_file_uploads");
    upload.setTransferTimeout(0); // Large uploads over the printer's Wi-Fi exceed the request limit.
    auto *reply = network.post(upload, form);
    form->setParent(reply);
    reportUploadProgress(this, reply, "Dremel G-code", file.data.size());
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError) {
            emit failed(QString("Dremel upload failed (HTTP %1): %2").arg(status).arg(reply->errorString()));
            return;
        }
        startPrint();
    });
}

void DremelSendJob::startPrint()
{
    emit progress("Starting print…");
    command("PRINT=" + file.filename.toUtf8(), "start command", [this](const QJsonObject &result) {
        if (result.contains("error_code") && result.value("error_code").toInt() != 200) {
            emit failed("Uploaded, but the Dremel rejected the start: " + result.value("message").toString());
            return;
        }
        confirmStart();
    });
}

// PRINT returns before the job begins, so confirm the printer reports the job building or preparing.
void DremelSendJob::confirmStart()
{
    command("GETPRINTERSTATUS", "status request", [this](const QJsonObject &status) {
        const auto job = status.value("jobstatus").toString();
        if (status.value("status").toString() == "busy" || job == "building" || job == "preparing") {
            emit completed();
            return;
        }
        if (++confirmationPolls >= 20) {
            emit failed("Uploaded, but the Dremel did not start within 20 seconds. Check the printer before retrying.");
            return;
        }
        confirmation.start();
    });
}
