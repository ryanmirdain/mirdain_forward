#include "printers/PrusaSender.h"
#include "printers/PrusaBgcode.h"

#include <QAuthenticator>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QHttpMultiPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUuid>
#include <QVersionNumber>
#include <QtConcurrentRun>

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
    request.setTransferTimeout(15000);
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
    // Buddy firmware has a 168-byte URL buffer; archive member names can exceed it.
    file.filename = file.filename.chopped(6).left(32) + "-" + QUuid::createUuid().toString(QUuid::Id128) + ".gcode";
    emit progress("Connecting to PrusaLink…");
    get("/api/version", [this](const QJsonObject &version) {
        modern = version.value("capabilities").toObject().value("upload-by-put").toBool();
        binaryGcode = modern && QVersionNumber::fromString(version.value("firmware").toString()) >= QVersionNumber(5, 1, 0);
        checkPrinter();
    });
}

void PrusaSendJob::checkPrinter()
{
    emit progress("Checking Prusa printer…");
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
    emit progress("Selecting Prusa storage…");
    if (!modern) {
        if (storage.isEmpty()) storage = "local";
        if (storage != "local" && storage != "sdcard") {
            emit failed("This PrusaLink version requires local or sdcard storage.");
            return;
        }
        prepareUpload();
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
                prepareUpload();
                return;
            }
        }
        emit failed("PrusaLink has no available writable storage. Check the USB drive or connection.storage.");
    });
}

void PrusaSendJob::prepareUpload()
{
    if (!binaryGcode) {
        upload();
        return;
    }
    emit progress("Compressing Prusa G-code to BGCODE…");
    using CompressionResult = std::pair<QByteArray, QString>;
    auto *watcher = new QFutureWatcher<CompressionResult>(this);
    connect(watcher, &QFutureWatcher<CompressionResult>::finished, this, [this, watcher] {
        watcher->deleteLater();
        auto result = watcher->result();
        if (!result.second.isEmpty()) {
            emit failed("Prusa BGCODE compression failed: " + result.second);
            return;
        }
        file.data = std::move(result.first);
        file.filename.chop(6); // .gcode -> .bgcode
        file.filename += ".bgcode";
        upload();
    });
    watcher->setFuture(QtConcurrent::run([gcode = file.data]() -> CompressionResult {
        try {
            return {prusaBgcode(gcode), {}};
        } catch (const std::exception &error) {
            return {{}, QString::fromUtf8(error.what())};
        }
    }));
}

void PrusaSendJob::upload()
{
    const auto uploadName = binaryGcode ? QString("Prusa BGCODE") : QString("Prusa");
    emit progress(QString("Uploading %1… 0% · 0.0/%2 MiB · 0 KiB/s")
        .arg(uploadName).arg(file.data.size() / 1048576.0, 0, 'f', 1));
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
    QElapsedTimer progressClock;
    progressClock.start();
    connect(reply, &QNetworkReply::uploadProgress, this,
        [this, uploadName, progressClock, lastProgressBytes = qint64(0)](qint64 sent, qint64 total) mutable {
            if (total <= 0) return;
            if (sent == total) {
                emit progress("Prusa upload complete - Waiting for print response…");
                return;
            }
            const auto elapsed = progressClock.elapsed();
            if (elapsed < 1000) return;
            const double kibPerSecond = qMax<qint64>(0, sent - lastProgressBytes) * 1000.0 / elapsed / 1024;
            lastProgressBytes = sent;
            progressClock.restart();
            emit progress(QString("Uploading %1… %2% · %3/%4 MiB · %5 KiB/s")
                .arg(uploadName).arg(int(sent * 100 / total)).arg(sent / 1048576.0, 0, 'f', 1)
                .arg(total / 1048576.0, 0, 'f', 1).arg(kibPerSecond, 0, 'f', 0));
        });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (accepted(reply)) emit completed();
    });
}
