#include "printers/MoonrakerSender.h"
#include "printers/UploadProgress.h"

#include <QHttpMultiPart>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QUrlQuery>
#include <QUuid>

#include <utility>

std::unique_ptr<PrinterSendJob> MoonrakerSender::createJob(const QJsonObject &connection) const
{
    return std::make_unique<MoonrakerSendJob>(connection, autoLevelBedEnabled());
}

MoonrakerSendJob::MoonrakerSendJob(const QJsonObject &connection, bool autoLevelBed)
    : PrinterSendJob(autoLevelBed)
{
    origin = connectionUrl(connection, "http", 7125);
    apiKey = connection.value("api_key").toString().trimmed().toUtf8();
}

QNetworkRequest MoonrakerSendJob::request(const QString &path, const QString &query) const
{
    auto url = origin;
    url.setPath(path);
    if (!query.isEmpty()) url.setQuery(query);
    QNetworkRequest request(url);
    request.setTransferTimeout(15000);
    request.setRawHeader("Accept", "application/json");
    if (!apiKey.isEmpty()) request.setRawHeader("X-Api-Key", apiKey);
    return request;
}

// Moonraker wraps successful JSON-RPC style HTTP responses in {"result": …}.
void MoonrakerSendJob::get(const QString &path, const QString &query, std::function<void(const QJsonObject &)> next)
{
    auto *reply = network.get(request(path, query));
    connect(reply, &QNetworkReply::finished, this, [this, reply, next = std::move(next)] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError) {
            emit failed(QString("Moonraker request failed (HTTP %1): %2").arg(status).arg(reply->errorString())
                + (status == 401 ? " Add the Moonraker API key or trust this computer's address." : ""));
            return;
        }
        next(QJsonDocument::fromJson(reply->readAll()).object().value("result").toObject());
    });
}

void MoonrakerSendJob::send(PrinterFile input)
{
    file = std::move(input);
    // Moonraker overwrites same-named files; a unique name never replaces another queued or saved job.
    file.filename = file.filename.chopped(6).left(48) + "-" + QUuid::createUuid().toString(QUuid::Id128).left(8) + ".gcode";
    emit progress("Connecting to Moonraker…");
    get("/printer/info", {}, [this](const QJsonObject &info) {
        const auto state = info.value("state").toString();
        if (state != "ready") {
            emit failed("Klipper is not ready (" + (state.isEmpty() ? QString("unknown") : state)
                + "). Check the printer for errors before sending.");
            return;
        }
        checkPrinter();
    });
}

void MoonrakerSendJob::checkPrinter()
{
    emit progress("Checking Klipper printer…");
    get("/printer/objects/query", "print_stats=state", [this](const QJsonObject &result) {
        const auto state = result.value("status").toObject().value("print_stats").toObject().value("state").toString();
        if (state != "standby" && state != "complete" && state != "cancelled" && state != "error") {
            emit failed("Klipper printer is busy (" + state + "). The current job was left running.");
            return;
        }
        upload();
    });
}

void MoonrakerSendJob::upload()
{
    auto *form = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    for (const auto &[name, value] : {std::pair{"root", "gcodes"}, std::pair{"print", "true"}}) {
        QHttpPart part;
        part.setHeader(QNetworkRequest::ContentDispositionHeader, QString("form-data; name=\"%1\"").arg(name));
        part.setBody(value);
        form->append(part);
    }
    QHttpPart part;
    part.setHeader(QNetworkRequest::ContentDispositionHeader,
        QString("form-data; name=\"file\"; filename=\"%1\"").arg(file.filename));
    part.setHeader(QNetworkRequest::ContentTypeHeader, "application/octet-stream");
    part.setBody(file.data);
    form->append(part);
    auto upload = request("/server/files/upload");
    upload.setTransferTimeout(0); // Large uploads on slow Wi-Fi exceed the 15-second request limit.
    auto *reply = network.post(upload, form);
    form->setParent(reply);
    reportUploadProgress(this, reply, "Klipper G-code", file.data.size());
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError) {
            emit failed(QString("Moonraker upload failed (HTTP %1): %2").arg(status).arg(reply->errorString()));
            return;
        }
        // Moonraker versions differ on whether the upload result has a "result" wrapper.
        auto result = QJsonDocument::fromJson(reply->readAll()).object();
        if (result.value("result").isObject()) result = result.value("result").toObject();
        if (result.value("print_started").toBool()) emit completed();
        else if (result.value("print_queued").toBool())
            emit failed("Uploaded, but Moonraker queued the print instead of starting it. Check the job queue.");
        else emit failed("Uploaded, but Moonraker did not start the print. Check the printer before retrying.");
    });
}
