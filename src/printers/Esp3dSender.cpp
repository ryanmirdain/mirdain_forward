#include "printers/Esp3dSender.h"
#include "printers/UploadProgress.h"

#include <QHttpMultiPart>
#include <QNetworkReply>
#include <QTimer>
#include <QUrlQuery>
#include <QUuid>

#include <utility>

std::unique_ptr<PrinterSendJob> Esp3dSender::createJob(const QJsonObject &connection) const
{
    return std::make_unique<Esp3dSendJob>(connection, autoLevelBedEnabled());
}

Esp3dSendJob::Esp3dSendJob(const QJsonObject &connection, bool autoLevelBed)
    : PrinterSendJob(autoLevelBed)
{
    origin = connectionUrl(connection, "http", 80);
}

void Esp3dSendJob::command(const QString &gcode, std::function<void(const QString &)> next)
{
    auto url = origin;
    url.setPath("/command");
    url.setQuery(QUrlQuery{{"plain", gcode}});
    QNetworkRequest request(url);
    request.setTransferTimeout(15000);
    auto *reply = network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, gcode, next = std::move(next)] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit failed(QString("ESP3D command %1 failed: %2").arg(gcode, reply->errorString()));
            return;
        }
        next(QString::fromUtf8(reply->readAll()));
    });
}

void Esp3dSendJob::send(PrinterFile input)
{
    file = std::move(input);
    // ESP3D and Marlin's SD support need 8.3 names; longer names can crash the firmware.
    file.filename = "MF" + QUuid::createUuid().toString(QUuid::Id128).left(6).toUpper() + ".GCO";
    emit progress("Checking printer through ESP3D…");
    // Marlin answers M27 with "SD printing byte …" or "Not SD printing"; ESP3D versions that do not
    // relay the reply return neither, and the check is then skipped.
    command("M27", [this](const QString &reply) {
        if (reply.contains("SD printing byte", Qt::CaseInsensitive)) {
            emit failed("The printer is printing from its SD card. The current job was left running.");
            return;
        }
        upload();
    });
}

void Esp3dSendJob::upload()
{
    auto *form = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    QHttpPart part;
    part.setHeader(QNetworkRequest::ContentDispositionHeader,
        QString("form-data; name=\"file\"; filename=\"%1\"").arg(file.filename));
    part.setHeader(QNetworkRequest::ContentTypeHeader, "application/octet-stream");
    part.setBody(file.data);
    form->append(part);
    auto url = origin;
    url.setPath("/upload_serial");
    QNetworkRequest request(url); // No transfer timeout: serial SD writes are slow.
    auto *reply = network.post(request, form);
    form->setParent(reply);
    reportUploadProgress(this, reply, "ESP3D G-code", file.data.size());
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError) {
            emit failed(QString("ESP3D upload failed (HTTP %1): %2").arg(status).arg(reply->errorString()));
            return;
        }
        emit progress("Starting print…");
        // The printer does not accept commands immediately after an SD transfer.
        QTimer::singleShot(1500, this, [this] { startPrint(); });
    });
}

void Esp3dSendJob::startPrint()
{
    command("M23 " + file.filename, [this](const QString &) {
        command("M24", [this](const QString &) { emit completed(); });
    });
}
