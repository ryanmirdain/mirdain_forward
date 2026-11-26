#include "app/SetupServer.h"
#include "app/Account.h"
#include "app/AppConfig.h"
#include "app/AppPreferences.h"
#include "app/LocalAddress.h"
#include "printers/PrinterSettings.h"

#include <QFile>
#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerResponder>
#include <QJsonArray>
#include <QJsonDocument>

#include <memory>
#include <stdexcept>

using Status = QHttpServerResponder::StatusCode;

// Same-origin requests from Forward's pages, addressed to Forward by name: the Host check
// turns away DNS rebinding and the Origin check other sites' pages.
static bool fromForwardPage(const QHttpServerRequest &request)
{
    const auto host = request.headers().value("Host").toByteArray();
    const auto origin = request.headers().value("Origin").toByteArray();
    const auto local = [](const QByteArray &prefix, const QByteArray &value) {
        return value == prefix + "127.0.0.1:" + QByteArray::number(ForwardPort) || value == prefix + "localhost:" + QByteArray::number(ForwardPort);
    };
    return local("", host) && (origin.isEmpty() || local("http://", origin));
}

static QHttpServerResponse json(Status status, const QJsonValue &body)
{
    QHttpServerResponse response = body.isArray() ? QHttpServerResponse(body.toArray(), status) : QHttpServerResponse(body.toObject(), status);
    auto headers = response.headers();
    headers.append("Cache-Control", "no-store");
    response.setHeaders(headers);
    return response;
}

static QHttpServerResponse error(Status status, const QString &message)
{
    return json(status, QJsonObject{{"error", message}});
}

// The message to show for a failed Mirdain API call.
static QString apiError(int status, const QJsonDocument &body)
{
    if (status == 0) return "Could not reach Mirdain. Check your internet connection and try again.";
    if (status == 401) return "Your Mirdain session ended. Sign in again to continue.";
    const auto detail = body.object().value("detail");
    if (detail.isString()) return detail.toString();
    // FastAPI validation errors list each problem.
    if (detail.isArray()) return detail.toArray().first().toObject().value("msg").toString();
    return QString("Mirdain returned HTTP %1. Try again.").arg(status);
}

static QHttpServerResponse resource(const QString &path, const QByteArray &type)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error(("Missing resource: " + path).toStdString());
    return QHttpServerResponse(type, file.readAll());
}

SetupServer::SetupServer(QHttpServer &server, AppConfig &config, AppPreferences &preferences, Account &account,
    PrinterSettings &printers, const QUrl &slicerUrl)
    : config(config), preferences(preferences), account(account), printers(printers), slicerUrl(slicerUrl)
{
    // The pages: one HTML file with its script and styles inlined, and the app's fonts.
    route(server, "/", false, [](const QJsonObject &, const Respond &respond) {
        auto page = resource(":/web/index.html", "text/html; charset=utf-8");
        auto headers = page.headers();
        headers.append("Cache-Control", "no-store");
        page.setHeaders(headers);
        respond(std::move(page));
    });
    for (const QString name : {"Aileron-Regular.otf", "Aileron-SemiBold.otf"})
        route(server, "/fonts/" + name, false, [name](const QJsonObject &, const Respond &respond) {
            respond(resource(":/assets/fonts/" + name, "font/otf"));
        });

    route(server, "/api/app/state", false, [this](const QJsonObject &, const Respond &respond) {
        respond(json(Status::Ok, state()));
    });
    route(server, "/api/app/preferences", true, [this](const QJsonObject &body, const Respond &respond) {
        this->preferences.save(body.value("launchAtSignIn").toBool(), body.value("notifications").toBool(),
            body.value("updates").toBool(), body.value("quitOnClose").toBool());
        respond(json(Status::Ok, state()));
    });
    route(server, "/api/app/startup-settings", true, [this](const QJsonObject &, const Respond &respond) {
        this->preferences.openStartupSettings();
        respond(QHttpServerResponse(Status::NoContent));
    });

    // Sign-in finishes at /callback (see main.cpp); the page polls /api/app/state until then.
    route(server, "/api/app/sign-in", true, [this](const QJsonObject &, const Respond &respond) {
        this->account.beginSignIn();
        respond(QHttpServerResponse(Status::NoContent));
    });
    route(server, "/api/app/sign-out", true, [this](const QJsonObject &, const Respond &respond) {
        this->account.signOut();
        respond(json(Status::Ok, state()));
    });

    route(server, "/api/app/organizations", false, [this](const QJsonObject &, const Respond &respond) {
        this->account.request("GET", "/api/v1/client/home", {}, [respond](int status, const QJsonDocument &body) {
            if (status != 200) {
                respond(error(Status::BadGateway, apiError(status, body)));
                return;
            }
            // Guests cannot add printers.
            QJsonArray memberships;
            for (const auto &value : body.object().value("memberships").toArray()) {
                const auto membership = value.toObject();
                if (membership.value("accepted").toBool() && membership.value("role").toString() != "guest") memberships.append(membership);
            }
            respond(json(Status::Ok, QJsonObject{{"memberships", memberships},
                {"selected", body.object().value("selected_organization_id")}}));
        });
    });
    route(server, "/api/app/organization", true, [this](const QJsonObject &body, const Respond &respond) {
        const QJsonObject organization{{"id", body.value("id").toInteger()}, {"name", body.value("name").toString()}};
        switchOrganization(organization, [this, respond](const QString &message) {
            if (!message.isEmpty()) {
                respond(error(Status::BadGateway, message));
                return;
            }
            auto saved = this->config.section("preferences");
            saved.insert("onboarding_complete", true);
            this->config.setSection("preferences", saved);
            respond(json(Status::Ok, state()));
        });
    });

    // Add Printer, as on mirdainsystems.com/forward: the preset catalog, then the new printer.
    route(server, "/api/app/printer-presets", false, [this](const QJsonObject &, const Respond &respond) {
        this->account.request("GET", "/api/v1/printer-presets", {}, [respond](int status, const QJsonDocument &body) {
            respond(status == 200 ? json(Status::Ok, body.array()) : error(Status::BadGateway, apiError(status, body)));
        }, false);
    });
    route(server, "/api/app/printers", true, [this](const QJsonObject &body, const Respond &respond) {
        const auto organization = this->account.organization();
        if (organization.isEmpty()) {
            respond(error(Status::Conflict, "Choose an organization before adding a printer."));
            return;
        }
        const auto path = QString("/api/v1/client/organizations/%1/slicer-printers").arg(organization.value("id").toInteger());
        this->account.request("POST", path, QJsonDocument(body), [this, organization, respond](int status, const QJsonDocument &saved) {
            if (status != 201) {
                respond(error(Status::BadGateway, apiError(status, saved)));
                return;
            }
            const QJsonObject printer{{"id", saved.object().value("id")}, {"name", saved.object().value("name")}};
            // The printer is saved either way; a failed refresh leaves the menu until the next one.
            switchOrganization(organization, [respond, printer](const QString &) { respond(json(Status::Created, printer)); });
        });
    });
}

bool SetupServer::onboardingComplete() const
{
    return config.section("preferences").value("onboarding_complete").toBool();
}

void SetupServer::switchOrganization(const QJsonObject &organization, std::function<void(const QString &error)> done)
{
    const auto path = QString("/api/v1/client/organizations/%1/slicer-printers").arg(organization.value("id").toInteger());
    account.request("GET", path, {}, [this, organization, done](int status, const QJsonDocument &body) {
        if (status != 200) {
            done(apiError(status, body));
            return;
        }
        try {
            printers.replaceWithOrganization(body.array());
            account.setOrganization(organization);
        } catch (const std::exception &failure) {
            done(QString::fromUtf8(failure.what()));
            return;
        }
        if (organizationChanged) organizationChanged();
        done({});
    });
}

QJsonObject SetupServer::state() const
{
    const auto organization = account.organization();
    const auto nullable = [](const QString &value) { return value.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(value); };
    return {{"preferences", preferences.toJson()}, {"signedIn", account.signedIn()},
        {"email", nullable(account.email())}, {"signInError", nullable(account.signInError())},
        {"organization", organization.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(organization)},
        {"printers", int(printers.entries().size())}, {"slicerUrl", slicerUrl.toString()}};
}

void SetupServer::route(QHttpServer &server, const QString &path, bool post,
    std::function<void(const QJsonObject &body, const Respond &respond)> handle)
{
    server.route(path, post ? QHttpServerRequest::Method::Post : QHttpServerRequest::Method::Get, this,
        [handle, post](const QHttpServerRequest &request, QHttpServerResponder &responder) {
            if (!fromForwardPage(request)) {
                responder.sendResponse(error(Status::Forbidden, "Only Forward's own pages can use this."));
                return;
            }
            auto shared = std::make_shared<QHttpServerResponder>(std::move(responder));
            const Respond respond = [shared](QHttpServerResponse &&response) { shared->sendResponse(std::move(response)); };
            try {
                QJsonObject body;
                if (post && !request.body().isEmpty()) {
                    if (request.body().size() > 64 * 1024) throw std::runtime_error("Send up to 64 KiB.");
                    const auto document = QJsonDocument::fromJson(request.body());
                    if (!document.isObject()) throw std::runtime_error("Expected a JSON object.");
                    body = document.object();
                }
                handle(body, respond);
            } catch (const std::exception &failure) {
                respond(error(Status::InternalServerError, QString::fromUtf8(failure.what())));
            }
        });
}
