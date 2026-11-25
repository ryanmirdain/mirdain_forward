#pragma once

#include <QJsonObject>
#include <QObject>
#include <QUrl>

#include <functional>

class Account;
class AppConfig;
class AppPreferences;
class PrinterSettings;
class QHttpServer;
class QHttpServerRequest;
class QHttpServerResponder;
class QHttpServerResponse;

// Serves Forward's own pages (onboarding, Add Printer, organization; built from web/) and the
// /api/app/* routes they call. The pages are same-origin with these routes, which answer no
// other origin. Forward calls the Mirdain API for them with its own session.
class SetupServer : public QObject {
public:
    SetupServer(QHttpServer &server, AppConfig &config, AppPreferences &preferences, Account &account,
        PrinterSettings &printers, const QUrl &slicerUrl);
    bool onboardingComplete() const;
    // Registers Forward with organization {id, name} and loads its Forward printers into the menu
    // bar. error is empty on success.
    void switchOrganization(const QJsonObject &organization, std::function<void(const QString &error)> done);
    // Runs after the organization or its printers change.
    std::function<void()> organizationChanged;

private:
    using Respond = std::function<void(QHttpServerResponse &&)>;
    AppConfig &config;
    AppPreferences &preferences;
    Account &account;
    PrinterSettings &printers;
    QUrl slicerUrl;

    QJsonObject state() const;
    void route(QHttpServer &server, const QString &path, bool post,
        std::function<void(const QJsonObject &body, const Respond &respond)> handle);
};
