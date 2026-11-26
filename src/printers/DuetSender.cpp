#include "printers/DuetSender.h"
#include "printers/UploadProgress.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QUuid>

#include <utility>

std::unique_ptr<PrinterSendJob> DuetSender::createJob(const QJsonObject &connection) const
{
    return std::make_unique<DuetSendJob>(connection, autoLevelBedEnabled());
}

DuetSendJob::DuetSendJob(const QJsonObject &connection, bool autoLevelBed)
    : PrinterSendJob(autoLevelBed)
{
    origin = connectionUrl(connection, "http", 80);
    // RepRapFirmware accepts any password when none is set; "reprap" is its documented default.
    password = connection.value("password").toString("reprap");
    confirmation.setInterval(1000);
    connect(&confirmation, &QTimer::timeout, this, &DuetSendJob::confirmStart);
}

QNetworkRequest DuetSendJob::request(const QString &path, const QUrlQuery &query) const
{
    auto url = origin;
    url.setPath(path);
    if (!query.isEmpty()) url.setQuery(query);
    QNetworkRequest request(url);
    request.setTransferTimeout(15000);
    if (!sessionKey.isEmpty()) request.setRawHeader("X-Session-Key", sessionKey);
    return request;
}

void DuetSendJob::call(QNetworkReply *reply, const QString &action, std::function<void(const QByteArray &)> next)
{
    connect(reply, &QNetworkReply::finished, this, [this, reply, action, next = std::move(next)] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError) {
            confirmation.stop();
            emit failed(QString("Duet %1 failed (HTTP %2): %3").arg(action).arg(status).arg(reply->errorString()));
            return;
        }
        next(reply->readAll());
    });
}

void DuetSendJob::send(PrinterFile input)
{
    file = std::move(input);
    file.filename = file.filename.chopped(6).left(48) + "-" + QUuid::createUuid().toString(QUuid::Id128).left(8) + ".gcode";
    emit progress("Connecting to Duet…");
    connectBoard();
}

// A standalone board answers rr_connect; a board with an SBC answers DSF's /machine/connect instead.
void DuetSendJob::connectBoard()
{
    const QUrlQuery query{{"password", password},
        {"time", QDateTime::currentDateTime().toString("yyyy-MM-ddTHH:mm:ss")}};
    auto *reply = network.get(request("/rr_connect", query));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            const auto result = QJsonDocument::fromJson(reply->readAll()).object();
            const int err = result.value("err").toInt(-1);
            if (err != 0) {
                emit failed(err == 1 ? QString("Duet rejected the password.")
                    : err == 2 ? QString("Duet has no free HTTP sessions. Close a browser tab running Duet Web Control and retry.")
                    : QString("Duet returned an unexpected connect response."));
                return;
            }
            if (result.contains("sessionKey"))
                sessionKey = QByteArray::number(result.value("sessionKey").toVariant().toLongLong());
            checkIdle();
            return;
        }
        sbc = true;
        call(network.get(request("/machine/connect", QUrlQuery{{"password", password}})), "SBC connection",
            [this](const QByteArray &body) {
                sessionKey = QJsonDocument::fromJson(body).object().value("sessionKey").toVariant().toByteArray();
                checkIdle();
            });
    });
}

void DuetSendJob::readStatus(std::function<void(const QString &)> next)
{
    auto *reply = sbc ? network.get(request("/machine/status"))
                      : network.get(request("/rr_model", QUrlQuery{{"key", "state"}, {"flags", "d99f"}}));
    call(reply, "status request", [this, next = std::move(next)](const QByteArray &body) {
        const auto json = QJsonDocument::fromJson(body).object();
        // rr_model wraps the requested key in "result"; DSF returns the whole object model.
        const auto state = sbc ? json.value("state").toObject() : json.value("result").toObject();
        const auto status = state.value("status").toString();
        if (status.isEmpty()) {
            confirmation.stop();
            emit failed("Duet did not report its status. RepRapFirmware 3 or newer is required.");
            return;
        }
        next(status);
    });
}

void DuetSendJob::checkIdle()
{
    emit progress("Checking Duet printer…");
    readStatus([this](const QString &status) {
        if (status != "idle") {
            emit failed("Duet printer is busy or not ready (" + status + "). The current job was left running.");
            return;
        }
        upload();
    });
}

void DuetSendJob::upload()
{
    QNetworkReply *reply;
    if (sbc) {
        auto upload = request("/machine/file/0:/gcodes/" + file.filename);
        upload.setTransferTimeout(0);
        upload.setHeader(QNetworkRequest::ContentTypeHeader, "application/octet-stream");
        reply = network.put(upload, file.data);
    } else {
        auto upload = request("/rr_upload", QUrlQuery{{"name", "0:/gcodes/" + file.filename},
            {"time", QDateTime::currentDateTime().toString("yyyy-MM-ddTHH:mm:ss")}});
        upload.setTransferTimeout(0); // Large uploads over the board's Wi-Fi exceed the 15-second request limit.
        upload.setHeader(QNetworkRequest::ContentTypeHeader, "application/octet-stream");
        reply = network.post(upload, file.data);
    }
    reportUploadProgress(this, reply, "Duet G-code", file.data.size());
    call(reply, "upload", [this](const QByteArray &body) {
        if (!sbc && QJsonDocument::fromJson(body).object().value("err").toInt(-1) != 0) {
            emit failed("Duet rejected the upload. Check the SD card.");
            return;
        }
        startPrint();
    });
}

void DuetSendJob::startPrint()
{
    emit progress("Starting print…");
    const auto command = QString("M32 \"0:/gcodes/%1\"").arg(file.filename);
    QNetworkReply *reply;
    if (sbc) {
        auto start = request("/machine/code");
        start.setHeader(QNetworkRequest::ContentTypeHeader, "text/plain");
        reply = network.post(start, command.toUtf8());
    } else {
        reply = network.get(request("/rr_gcode", QUrlQuery{{"gcode", command}}));
    }
    call(reply, "start command", [this](const QByteArray &body) {
        // DSF replies with the command's output, which is empty unless M32 reported an error.
        if (sbc && body.trimmed().startsWith("Error")) {
            emit failed("Uploaded, but Duet rejected the start: " + QString::fromUtf8(body.trimmed()));
            return;
        }
        // rr_gcode only queues the command, so confirm the printer started from its status.
        confirmation.start();
    });
}

void DuetSendJob::confirmStart()
{
    confirmation.stop();
    readStatus([this](const QString &status) {
        if (status == "processing") {
            emit completed();
            return;
        }
        if (++confirmationPolls >= 20) {
            emit failed("Uploaded, but the print did not start within 20 seconds. Check the printer before retrying.");
            return;
        }
        confirmation.start();
    });
}
