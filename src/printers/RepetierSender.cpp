#include "printers/RepetierSender.h"
#include "printers/UploadProgress.h"

#include <QHttpMultiPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QUuid>

#include <stdexcept>
#include <utility>

std::unique_ptr<PrinterSendJob> RepetierSender::createJob(const QJsonObject &connection) const
{
    return std::make_unique<RepetierSendJob>(connection, autoLevelBedEnabled());
}

RepetierSendJob::RepetierSendJob(const QJsonObject &connection, bool autoLevelBed)
    : PrinterSendJob(autoLevelBed)
{
    origin = connectionUrl(connection, "http", 3344);
    apiKey = connection.value("api_key").toString().trimmed().toUtf8();
    slug = connection.value("printer_slug").toString().trimmed();
    if (apiKey.isEmpty() || slug.isEmpty())
        throw std::runtime_error("Repetier-Server requires api_key and printer_slug (the printer's slug in Repetier-Server).");
}

void RepetierSendJob::send(PrinterFile input)
{
    file = std::move(input);
    file.filename = file.filename.chopped(6).left(48) + "-" + QUuid::createUuid().toString(QUuid::Id128).left(6) + ".gcode";
    emit progress("Checking printer in Repetier-Server…");
    auto url = origin;
    url.setPath("/printer/list");
    QNetworkRequest request(url);
    request.setTransferTimeout(15000);
    request.setRawHeader("X-Api-Key", apiKey);
    auto *reply = network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError) {
            emit failed(QString("Repetier-Server request failed (HTTP %1): %2").arg(status).arg(reply->errorString()));
            return;
        }
        const auto body = QJsonDocument::fromJson(reply->readAll()).object();
        if (body.contains("error")) {
            emit failed("Repetier-Server: " + body.value("error").toString() + " Check the API key.");
            return;
        }
        for (const auto &entry : body.value("data").toArray()) {
            const auto printer = entry.toObject();
            if (printer.value("slug").toString() != slug) continue;
            // job is "none" when nothing is printing.
            if (printer.value("online").toInt() != 1 || printer.value("job").toString() != "none" || printer.value("paused").toBool()) {
                emit failed("The Repetier-Server printer is offline or busy. The current job was left running.");
                return;
            }
            upload();
            return;
        }
        emit failed("Repetier-Server has no printer with slug " + slug + ".");
    });
}

void RepetierSendJob::upload()
{
    auto *form = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    const auto field = [form](const char *name, const QByteArray &value) {
        QHttpPart part;
        part.setHeader(QNetworkRequest::ContentDispositionHeader, QString("form-data; name=\"%1\"").arg(name));
        part.setBody(value);
        form->append(part);
    };
    field("a", "upload");
    field("name", file.filename.toUtf8());
    field("autostart", "true");
    QHttpPart part;
    part.setHeader(QNetworkRequest::ContentDispositionHeader,
        QString("form-data; name=\"filename\"; filename=\"%1\"").arg(file.filename));
    part.setHeader(QNetworkRequest::ContentTypeHeader, "application/octet-stream");
    part.setBody(file.data);
    form->append(part);
    auto url = origin;
    url.setPath("/printer/job/" + slug);
    QNetworkRequest request(url);
    request.setRawHeader("X-Api-Key", apiKey);
    auto *reply = network.post(request, form);
    form->setParent(reply);
    reportUploadProgress(this, reply, "Repetier G-code", file.data.size());
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto body = QJsonDocument::fromJson(reply->readAll()).object();
        if (reply->error() != QNetworkReply::NoError || body.contains("error")) {
            emit failed(QString("Repetier-Server rejected the job (HTTP %1): %2").arg(status)
                .arg(body.contains("error") ? body.value("error").toString() : reply->errorString()));
            return;
        }
        emit completed();
    });
}
