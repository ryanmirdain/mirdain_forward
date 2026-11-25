#include "app/Account.h"
#include "app/AppConfig.h"
#include "app/LocalAddress.h"

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QUrl>

// The public Cognito client the Mirdain sites and slicer share; Cognito allows Forward's
// /callback as its redirect.
static const QString AuthDomain = "https://auth.mirdainsystems.com";
static const QString ClientId = "60vq9lui8oji9egopaj0v0idl9";
static const QString RedirectUri = QString("http://localhost:%1/callback").arg(ForwardPort);
static const QString ApiUrl = FORWARD_API_URL;

static QByteArray base64Url(const QByteArray &bytes)
{
    return bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

static QByteArray randomBytes(int count)
{
    QByteArray bytes(count, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(bytes.data()), count / 4);
    return bytes;
}

// A JWT's payload. Cognito signed it, and Forward only reads its expiry and email.
static QJsonObject claims(const QString &token)
{
    return QJsonDocument::fromJson(QByteArray::fromBase64(token.section('.', 1, 1).toLatin1(),
        QByteArray::Base64UrlEncoding)).object();
}

Account::Account(AppConfig &config, QNetworkAccessManager &network) : config(config), network(network) {}

bool Account::signedIn() const
{
    return !config.section("account").value("refresh_token").toString().isEmpty();
}

QString Account::email() const
{
    return config.section("account").value("email").toString();
}

QJsonObject Account::organization() const
{
    return config.section("account").value("organization").toObject();
}

void Account::setOrganization(const QJsonObject &organization)
{
    auto account = config.section("account");
    account.insert("organization", organization);
    config.setSection("account", account);
}

void Account::beginSignIn()
{
    const auto verifier = base64Url(randomBytes(32));
    const auto state = QString::fromLatin1(base64Url(randomBytes(16)));
    pendingSignIns[state] = verifier;
    lastSignInError.clear();
    QUrlQuery query{{"client_id", ClientId}, {"redirect_uri", RedirectUri}, {"response_type", "code"},
        {"scope", "email openid"}, {"code_challenge", base64Url(QCryptographicHash::hash(verifier, QCryptographicHash::Sha256))},
        {"code_challenge_method", "S256"}, {"state", state}};
    QUrl url(AuthDomain + "/oauth2/authorize");
    url.setQuery(query);
    QDesktopServices::openUrl(url);
}

bool Account::completeSignIn(const QUrlQuery &callback)
{
    const auto pending = pendingSignIns.find(callback.queryItemValue("state", QUrl::FullyDecoded));
    if (pending == pendingSignIns.end()) return false;
    const auto verifier = pending->second;
    pendingSignIns.erase(pending);
    if (callback.hasQueryItem("error")) {
        lastSignInError = callback.queryItemValue("error_description", QUrl::FullyDecoded);
        if (lastSignInError.isEmpty()) lastSignInError = callback.queryItemValue("error", QUrl::FullyDecoded);
        return true;
    }
    exchange({{"grant_type", "authorization_code"}, {"code", callback.queryItemValue("code", QUrl::FullyDecoded)},
        {"code_verifier", verifier}, {"redirect_uri", RedirectUri}}, [this](int status, const QJsonObject &tokens) {
        if (status != 200) {
            lastSignInError = status ? QString("Sign-in failed (HTTP %1). Try again.").arg(status)
                                     : "Could not reach Mirdain to finish signing in. Check your connection and try again.";
            return;
        }
        pendingSignIns.clear();
        saveTokens(tokens);
        if (changed) changed();
    });
    return true;
}

void Account::signOut()
{
    accessToken.clear();
    config.setSection("account", {});
    if (changed) changed();
}

void Account::session(std::function<void(const QString &token, qint64 expiresAt)> use)
{
    withAccessToken([this, use](const QString &token) { use(token, token.isEmpty() ? 0 : accessExpiry.toSecsSinceEpoch()); });
}

void Account::saveTokens(const QJsonObject &tokens)
{
    accessToken = tokens.value("access_token").toString();
    accessExpiry = QDateTime::fromSecsSinceEpoch(claims(accessToken).value("exp").toInteger());
    auto account = config.section("account");
    // Refreshing returns no new refresh token or, without an ID token, email.
    if (tokens.contains("refresh_token")) account.insert("refresh_token", tokens.value("refresh_token"));
    if (tokens.contains("id_token")) {
        const auto id = claims(tokens.value("id_token").toString());
        account.insert("email", id.contains("email") ? id.value("email") : id.value("cognito:username"));
    }
    config.setSection("account", account);
}

void Account::exchange(const QUrlQuery &form, std::function<void(int status, const QJsonObject &tokens)> done)
{
    auto body = form;
    body.addQueryItem("client_id", ClientId);
    QNetworkRequest request(QUrl(AuthDomain + "/oauth2/token"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    request.setTransferTimeout(30000);
    auto *reply = network.post(request, body.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [reply, done] {
        reply->deleteLater();
        done(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), QJsonDocument::fromJson(reply->readAll()).object());
    });
}

void Account::withAccessToken(std::function<void(const QString &token)> use)
{
    if (!accessToken.isEmpty() && accessExpiry > QDateTime::currentDateTime().addSecs(60)) {
        use(accessToken);
        return;
    }
    const auto refresh = config.section("account").value("refresh_token").toString();
    if (refresh.isEmpty()) {
        use({});
        return;
    }
    waitingForToken.append(std::move(use));
    if (waitingForToken.size() > 1) return;
    exchange({{"grant_type", "refresh_token"}, {"refresh_token", refresh}}, [this](int status, const QJsonObject &tokens) {
        if (status == 200) saveTokens(tokens);
        // Cognito rejects a revoked or expired refresh token: sign in again.
        else if (status == 400) signOut();
        else accessToken.clear();
        const auto waiting = std::exchange(waitingForToken, {});
        for (const auto &use : waiting) use(accessToken);
    });
}

void Account::request(const QByteArray &verb, const QString &path, const QJsonDocument &body, Done done, bool authenticated)
{
    const auto send = [this, verb, path, body, done](const QString &token) {
        QNetworkRequest request(QUrl(ApiUrl + path));
        request.setTransferTimeout(30000);
        if (!token.isEmpty()) request.setRawHeader("Authorization", "Bearer " + token.toUtf8());
        if (!body.isNull()) request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        auto *reply = network.sendCustomRequest(request, verb, body.isNull() ? QByteArray() : body.toJson(QJsonDocument::Compact));
        connect(reply, &QNetworkReply::finished, this, [reply, done] {
            reply->deleteLater();
            done(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), QJsonDocument::fromJson(reply->readAll()));
        });
    };
    if (!authenticated) {
        send({});
        return;
    }
    withAccessToken([this, send, done](const QString &token) {
        if (token.isEmpty()) done(signedIn() ? 0 : 401, {});
        else send(token);
    });
}
