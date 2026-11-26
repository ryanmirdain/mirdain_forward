#include "printers/UltimakerSender.h"
#include "printers/UploadProgress.h"

#include <QAuthenticator>
#include <QHttpMultiPart>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QUrlQuery>

#include <utility>

std::unique_ptr<PrinterSendJob> UltimakerSender::createJob(const QJsonObject &connection) const
{
    return std::make_unique<UltimakerSendJob>(connection, approved);
}

UltimakerSendJob::UltimakerSendJob(const QJsonObject &connection, std::shared_ptr<UltimakerSender::Credentials> approved)
    : PrinterSendJob(false), credentials(std::move(approved))
{
    origin = connectionUrl(connection, "http", 80);
    if (credentials->id.isEmpty()) {
        credentials->id = connection.value("auth_id").toString().trimmed();
        credentials->key = connection.value("auth_key").toString().trimmed();
    }
    approval.setInterval(2000);
    approval.setSingleShot(true);
    connect(&approval, &QTimer::timeout, this, &UltimakerSendJob::awaitApproval);
    // Write requests use HTTP Digest with the approved id and key.
    connect(&network, &QNetworkAccessManager::authenticationRequired, this, [this](QNetworkReply *reply, QAuthenticator *auth) {
        if (reply->property("digestAttempted").toBool()) return;
        reply->setProperty("digestAttempted", true);
        auth->setUser(credentials->id);
        auth->setPassword(credentials->key);
    });
}

QNetworkRequest UltimakerSendJob::request(const QString &path) const
{
    auto url = origin;
    url.setPath(path);
    QNetworkRequest request(url);
    request.setTransferTimeout(15000);
    return request;
}

void UltimakerSendJob::get(const QString &path, std::function<void(int, const QJsonDocument &)> next)
{
    auto *reply = network.get(request(path));
    connect(reply, &QNetworkReply::finished, this, [this, reply, next = std::move(next)] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError && status != 401 && status != 404) {
            emit failed(QString("UltiMaker request failed (HTTP %1): %2").arg(status).arg(reply->errorString()));
            return;
        }
        next(status, QJsonDocument::fromJson(reply->readAll()));
    });
}

void UltimakerSendJob::send(PrinterFile input)
{
    file = std::move(input);
    emit progress("Connecting to UltiMaker…");
    if (credentials->id.isEmpty()) requestCredentials();
    else verifyCredentials();
}

// GET /auth/verify needs Digest credentials and answers 401 when the printer no longer knows them.
void UltimakerSendJob::verifyCredentials()
{
    get("/api/v1/auth/verify", [this](int status, const QJsonDocument &) {
        if (status == 200) checkIdle();
        else requestCredentials();
    });
}

void UltimakerSendJob::requestCredentials()
{
    auto authRequest = request("/api/v1/auth/request");
    authRequest.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    const QUrlQuery form{{"application", "Mirdain Forward"}, {"user", "Mirdain Forward"}};
    auto *reply = network.post(authRequest, form.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit failed("UltiMaker authorization request failed: " + reply->errorString());
            return;
        }
        const auto result = QJsonDocument::fromJson(reply->readAll()).object();
        credentials->id = result.value("id").toString();
        credentials->key = result.value("key").toString();
        if (credentials->id.isEmpty() || credentials->key.isEmpty()) {
            emit failed("UltiMaker did not return authorization credentials.");
            return;
        }
        emit progress("Allow Mirdain Forward on the UltiMaker's touchscreen…");
        approvalPolls = 0;
        awaitApproval();
    });
}

void UltimakerSendJob::awaitApproval()
{
    get("/api/v1/auth/check/" + credentials->id, [this](int, const QJsonDocument &result) {
        const auto message = result.object().value("message").toString();
        if (message == "authorized") {
            checkIdle();
            return;
        }
        if (message == "unauthorized" || ++approvalPolls >= 30) {
            *credentials = {};
            emit failed(message == "unauthorized" ? QString("Mirdain Forward was denied on the UltiMaker's touchscreen.")
                : QString("Mirdain Forward was not allowed on the UltiMaker's touchscreen within 60 seconds."));
            return;
        }
        approval.start();
    });
}

void UltimakerSendJob::checkIdle()
{
    emit progress("Checking UltiMaker printer…");
    auto *reply = network.get(request("/api/v1/printer/status"));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit failed("UltiMaker status request failed: " + reply->errorString());
            return;
        }
        // A bare JSON string such as "idle", "printing", "error", "maintenance", or "booting".
        const auto status = QString::fromUtf8(reply->readAll()).trimmed().remove('"');
        if (status != "idle") {
            emit failed("UltiMaker is busy or not ready (" + status + "). The current job was left running.");
            return;
        }
        upload();
    });
}

void UltimakerSendJob::upload()
{
    auto *form = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    QHttpPart part;
    part.setHeader(QNetworkRequest::ContentDispositionHeader,
        QString("form-data; name=\"file\"; filename=\"%1\"").arg(file.filename));
    part.setHeader(QNetworkRequest::ContentTypeHeader, "application/octet-stream");
    part.setBody(file.data);
    form->append(part);
    auto upload = request("/api/v1/print_job");
    upload.setTransferTimeout(0); // Large uploads exceed the 15-second request limit.
    auto *reply = network.post(upload, form);
    form->setParent(reply);
    reportUploadProgress(this, reply, "UltiMaker G-code", file.data.size());
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError) {
            emit failed(QString("UltiMaker rejected the print job (HTTP %1): %2").arg(status).arg(reply->errorString()));
            return;
        }
        emit completed();
    });
}
