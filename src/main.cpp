#include "app/Account.h"
#include "app/AppConfig.h"
#include "app/AppPreferences.h"
#include "app/LocalAddress.h"
#include "app/SetupServer.h"
#include "platform/DragWatcher.h"
#include "platform/NotificationSender.h"
#include "platform/AppUpdater.h"
#include "platform/ApplicationReopen.h"
#include "platform/RemovableDrives.h"
#include "platform/SlicerWindow.h"
#include "printers/BambuFilaments.h"
#include "printers/PrinterSender.h"
#include "printers/PrinterSettings.h"
#include "archive/ZipArchive.h"

#include <QApplication>
#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFileOpenEvent>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerResponder>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QMenu>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QTcpServer>
#include <QTemporaryFile>
#include <QTimer>
#include <QUrlQuery>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

#include <functional>
#include <memory>
#include <stdexcept>

using Status = QHttpServerResponder::StatusCode;
using Reply = std::function<void(Status, const QJsonObject &)>;
static const QUrl SlicerUrl(FORWARD_SLICER_URL);

// The slicer locked to Forward's organization: with organization set, and force_org not false,
// it hides its organization picker. Unlocked until an organization is chosen.
static QUrl slicerUrl(const QJsonObject &organization)
{
    if (organization.isEmpty()) return SlicerUrl;
    auto url = SlicerUrl;
    url.setQuery(QUrlQuery{{"organization", QString::number(organization.value("id").toInteger())}, {"force_org", "true"}});
    return url;
}

static bool allowedOrigin(const QByteArray &origin)
{
    return origin.isEmpty() || origin == "https://slicer.mirdainsystems.com"
        || origin == "https://mirdainsystems.com" || origin == "https://www.mirdainsystems.com"
        || origin == "http://localhost:5173" || origin == "http://127.0.0.1:5173"
        || origin == "http://localhost:3000" || origin == "http://127.0.0.1:3000";
}

static QHttpServerResponse response(const QByteArray &origin, Status status, const QJsonObject &body)
{
    QHttpServerResponse result(body, status);
    auto headers = result.headers();
    if (!origin.isEmpty() && allowedOrigin(origin)) {
        headers.append("Access-Control-Allow-Origin", origin);
        headers.append("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        headers.append("Access-Control-Allow-Headers", "Content-Type");
        headers.append("Access-Control-Allow-Private-Network", "true");
        headers.append("Vary", "Origin");
    }
    result.setHeaders(headers);
    return result;
}

// Models that Forward accepts from Finder, Explorer, or its command line, as absolute paths.
static QStringList modelPaths(const QStringList &arguments)
{
    QStringList paths;
    for (const auto &argument : arguments) {
        const QFileInfo file(argument);
        const auto suffix = file.suffix().toLower();
        if (file.isFile() && (suffix == "stl" || suffix == "3mf")) paths.append(file.absoluteFilePath());
    }
    return paths;
}

static QJsonObject parseObject(const QByteArray &bytes)
{
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        throw std::runtime_error("Expected a JSON object.");
    return document.object();
}

static QString plateGcodePath(ZipArchive &archive, bool native)
{
    const auto members = archive.members().filter(QRegularExpression(
        native ? "^Metadata/plate_[0-9]+\\.gcode$" : "\\.gcode$"));
    if (members.size() != 1)
        throw std::runtime_error("The selected plate archive must contain one G-code file.");
    return members.first();
}

struct Job {
    explicit Job(Reply reply) : reply(std::move(reply)) {}
    Reply reply;
    QByteArray downloadAuthorization;
    QTemporaryFile archive;
    QString sliceId;
    QString filename;
    QJsonObject connection;
    PrinterSender *sender = nullptr;
    QStorageInfo drive;
    QString doneMessage;
    QJsonObject result;
};

class Forwarder : public QObject {
public:
    Forwarder() : printerSettings(config, menu)
    {
        status = menu.addAction("Idle - 127.0.0.1:18744");
        status->setEnabled(false);
        menu.addSeparator();
        menu.addAction("Quit", qApp, &QApplication::quit);
        QIcon icon(":/assets/mirdain-mark.png");
        icon.setIsMask(true);
        tray.setIcon(icon);
        tray.setToolTip("Mirdain Forward");
        tray.setContextMenu(&menu);
        tray.show();

        server.route("/health", QHttpServerRequest::Method::Get | QHttpServerRequest::Method::Options,
            this, [](const QHttpServerRequest &request) {
                const auto origin = request.headers().value("Origin").toByteArray();
                if (!allowedOrigin(origin))
                    return response(origin, Status::Forbidden, {{"error", "Origin not allowed."}});
                if (request.method() == QHttpServerRequest::Method::Options)
                    return response(origin, Status::NoContent, {});
                auto result = response(origin, Status::Ok, {{"ok", true}, {"service", "mirdain_forward"},
                    {"version", QCoreApplication::applicationVersion()}});
                auto headers = result.headers();
                headers.append("Cache-Control", "no-store");
                result.setHeaders(headers);
                return result;
            });

        server.route("/send", QHttpServerRequest::Method::Post | QHttpServerRequest::Method::Options,
            this, [this](const QHttpServerRequest &request, QHttpServerResponder &responder) {
                route(request, responder, [this](const QJsonObject &body, Reply reply) { startSend(body, reply); });
            });

        // Saves one plate file (.gcode or .gcode.3mf) to a connected USB drive or SD card.
        server.route("/usb", QHttpServerRequest::Method::Post | QHttpServerRequest::Method::Options,
            this, [this](const QHttpServerRequest &request, QHttpServerResponder &responder) {
                route(request, responder, [this](const QJsonObject &body, Reply reply) { startUsb(body, reply); });
            });

        // Reads the printer's loaded spools. Independent of transfers, so it runs during one.
        server.route("/filaments", QHttpServerRequest::Method::Post | QHttpServerRequest::Method::Options,
            this, [this](const QHttpServerRequest &request, QHttpServerResponder &responder) {
                route(request, responder, [this](const QJsonObject &body, Reply reply) { startFilaments(body, reply); });
            });

        // Cognito's redirect for a sign-in started by Forward's onboarding (see Account) or in the
        // slicer window (see SlicerWindow::completeSignIn).
        server.route("/callback", QHttpServerRequest::Method::Get, this, [this](const QHttpServerRequest &request) {
            const QUrlQuery callback(request.url());
            if (!account.completeSignIn(callback)) slicer->completeSignIn(callback);
            return QHttpServerResponse("text/html", "<!doctype html><meta charset=utf-8><title>Mirdain Forward</title>"
                "<body style=\"font:16px system-ui;margin:4rem auto;max-width:32rem;text-align:center\">"
                "<h1>Signed in</h1><p>Return to Mirdain Forward to continue. You can close this tab.</p>");
        });

        // The embedded slicer reaches the same handlers through a native bridge instead of HTTP.
        slicer = std::make_unique<SlicerWindow>(slicerUrl(account.organization()),
            [this](const QString &method, const QJsonObject &body, const SlicerWindow::Reply &bridgeReply) {
                const Reply reply = [bridgeReply](Status status, const QJsonObject &result) {
                    bridgeReply(int(status), result);
                };
                if (method == "send") startSend(body, reply);
                else if (method == "usb") startUsb(body, reply);
                else if (method == "filaments") startFilaments(body, reply);
                // The slicer in this window shares Forward's account: it asks for Forward's session
                // instead of signing in itself.
                else if (method == "session") account.session([this, reply](const QString &token, qint64 expiresAt) {
                    if (token.isEmpty() && account.signedIn())
                        reply(Status::BadGateway, {{"error", "Could not reach Mirdain. Check your internet connection."}});
                    else reply(Status::Ok, {{"accessToken", token.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(token)},
                        {"email", token.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(account.email())}, {"expiresAt", expiresAt}});
                });
                else if (method == "signIn") {
                    // Forward finishes it in the browser and then sends mirdainforwardaccount.
                    if (!account.signedIn()) account.beginSignIn();
                    reply(Status::Ok, {});
                } else if (method == "signOut") {
                    account.signOut();
                    reply(Status::Ok, {});
                } else reply(Status::NotFound, {{"error", "Unknown Forward method."}});
            });
        const auto first = menu.actions().first();
        auto *openSlicer = new QAction("Open Slicer", &menu);
        connect(openSlicer, &QAction::triggered, this, [this] { slicer->showSlicer(); });
        menu.insertAction(first, openSlicer);
        auto *addPrinter = new QAction("Add Printer…", &menu);
        connect(addPrinter, &QAction::triggered, this, [this] { slicer->showPage(forwardPage("/add-printer")); });
        menu.insertAction(first, addPrinter);
        organizationAction = new QAction(&menu);
        connect(organizationAction, &QAction::triggered, this, [this] { slicer->showPage(forwardPage("/organization")); });
        menu.insertAction(first, organizationAction);
        menu.insertSeparator(first);
        updateOrganizationAction();
        setup.organizationChanged = [this] {
            updateOrganizationAction();
            slicer->setSlicerUrl(slicerUrl(account.organization()));
        };
        // Signing in happens in the browser; come back to the window after it. The slicer in the
        // window shares the account, so it reloads its session either way.
        account.changed = [this] {
            slicer->accountChanged();
            // Signing out forgets the organization, which unlocks the slicer's.
            slicer->setSlicerUrl(slicerUrl(account.organization()));
            if (account.signedIn()) slicer->show();
        };
        // Models dropped on the panel that appears under the tray icon during a drag go to Gwaith.
        dragWatcher = std::make_unique<DragWatcher>(tray, [this] {
            QList<DragWatcher::Target> targets;
            for (const auto &printer : printerSettings.entries()) targets.append({printer.id, printer.name});
            return targets;
        }, [this](const QStringList &paths, const DragWatcher::Target &target) {
            slicer->sendToGwaith(modelPaths(paths), {{"id", target.printerId}, {"name", target.name}});
        });

        // Launching Forward again while it runs opens the slicer window: a second instance
        // posts /show, with any model paths it was given, before exiting. macOS instead sends
        // the running app a reopen event, or a file open event for models.
        server.route("/show", QHttpServerRequest::Method::Post, this, [this](const QHttpServerRequest &request) {
            if (request.headers().contains("Origin"))
                return QHttpServerResponse(Status::Forbidden);
            QStringList paths;
            if (!request.body().isEmpty())
                for (const auto &path : parseObject(request.body()).value("paths").toArray()) paths.append(path.toString());
            if (paths.isEmpty()) slicer->show();
            else slicer->openFiles(modelPaths(paths));
            return QHttpServerResponse(Status::NoContent);
        });
        qApp->installEventFilter(this);
    }

    void openModels(const QStringList &paths) { slicer->openFiles(paths); }

    bool listen()
    {
        auto *listener = new QTcpServer(&server);
        return listener->listen(QHostAddress::LocalHost, ForwardPort) && server.bind(listener);
    }

    // Forward is already listening, so its pages load right away.
    void start()
    {
        if (setup.onboardingComplete()) slicer->showSlicer();
        else slicer->showPage(forwardPage());
        // Keep the menu's printers in step with printers added elsewhere, such as in the slicer.
        const auto organization = account.organization();
        if (account.signedIn() && !organization.isEmpty()) setup.switchOrganization(organization, [](const QString &) {});
        updater.start();
        // Installed once the app has finished launching, so AppKit's own handler doesn't replace it.
        onApplicationReopen([this] { slicer->show(); });
    }

private:
    QHttpServer server;
    QNetworkAccessManager network;
    QMenu menu;
    AppConfig config;
    PrinterSettings printerSettings;
    QSystemTrayIcon tray;
    NotificationSender notifications{tray};
    AppUpdater updater{menu};
    AppPreferences preferences{config, notifications, updater, menu, [this] { slicer->showPage(forwardPage("/preferences")); }};
    Account account{config, network};
    SetupServer setup{server, config, preferences, account, printerSettings, SlicerUrl};
    QAction *status;
    QAction *organizationAction;
    std::shared_ptr<Job> active;
    std::unique_ptr<SlicerWindow> slicer;
    std::unique_ptr<DragWatcher> dragWatcher;

    void updateOrganizationAction()
    {
        const auto name = account.organization().value("name").toString();
        organizationAction->setText(name.isEmpty() ? "Choose Organization…" : "Organization: " + name + "…");
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == qApp && event->type() == QEvent::FileOpen) {
            openModels(modelPaths({static_cast<QFileOpenEvent *>(event)->file()}));
            return true;
        }
        return QObject::eventFilter(watched, event);
    }

    void route(const QHttpServerRequest &request, QHttpServerResponder &responder,
        const std::function<void(const QJsonObject &, Reply)> &start)
    {
        const auto origin = request.headers().value("Origin").toByteArray();
        if (!allowedOrigin(origin)) {
            responder.sendResponse(response(origin, Status::Forbidden, {{"error", "Origin not allowed."}}));
            return;
        }
        if (request.method() == QHttpServerRequest::Method::Options) {
            responder.sendResponse(response(origin, Status::NoContent, {}));
            return;
        }
        if (request.body().size() > 64 * 1024 || !request.headers().value("Content-Type").startsWith("application/json")) {
            responder.sendResponse(response(origin, Status::BadRequest, {{"error", "Send application/json, up to 64 KiB."}}));
            return;
        }
        auto shared = std::make_shared<QHttpServerResponder>(std::move(responder));
        const Reply reply = [shared, origin](Status status, const QJsonObject &body) {
            shared->sendResponse(response(origin, status, body));
        };
        try {
            start(parseObject(request.body()), reply);
        } catch (const std::exception &error) {
            reply(Status::BadRequest, {{"error", error.what()}});
        }
    }

    void startSend(const QJsonObject &body, const Reply &reply)
    {
        if (active) {
            reply(Status::Conflict, {{"error", "A transfer is already running."}});
            return;
        }
        try {
            const auto connection = body.value("connection").toObject();
            const QUrl url(body.value("url").toString());
            if (!url.isValid() || url.host().isEmpty() || (url.scheme() != "http" && url.scheme() != "https"))
                throw std::runtime_error("url must be an HTTP(S) download link to a .mirdain archive.");
            auto *sender = printerSettings.remember(body);
            auto job = std::make_shared<Job>(reply);
            job->connection = connection;
            job->downloadAuthorization = body.value("download_headers").toObject().value("Authorization").toString().toUtf8();
            job->sender = sender;
            connect(sender, &PrinterSender::progress, this, [this](const QString &message) {
                status->setText(message);
            });
            connect(sender, &PrinterSender::completed, this, [this, job] {
                job->doneMessage = "Print requested " + job->filename;
                job->result = {{"ok", true}, {"print_requested", true}, {"filename", job->filename}, {"plate", 0}};
                finish(job);
            });
            connect(sender, &PrinterSender::failed, this, [this, job](const QString &error) { finish(job, error); });
            job->sliceId = body.value("slice_id").toString();
            if (!updater.beginTransfer()) {
                job->sender->disconnect(this);
                reply(Status::Conflict, {{"error", "Forward is restarting to install an update."}});
                return;
            }
            active = job;
            printerSettings.setBusy(true);
            if (preferences.notificationsEnabled())
                notifications.send("Print request received", "Preparing Plate 1 for " + sender->name() + ".");
            download(job, url, [this, job] { sendPlate(job); });
        } catch (const std::exception &error) {
            reply(Status::BadRequest, {{"error", error.what()}});
        }
    }

    void startUsb(const QJsonObject &body, const Reply &reply)
    {
        if (active) {
            reply(Status::Conflict, {{"error", "A transfer is already running."}});
            return;
        }
        const QUrl url(body.value("url").toString());
        if (!url.isValid() || url.host().isEmpty() || (url.scheme() != "http" && url.scheme() != "https")) {
            reply(Status::BadRequest, {{"error", "url must be an HTTP(S) download link to a plate file."}});
            return;
        }
        const auto drives = removableDrives();
        if (drives.isEmpty()) {
            reply(Status::NotFound, {{"error", "No USB drive is connected."}, {"code", "no_usb"}});
            return;
        }
        auto job = std::make_shared<Job>(reply);
        job->downloadAuthorization = body.value("download_headers").toObject().value("Authorization").toString().toUtf8();
        job->drive = drives.first();
        if (!updater.beginTransfer()) {
            reply(Status::Conflict, {{"error", "Forward is restarting to install an update."}});
            return;
        }
        active = job;
        printerSettings.setBusy(true);
        download(job, url, [this, job] { saveToDrive(job); });
    }

    void startFilaments(const QJsonObject &body, const Reply &reply)
    {
        const auto connection = body.value("connection").toObject();
        if (connection.value("protocol").toString() != "bambu_lan") {
            reply(Status::BadRequest, {{"error", "Loaded filaments can only be read from Bambu LAN printers."},
                {"code", "unsupported"}});
            return;
        }
        BambuFilaments *query;
        try {
            query = new BambuFilaments(connection, body.value("printer_model").toString());
        } catch (const std::exception &error) {
            reply(Status::BadRequest, {{"error", error.what()}});
            return;
        }
        connect(query, &BambuFilaments::completed, this, [query, reply](const QJsonArray &units) {
            reply(Status::Ok, {{"ok", true}, {"filaments", units}});
            query->deleteLater();
        });
        connect(query, &BambuFilaments::failed, this, [query, reply](const QString &error) {
            reply(Status::BadGateway, {{"error", error}});
            query->deleteLater();
        });
        query->start();
    }

    void finish(std::shared_ptr<Job> job, const QString &error = {})
    {
        if (job->sender) job->sender->disconnect(this);
        const auto message = error.isEmpty() ? job->doneMessage : error;
        status->setText(QFontMetrics(menu.font()).elidedText(message, Qt::ElideMiddle, 360));
        tray.setToolTip("Mirdain Forward - " + message);
        job->reply(error.isEmpty() ? Status::Ok : Status::BadGateway,
            error.isEmpty() ? job->result : QJsonObject{{"error", error}});
        active.reset();
        printerSettings.setBusy(false);
        updater.endTransfer();
    }

    void download(const std::shared_ptr<Job> &job, const QUrl &url, std::function<void()> downloaded)
    {
        if (!job->archive.open()) {
            finish(job, job->archive.errorString());
            return;
        }
        status->setText("Downloading…");
        QNetworkRequest request(url);
        request.setTransferTimeout(180000);
        if (!job->downloadAuthorization.isEmpty()) {
            request.setRawHeader("Authorization", job->downloadAuthorization);
            request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
        }
        auto *reply = network.get(request);
        connect(reply, &QNetworkReply::readyRead, this, [job, reply] { job->archive.write(reply->readAll()); });
        connect(reply, &QNetworkReply::finished, this, [this, job, reply, downloaded] {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                finish(job, "Download failed: " + reply->errorString());
                return;
            }
            job->archive.write(reply->readAll());
            if (!job->archive.flush() || job->archive.error() != QFileDevice::NoError) {
                finish(job, job->archive.errorString());
                return;
            }
            // The slicer names plate downloads with an RFC 5987 Content-Disposition filename.
            const auto disposition = QString::fromUtf8(reply->rawHeader("Content-Disposition"));
            const auto encoded = disposition.section("filename*=utf-8''", 1, 1, QString::SectionCaseInsensitiveSeps).section(';', 0, 0);
            job->filename = QUrl::fromPercentEncoding(encoded.toUtf8());
            downloaded();
        });
    }

    void saveToDrive(const std::shared_ptr<Job> &job)
    {
        job->drive.refresh();
        if (!job->drive.isValid() || !job->drive.isReady()) {
            finish(job, "The USB drive was disconnected.");
            return;
        }
        job->filename.replace(QRegularExpression("[^A-Za-z0-9._-]"), "_");
        if (job->filename.isEmpty()) {
            finish(job, "The plate download had no filename.");
            return;
        }
        const QDir root(job->drive.rootPath());
        const QFileInfo name(job->filename);
        auto path = root.filePath(job->filename);
        for (int copy = 2; QFileInfo::exists(path); ++copy)
            path = root.filePath(QString("%1-%2.%3").arg(name.baseName()).arg(copy).arg(name.completeSuffix()));
        status->setText("Saving to " + job->drive.displayName() + "…");
        if (!QFile::copy(job->archive.fileName(), path)) {
            finish(job, "Could not save to " + job->drive.displayName() + ". Check that the drive is writable and has free space.");
            return;
        }
        job->filename = QFileInfo(path).fileName();
        const auto driveName = job->drive.displayName();
        status->setText("Ejecting " + driveName + "…");
        // The file is saved either way; an eject failure is reported, not treated as a failed save.
        ejectDrive(job->drive, this, [this, job, path, driveName](const QString &ejectError) {
            job->doneMessage = "Saved " + job->filename + " to " + driveName
                + (ejectError.isEmpty() ? ". Safe to remove." : ", but it could not be ejected.");
            job->result = {{"ok", true}, {"filename", job->filename}, {"drive", driveName}, {"path", path},
                {"ejected", ejectError.isEmpty()}};
            if (!ejectError.isEmpty()) job->result.insert("eject_error", ejectError);
            if (preferences.notificationsEnabled()) notifications.send("Saved to USB", job->doneMessage);
            finish(job);
        });
    }

    void sendPlate(const std::shared_ptr<Job> &job)
    {
        try {
            ZipArchive archive(job->archive);
            const auto manifest = parseObject(archive.read("manifest.json"));
            const auto slices = manifest.value("slices").toArray();
            if (manifest.value("format").toString() != "mirdain" || slices.isEmpty())
                throw std::runtime_error("The archive has no Mirdain slices.");
            QJsonObject slice = slices.first().toObject();
            if (!job->sliceId.isEmpty()) {
                slice = {};
                for (const auto &entry : slices)
                    if (entry.toObject().value("id").toString() == job->sliceId) slice = entry.toObject();
            }
            const auto member = slice.contains("plate_paths")
                ? slice.value("plate_paths").toObject().value("0").toString()
                : slice.value("path").toString();
            if (slice.value("format").toString() != "gcode.3mf" || !member.endsWith(".gcode.3mf"))
                throw std::runtime_error("The selected slice has no .gcode.3mf output for plate 0.");
            // Each native per-plate package contains one plain .gcode file.
            auto package = archive.read(member);
            QBuffer plate(&package);
            plate.open(QIODevice::ReadOnly);
            ZipArchive nativePlate(plate);
            const bool native = job->sender->fileFormat() == PrinterFile::Format::Gcode3mf;
            const auto gcodePath = plateGcodePath(nativePlate, native);
            auto gcode = nativePlate.read(gcodePath);
            if (gcode.isEmpty()) throw std::runtime_error("The sliced plate is empty.");
            job->filename = QFileInfo(member).fileName();
            if (!native) job->filename.chop(4); // .gcode.3mf -> .gcode
            job->filename.replace(QRegularExpression("[^A-Za-z0-9._-]"), "_");
            const auto md5 = QCryptographicHash::hash(gcode, QCryptographicHash::Md5).toHex();
            job->sender->send({native ? nativePlate.withoutDirectory("Objects") : std::move(gcode), job->filename,
                slice.value("bed_type").toString(), native ? gcodePath : QString{}, md5}, job->connection);
        } catch (const std::exception &error) {
            finish(job, QString::fromUtf8(error.what()));
        }
    }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("Mirdain Forward");
    QApplication::setApplicationVersion(FORWARD_APP_VERSION);
    QApplication::setQuitOnLastWindowClosed(false);
    try {
#ifdef Q_OS_WIN
        // Keep this handle until process exit so Inno Setup cannot replace a running app.
        if (!CreateMutexW(nullptr, FALSE, L"MirdainForwardRunning"))
            throw std::runtime_error("Cannot create the installer mutex.");
#endif
        const auto directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        if (!QDir().mkpath(directory))
            throw std::runtime_error(("Cannot create app directory: " + directory).toStdString());
        QLockFile lock(QDir(directory).filePath("forward.lock"));
        lock.setStaleLockTime(0);
        if (!lock.tryLock(0)) {
            if (lock.error() == QLockFile::LockFailedError) {
                // Forward is already running: ask it to open the slicer window and any models.
                QNetworkAccessManager network;
                QNetworkRequest request(QUrl(QString("http://127.0.0.1:%1/show").arg(ForwardPort)));
                request.setTransferTimeout(3000);
                request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
                const auto paths = QJsonArray::fromStringList(modelPaths(app.arguments().mid(1)));
                auto *reply = network.post(request, QJsonDocument(QJsonObject{{"paths", paths}}).toJson(QJsonDocument::Compact));
                QObject::connect(reply, &QNetworkReply::finished, &app, &QApplication::quit);
                return app.exec();
            }
            throw std::runtime_error("Cannot lock Mirdain Forward's app directory.");
        }
        Forwarder forwarder;
        if (!forwarder.listen()) {
            QMessageBox::critical(nullptr, "Mirdain Forward", "Cannot listen on 127.0.0.1:18744. Is Mirdain Forward already running?");
            return 1;
        }
        QTimer::singleShot(0, &forwarder, &Forwarder::start);
        // Windows passes a model opened from Explorer on the command line.
        const auto models = modelPaths(app.arguments().mid(1));
        QTimer::singleShot(0, &forwarder, [&forwarder, models] { forwarder.openModels(models); });
        return app.exec();
    } catch (const std::exception &error) {
        QMessageBox::critical(nullptr, "Mirdain Forward", QString::fromUtf8(error.what()));
        return 1;
    }
}
