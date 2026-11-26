#include "printers/SimplyPrintSender.h"
#include "printers/UploadProgress.h"

#include <QHttpMultiPart>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QRegularExpression>

#include <stdexcept>
#include <utility>

std::unique_ptr<PrinterSendJob> SimplyPrintSender::createJob(const QJsonObject &connection) const
{
    return std::make_unique<SimplyPrintSendJob>(connection, autoLevelBedEnabled());
}

SimplyPrintSendJob::SimplyPrintSendJob(const QJsonObject &connection, bool autoLevelBed)
    : PrinterSendJob(autoLevelBed)
{
    // host is SimplyPrint's API host, api.simplyprint.io; uploads go to its files host.
    api = connectionUrl(connection, "https", 443);
    const auto company = connection.value("company_id").toVariant().toString().trimmed();
    printerId = connection.value("printer_id").toVariant().toString().trimmed();
    apiKey = connection.value("api_key").toString().trimmed().toUtf8();
    const QRegularExpression digits("^\\d+$");
    if (!digits.match(company).hasMatch() || !digits.match(printerId).hasMatch() || apiKey.isEmpty())
        throw std::runtime_error("SimplyPrint requires a numeric company_id and printer_id, and an api_key.");
    api.setPath("/" + company + "/");
    files = api;
    files.setHost(QString(api.host()).replace(QRegularExpression("^api\\."), "files."));
}

QNetworkRequest SimplyPrintSendJob::request(QUrl base, const QString &path, const QString &query) const
{
    base.setPath(base.path() + path);
    if (!query.isEmpty()) base.setQuery(query);
    QNetworkRequest request(base);
    request.setTransferTimeout(30000);
    request.setRawHeader("X-API-KEY", apiKey);
    request.setRawHeader("Accept", "application/json");
    return request;
}

void SimplyPrintSendJob::call(QNetworkReply *reply, const QString &action, std::function<void(const QJsonObject &)> next)
{
    connect(reply, &QNetworkReply::finished, this, [this, reply, action, next = std::move(next)] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto body = QJsonDocument::fromJson(reply->readAll()).object();
        if (reply->error() != QNetworkReply::NoError || !body.value("status").toBool()) {
            const auto message = body.value("message").toString();
            emit failed(QString("SimplyPrint %1 failed (HTTP %2): %3").arg(action).arg(status)
                .arg(message.isEmpty() ? reply->errorString() : message));
            return;
        }
        next(body);
    });
}

void SimplyPrintSendJob::send(PrinterFile input)
{
    file = std::move(input);
    emit progress("Checking printer in SimplyPrint…");
    call(network.get(request(api, "printers/Get", "pid=" + printerId)), "printer status", [this](const QJsonObject &body) {
        const auto printer = body.value("data").toObject().value("printer").toObject();
        const auto state = printer.value("state").toString();
        if (!printer.value("online").toBool() || state != "operational" || printer.value("awaitingBedClear").toBool()) {
            emit failed("SimplyPrint reports the printer is not ready ("
                + (printer.value("awaitingBedClear").toBool() ? QString("awaiting bed clear") : state.isEmpty() ? QString("unknown") : state)
                + "). The current job was left running.");
            return;
        }
        upload();
    });
}

void SimplyPrintSendJob::upload()
{
    auto *form = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    QHttpPart part;
    part.setHeader(QNetworkRequest::ContentDispositionHeader,
        QString("form-data; name=\"file\"; filename=\"%1\"").arg(file.filename));
    part.setHeader(QNetworkRequest::ContentTypeHeader, "application/octet-stream");
    part.setBody(file.data);
    form->append(part);
    auto upload = request(files, "files/Upload");
    upload.setTransferTimeout(0);
    auto *reply = network.post(upload, form);
    form->setParent(reply);
    reportUploadProgress(this, reply, "SimplyPrint G-code", file.data.size());
    call(reply, "upload", [this](const QJsonObject &body) {
        const auto fileId = body.value("file").toObject().value("id").toVariant().toString();
        if (fileId.isEmpty()) {
            emit failed("SimplyPrint did not return an uploaded file ID.");
            return;
        }
        createJob(fileId);
    });
}

void SimplyPrintSendJob::createJob(const QString &fileId)
{
    emit progress("Starting print through SimplyPrint…");
    auto start = request(api, "printers/actions/CreateJob", "pid=" + printerId);
    start.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    auto *reply = network.post(start, QJsonDocument(QJsonObject{{"file_id", fileId}}).toJson(QJsonDocument::Compact));
    call(reply, "print start", [this](const QJsonObject &) { emit completed(); });
}
