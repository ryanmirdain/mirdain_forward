#include "PrusaSender.h"

#include <QAuthenticator>
#include <QHttpMultiPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

#include <stdexcept>
#include <utility>

std::unique_ptr<PrinterSendJob> PrusaSender::createJob(const QJsonObject &connection) const
{
    return std::make_unique<PrusaSendJob>(connection, autoLevelBedEnabled());
}

PrusaSendJob::PrusaSendJob(const QJsonObject &connection, bool autoLevelBed)
    : PrinterSendJob(autoLevelBed)
{
    origin = connectionUrl(connection, "http", 80);
    apiKey = connection.value("api_key").toString().trimmed().toUtf8();
    const auto password = connection.value("password").toString();
    if (apiKey.isEmpty() && password.isEmpty())
        throw std::runtime_error("PrusaLink requires api_key or a Digest password (username defaults to maker).");
    if (apiKey.isEmpty()) {
        connect(&network, &QNetworkAccessManager::authenticationRequired, this,
            [username = connection.value("username").toString("maker"), password](QNetworkReply *reply, QAuthenticator *auth) {
                if (reply->property("digestAttempted").toBool()) return;
                reply->setProperty("digestAttempted", true);
                auth->setUser(username);
                auth->setPassword(password);
            });
    }
    storage = connection.value("storage").toString();
    if (connection.contains("storage") && !QRegularExpression("^/?[^/]+/?$").match(storage).hasMatch())
        throw std::runtime_error("PrusaLink storage must name a storage such as usb, local, or sdcard.");
    storage.remove(QRegularExpression("^/|/$"));
}

QNetworkRequest PrusaSendJob::request(const QString &path) const
{
    auto url = origin;
    url.setPath(path);
    QNetworkRequest request(url);
    request.setTransferTimeout(180000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setRawHeader("Accept", "application/json");
    if (!apiKey.isEmpty()) request.setRawHeader("X-Api-Key", apiKey);
    return request;
}

bool PrusaSendJob::accepted(QNetworkReply *reply)
{
    reply->deleteLater();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() == QNetworkReply::NoError && status >= 200 && status < 300) return true;
    emit failed(QString("PrusaLink request failed (HTTP %1): %2").arg(status).arg(reply->errorString()));
    return false;
}

void PrusaSendJob::get(const QString &path, std::function<void(const QJsonObject &)> next)
{
    auto *reply = network.get(request(path));
    connect(reply, &QNetworkReply::finished, this, [this, reply, next = std::move(next)] {
        if (!accepted(reply)) return;
        const auto document = QJsonDocument::fromJson(reply->readAll());
        if (!document.isObject()) {
            emit failed("PrusaLink returned an invalid JSON response.");
            return;
        }
        next(document.object());
    });
}

void PrusaSendJob::send(PrinterFile input)
{
    file = std::move(input);
    emit progress("Connecting to PrusaLink…");
    get("/api/version", [this](const QJsonObject &version) {
        modern = version.value("capabilities").toObject().value("upload-by-put").toBool();
        checkPrinter();
    });
}

void PrusaSendJob::checkPrinter()
{
    get(modern ? "/api/v1/status" : "/api/printer", [this](const QJsonObject &status) {
        bool ready;
        if (modern) {
            const auto state = status.value("printer").toObject().value("state").toString();
            ready = state == "IDLE" || state == "READY" || state == "FINISHED" || state == "STOPPED";
        } else {
            const auto flags = status.value("state").toObject().value("flags").toObject();
            ready = flags.value("operational").toBool() && !flags.value("printing").toBool()
                && !flags.value("paused").toBool() && !flags.value("cancelling").toBool()
                && !flags.value("pausing").toBool() && !flags.value("error").toBool();
        }
        if (!ready) {
            emit failed("PrusaLink printer is busy or not ready to print.");
            return;
        }
        chooseStorage();
    });
}

void PrusaSendJob::chooseStorage()
{
    if (!modern) {
        if (storage.isEmpty()) storage = "local";
        if (storage != "local" && storage != "sdcard") {
            emit failed("This PrusaLink version requires local or sdcard storage.");
            return;
        }
        upload();
        return;
    }
    get("/api/v1/storage", [this](const QJsonObject &result) {
        for (const auto &entry : result.value("storage_list").toArray()) {
            const auto item = entry.toObject();
            auto name = item.value("path").toString();
            name.remove(QRegularExpression("^/|/$"));
            if (item.value("available").toBool() && !item.value("read_only").toBool()
                && !name.isEmpty() && (storage.isEmpty() || storage == name)) {
                storage = name;
                upload();
                return;
            }
        }
        emit failed("PrusaLink has no available writable storage. Check the USB drive or connection.storage.");
    });
}

void PrusaSendJob::upload()
{
    emit progress("Uploading to PrusaLink and requesting print…");
    QNetworkReply *reply;
    if (modern) {
        auto upload = request("/api/v1/files/" + storage + "/" + file.filename);
        upload.setHeader(QNetworkRequest::ContentTypeHeader, "application/octet-stream");
        upload.setRawHeader("Print-After-Upload", "?1");
        upload.setRawHeader("Overwrite", "?0");
        reply = network.put(upload, file.data);
    } else {
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
        reply = network.post(request("/api/files/" + storage), form);
        form->setParent(reply);
    }
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (accepted(reply)) emit completed();
    });
}
