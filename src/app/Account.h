#pragma once

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QUrlQuery>

#include <functional>
#include <map>

class AppConfig;
class QNetworkAccessManager;

// Forward's own Mirdain session: Cognito sign-in in the default browser (authorization code
// with PKCE, redirected to Forward's /callback), the refresh token in the app config, and
// authenticated calls to the Mirdain API. Also remembers the organization Forward is
// registered with.
class Account : public QObject {
public:
    Account(AppConfig &config, QNetworkAccessManager &network);

    bool signedIn() const;
    QString email() const;
    // The last sign-in that failed, until the next one starts.
    QString signInError() const { return lastSignInError; }
    // Opens Cognito's sign-in page in the default browser.
    void beginSignIn();
    // Finishes a sign-in begun by beginSignIn. Returns false when the callback belongs to
    // someone else, such as the slicer window's own sign-in.
    bool completeSignIn(const QUrlQuery &callback);
    void signOut();
    // Runs after Forward signs in or out.
    std::function<void()> changed;
    // A current access token and its expiry (seconds since the epoch), refreshed as needed, for
    // the slicer in Forward's window, which shares Forward's account. The token is empty when
    // signed out or when Mirdain is unreachable.
    void session(std::function<void(const QString &token, qint64 expiresAt)> use);

    // {id, name}, or empty before an organization is chosen.
    QJsonObject organization() const;
    void setOrganization(const QJsonObject &organization);

    // Calls the Mirdain API at path (e.g. /api/v1/client/home). status is 0 when the API is
    // unreachable and 401 when Forward is signed out.
    using Done = std::function<void(int status, const QJsonDocument &body)>;
    void request(const QByteArray &verb, const QString &path, const QJsonDocument &body, Done done, bool authenticated = true);

private:
    AppConfig &config;
    QNetworkAccessManager &network;
    // Code verifiers of this session's sign-ins, by state. Opening the page again starts
    // another sign-in, and either tab may be the one that completes.
    std::map<QString, QByteArray> pendingSignIns;
    QString lastSignInError;
    QString accessToken;
    QDateTime accessExpiry;
    QList<std::function<void(const QString &token)>> waitingForToken;

    void withAccessToken(std::function<void(const QString &token)> use);
    void exchange(const QUrlQuery &form, std::function<void(int status, const QJsonObject &tokens)> done);
    void saveTokens(const QJsonObject &tokens);
};
