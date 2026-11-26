#include "printers/FlashforgeSender.h"
#include "printers/UploadProgress.h"

#include <QHttpMultiPart>
#include <QJsonDocument>
#include <QNetworkReply>

#include <stdexcept>
#include <utility>

std::unique_ptr<PrinterSendJob> FlashforgeSender::createJob(const QJsonObject &connection) const
{
    return std::make_unique<FlashforgeSendJob>(connection, autoLevelBedEnabled());
}

FlashforgeSendJob::FlashforgeSendJob(const QJsonObject &connection, bool autoLevelBed)
    : PrinterSendJob(autoLevelBed)
{
    origin = connectionUrl(connection, "http", 8898);
    serial = connection.value("serial_number").toString().trimmed().toUtf8();
    checkCode = connection.value("check_code").toString().trimmed().toUtf8();
    if (serial.isEmpty() || checkCode.isEmpty())
        throw std::runtime_error("Flashforge LAN mode requires serial_number and check_code from the printer's network settings.");
}

QNetworkRequest FlashforgeSendJob::request(const QString &path) const
{
    auto url = origin;
    url.setPath(path);
    QNetworkRequest request(url);
    request.setTransferTimeout(15000);
    return request;
}

// The printer answers {"code": 0, "message": "Success", …}; any other code is a rejection.
QJsonObject FlashforgeSendJob::result(QNetworkReply *reply, const QString &action)
{
    reply->deleteLater();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError) {
        emit failed(QString("Flashforge %1 failed (HTTP %2): %3").arg(action).arg(status).arg(reply->errorString()));
        return {};
    }
    const auto body = QJsonDocument::fromJson(reply->readAll()).object();
    if (body.isEmpty()) {
        emit failed("Flashforge returned an invalid response to the " + action + ".");
        return {};
    }
    if (const auto code = body.value("code").toVariant().toInt(); code != 0) {
        emit failed(QString("Flashforge rejected the %1 (code %2): %3 Check the serial number and check code.")
            .arg(action).arg(code).arg(body.value("message").toString()));
        return {};
    }
    return body;
}

void FlashforgeSendJob::send(PrinterFile input)
{
    file = std::move(input);
    emit progress("Checking Flashforge printer…");
    auto detail = request("/detail");
    detail.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    auto *reply = network.post(detail, QJsonDocument(QJsonObject{
        {"serialNumber", QString::fromUtf8(serial)}, {"checkCode", QString::fromUtf8(checkCode)}}).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const auto body = result(reply, "status request");
        if (body.isEmpty()) return;
        const auto status = body.value("detail").toObject().value("status").toString();
        if (status != "ready" && status != "completed" && status != "cancel") {
            emit failed("Flashforge printer is busy or not ready (" + (status.isEmpty() ? QString("unknown") : status)
                + "). The current job was left running.");
            return;
        }
        upload();
    });
}

void FlashforgeSendJob::upload()
{
    auto *form = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    QHttpPart part;
    part.setHeader(QNetworkRequest::ContentDispositionHeader,
        QString("form-data; name=\"gcodeFile\"; filename=\"%1\"").arg(file.filename));
    part.setHeader(QNetworkRequest::ContentTypeHeader, "application/octet-stream");
    part.setBody(file.data);
    form->append(part);
    auto upload = request("/uploadGcode");
    upload.setTransferTimeout(0); // Large uploads over the printer's Wi-Fi exceed the 15-second request limit.
    upload.setRawHeader("serialNumber", serial);
    upload.setRawHeader("checkCode", checkCode);
    upload.setRawHeader("fileSize", QByteArray::number(file.data.size()));
    upload.setRawHeader("printNow", "true");
    upload.setRawHeader("levelingBeforePrint", autoLevelBed ? "true" : "false");
    upload.setRawHeader("flowCalibration", "false");
    upload.setRawHeader("firstLayerInspection", "false");
    upload.setRawHeader("timeLapseVideo", "false");
    // Single-filament printing; AD5X material station mappings are not derived from the slice.
    upload.setRawHeader("useMatlStation", "false");
    upload.setRawHeader("gcodeToolCnt", "0");
    upload.setRawHeader("materialMappings", QByteArray("[]").toBase64());
    auto *reply = network.post(upload, form);
    form->setParent(reply);
    reportUploadProgress(this, reply, "Flashforge G-code", file.data.size());
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (!result(reply, "upload and print").isEmpty()) emit completed();
    });
}
