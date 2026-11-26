#include "printers/SnapmakerSender.h"
#include "printers/UploadProgress.h"

#include <QHttpMultiPart>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QUrlQuery>

#include <utility>

std::unique_ptr<PrinterSendJob> SnapmakerSender::createJob(const QJsonObject &connection) const
{
    return std::make_unique<SnapmakerSendJob>(connection, approvedToken);
}

SnapmakerSendJob::SnapmakerSendJob(const QJsonObject &connection, std::shared_ptr<QString> approvedToken)
    : PrinterSendJob(false), token(std::move(approvedToken))
{
    origin = connectionUrl(connection, "http", 8080);
    if (token->isEmpty()) *token = connection.value("token").toString().trimmed();
    approval.setInterval(2000);
    approval.setSingleShot(true);
    connect(&approval, &QTimer::timeout, this, &SnapmakerSendJob::awaitApproval);
}

QNetworkRequest SnapmakerSendJob::request(const QString &path) const
{
    auto url = origin;
    url.setPath(path);
    QNetworkRequest request(url);
    request.setTransferTimeout(15000);
    return request;
}

void SnapmakerSendJob::send(PrinterFile input)
{
    file = std::move(input);
    emit progress("Connecting to Snapmaker…");
    if (token->isEmpty()) {
        connectPrinter();
        return;
    }
    readStatus([this](int status, const QJsonObject &json) {
        if (status == 200) checkIdle(json);
        else connectPrinter(); // The saved token was revoked or the printer restarted.
    });
}

// POST /api/v1/connect returns a token the touchscreen must approve before it is usable.
void SnapmakerSendJob::connectPrinter()
{
    auto connectRequest = request("/api/v1/connect");
    connectRequest.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    auto *reply = network.post(connectRequest, QByteArray());
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit failed("Snapmaker connection failed: " + reply->errorString());
            return;
        }
        *token = QJsonDocument::fromJson(reply->readAll()).object().value("token").toString();
        if (token->isEmpty()) {
            emit failed("Snapmaker did not return a connection token.");
            return;
        }
        emit progress("Approve Mirdain Forward on the Snapmaker touchscreen…");
        approvalPolls = 0;
        awaitApproval();
    });
}

void SnapmakerSendJob::readStatus(std::function<void(int, const QJsonObject &)> next)
{
    auto url = origin;
    url.setPath("/api/v1/status");
    url.setQuery(QUrlQuery{{"token", *token}});
    QNetworkRequest statusRequest(url);
    statusRequest.setTransferTimeout(15000);
    auto *reply = network.get(statusRequest);
    connect(reply, &QNetworkReply::finished, this, [this, reply, next = std::move(next)] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError && status != 401) {
            emit failed(QString("Snapmaker status request failed (HTTP %1): %2").arg(status).arg(reply->errorString()));
            return;
        }
        next(status, QJsonDocument::fromJson(reply->readAll()).object());
    });
}

void SnapmakerSendJob::awaitApproval()
{
    readStatus([this](int status, const QJsonObject &json) {
        if (status == 200) {
            checkIdle(json);
            return;
        }
        if (status == 401) {
            token->clear();
            emit failed("The connection was declined on the Snapmaker touchscreen.");
            return;
        }
        if (++approvalPolls >= 30) {
            token->clear();
            emit failed("Snapmaker connection was not approved on the touchscreen within 60 seconds.");
            return;
        }
        approval.start();
    });
}

void SnapmakerSendJob::checkIdle(const QJsonObject &status)
{
    const auto state = status.value("status").toString();
    if (state != "IDLE" && state != "FINISHED" && state != "STOPPED") {
        emit failed("Snapmaker is busy or not ready (" + (state.isEmpty() ? QString("unknown") : state)
            + "). The current job was left running.");
        return;
    }
    upload();
}

void SnapmakerSendJob::upload()
{
    auto *form = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    for (const auto &[name, value] : {std::pair{"token", token->toUtf8()}, std::pair{"type", QByteArray("3DP")}}) {
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
    auto upload = request("/api/v1/prepare_print");
    upload.setTransferTimeout(0); // Large uploads over the printer's Wi-Fi exceed the 15-second request limit.
    auto *reply = network.post(upload, form);
    form->setParent(reply);
    reportUploadProgress(this, reply, "Snapmaker G-code", file.data.size());
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError) {
            emit failed(QString("Snapmaker upload failed (HTTP %1): %2").arg(status).arg(reply->errorString()));
            return;
        }
        startPrint();
    });
}

void SnapmakerSendJob::startPrint()
{
    emit progress("Starting print…");
    auto start = request("/api/v1/start_print");
    start.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    auto *reply = network.post(start, QUrlQuery{{"token", *token}}.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError) {
            emit failed(QString("Uploaded, but Snapmaker rejected the start (HTTP %1): %2. Check the touchscreen before retrying.")
                .arg(status).arg(reply->errorString()));
            return;
        }
        emit completed();
    });
}
