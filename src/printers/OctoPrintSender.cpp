#include "printers/OctoPrintSender.h"
#include "printers/UploadProgress.h"

#include <QHttpMultiPart>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QUuid>

#include <stdexcept>
#include <utility>

std::unique_ptr<PrinterSendJob> OctoPrintSender::createJob(const QJsonObject &connection) const
{
    return std::make_unique<OctoPrintSendJob>(connection, autoLevelBedEnabled());
}

OctoPrintSendJob::OctoPrintSendJob(const QJsonObject &connection, bool autoLevelBed)
    : PrinterSendJob(autoLevelBed)
{
    origin = connectionUrl(connection, "http", 80);
    apiKey = connection.value("api_key").toString().trimmed().toUtf8();
    if (apiKey.isEmpty())
        throw std::runtime_error("OctoPrint requires api_key (Settings > Application Keys or API).");
}

QNetworkRequest OctoPrintSendJob::request(const QString &path) const
{
    auto url = origin;
    url.setPath(path);
    QNetworkRequest request(url);
    request.setTransferTimeout(15000);
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("X-Api-Key", apiKey);
    return request;
}

// Passes HTTP 409 through: OctoPrint uses it for a printer that is not connected.
void OctoPrintSendJob::get(const QString &path, std::function<void(int, const QJsonObject &)> next)
{
    auto *reply = network.get(request(path));
    connect(reply, &QNetworkReply::finished, this, [this, reply, next = std::move(next)] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError && status != 409) {
            emit failed(QString("OctoPrint request failed (HTTP %1): %2").arg(status).arg(reply->errorString())
                + (status == 403 ? " Check the OctoPrint API key." : ""));
            return;
        }
        next(status, QJsonDocument::fromJson(reply->readAll()).object());
    });
}

void OctoPrintSendJob::send(PrinterFile input)
{
    file = std::move(input);
    file.filename = file.filename.chopped(6).left(48) + "-" + QUuid::createUuid().toString(QUuid::Id128).left(8) + ".gcode";
    emit progress("Connecting to OctoPrint…");
    get("/api/version", [this](int, const QJsonObject &) { checkPrinter(); });
}

void OctoPrintSendJob::checkPrinter()
{
    emit progress("Checking OctoPrint printer…");
    get("/api/printer", [this](int status, const QJsonObject &printer) {
        if (status == 409) {
            emit failed("OctoPrint is not connected to the printer. Connect it in OctoPrint before sending.");
            return;
        }
        const auto flags = printer.value("state").toObject().value("flags").toObject();
        const bool ready = flags.value("operational").toBool() && flags.value("ready").toBool(true)
            && !flags.value("printing").toBool() && !flags.value("paused").toBool()
            && !flags.value("pausing").toBool() && !flags.value("cancelling").toBool()
            && !flags.value("error").toBool();
        if (!ready) {
            emit failed("OctoPrint printer is busy or not ready to print. The current job was left running.");
            return;
        }
        upload();
    });
}

void OctoPrintSendJob::upload()
{
    auto *form = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    for (const auto *name : {"select", "print"}) {
        QHttpPart part;
        part.setHeader(QNetworkRequest::ContentDispositionHeader, QString("form-data; name=\"%1\"").arg(name));
        part.setBody("true");
        form->append(part);
    }
    QHttpPart part;
    part.setHeader(QNetworkRequest::ContentDispositionHeader,
        QString("form-data; name=\"file\"; filename=\"%1\"").arg(file.filename));
    part.setHeader(QNetworkRequest::ContentTypeHeader, "application/octet-stream");
    part.setBody(file.data);
    form->append(part);
    auto upload = request("/api/files/local");
    upload.setTransferTimeout(0); // Large uploads on a Raspberry Pi exceed the 15-second request limit.
    auto *reply = network.post(upload, form);
    form->setParent(reply);
    reportUploadProgress(this, reply, "OctoPrint G-code", file.data.size());
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError) {
            emit failed(QString("OctoPrint upload failed (HTTP %1): %2").arg(status).arg(reply->errorString()));
            return;
        }
        // effectivePrint is false when OctoPrint stored the file but could not start it.
        const auto result = QJsonDocument::fromJson(reply->readAll()).object();
        if (result.value("effectivePrint").toBool(result.value("done").toBool())) emit completed();
        else emit failed("Uploaded, but OctoPrint did not start the print. Check the printer before retrying.");
    });
}
