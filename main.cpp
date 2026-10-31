#include "PrinterSender.h"
#include "PrinterSettings.h"
#include "ZipArchive.h"

#include <QApplication>
#include <QBuffer>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerResponder>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSystemTrayIcon>
#include <QTcpServer>
#include <QTemporaryFile>

#include <memory>
#include <stdexcept>

using Status = QHttpServerResponder::StatusCode;
constexpr quint16 Port = 18744;

static bool allowedOrigin(const QByteArray &origin)
{
    return origin.isEmpty() || origin == "https://slicer.mirdainsystems.com"
        || origin == "http://localhost:5173" || origin == "http://127.0.0.1:5173";
}

static QHttpServerResponse response(const QByteArray &origin, Status status, const QJsonObject &body)
{
    QHttpServerResponse result(body, status);
    auto headers = result.headers();
    if (!origin.isEmpty() && allowedOrigin(origin)) {
        headers.append("Access-Control-Allow-Origin", origin);
        headers.append("Access-Control-Allow-Methods", "POST, OPTIONS");
        headers.append("Access-Control-Allow-Headers", "Content-Type");
        headers.append("Vary", "Origin");
    }
    result.setHeaders(headers);
    return result;
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
    explicit Job(QHttpServerResponder &&reply) : responder(std::move(reply)) {}
    QHttpServerResponder responder;
    QByteArray origin;
    QByteArray downloadAuthorization;
    QTemporaryFile archive;
    QString sliceId;
    QString filename;
    QJsonObject connection;
    PrinterSender *sender;
};

class Forwarder : public QObject {
public:
    Forwarder() : printerSettings(*menu.addMenu("Printers"))
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

        server.route("/send", QHttpServerRequest::Method::Post | QHttpServerRequest::Method::Options,
            this, [this](const QHttpServerRequest &request, QHttpServerResponder &responder) {
                const auto origin = request.headers().value("Origin").toByteArray();
                if (!allowedOrigin(origin)) {
                    responder.sendResponse(response(origin, Status::Forbidden, {{"error", "Origin not allowed."}}));
                    return;
                }
                if (request.method() == QHttpServerRequest::Method::Options) {
                    responder.sendResponse(response(origin, Status::NoContent, {}));
                    return;
                }
                if (active) {
                    responder.sendResponse(response(origin, Status::Conflict, {{"error", "A transfer is already running."}}));
                    return;
                }
                if (request.body().size() > 64 * 1024 || !request.headers().value("Content-Type").startsWith("application/json")) {
                    responder.sendResponse(response(origin, Status::BadRequest, {{"error", "Send application/json, up to 64 KiB."}}));
                    return;
                }
                try {
                    const auto body = parseObject(request.body());
                    const auto connection = body.value("connection").toObject();
                    const QUrl url(body.value("url").toString());
                    if (!url.isValid() || url.host().isEmpty() || (url.scheme() != "http" && url.scheme() != "https"))
                        throw std::runtime_error("url must be an HTTP(S) download link to a .mirdain archive.");
                    auto *sender = printerSettings.remember(body);
                    auto job = std::make_shared<Job>(std::move(responder));
                    job->connection = connection;
                    job->origin = origin;
                    job->downloadAuthorization = body.value("download_headers").toObject().value("Authorization").toString().toUtf8();
                    job->sender = sender;
                    connect(sender, &PrinterSender::progress, this, [this](const QString &message) {
                        status->setText(message);
                    });
                    connect(sender, &PrinterSender::completed, this, [this, job] { finish(job); });
                    connect(sender, &PrinterSender::failed, this, [this, job](const QString &error) { finish(job, error); });
                    job->sliceId = body.value("slice_id").toString();
                    active = job;
                    printerSettings.setBusy(true);
                    download(job, url);
                } catch (const std::exception &error) {
                    responder.sendResponse(response(origin, Status::BadRequest, {{"error", error.what()}}));
                }
            });
    }

    bool listen()
    {
        auto *listener = new QTcpServer(&server);
        return listener->listen(QHostAddress::LocalHost, Port) && server.bind(listener);
    }

private:
    QHttpServer server;
    QNetworkAccessManager network;
    QMenu menu;
    PrinterSettings printerSettings;
    QSystemTrayIcon tray;
    QAction *status;
    std::shared_ptr<Job> active;

    void finish(std::shared_ptr<Job> job, const QString &error = {})
    {
        job->sender->disconnect(this);
        const auto message = error.isEmpty() ? "Print requested " + job->filename : error;
        status->setText(QFontMetrics(menu.font()).elidedText(message, Qt::ElideMiddle, 360));
        tray.setToolTip("Mirdain Forward - " + message);
        job->responder.sendResponse(response(job->origin, error.isEmpty() ? Status::Ok : Status::BadGateway,
            error.isEmpty() ? QJsonObject{{"ok", true}, {"print_requested", true}, {"filename", job->filename}, {"plate", 0}}
                            : QJsonObject{{"error", error}}));
        active.reset();
        printerSettings.setBusy(false);
    }

    void download(const std::shared_ptr<Job> &job, const QUrl &url)
    {
        if (!job->archive.open()) {
            finish(job, job->archive.errorString());
            return;
        }
        status->setText("Downloading .mirdain…");
        QNetworkRequest request(url);
        request.setTransferTimeout(180000);
        if (!job->downloadAuthorization.isEmpty()) {
            request.setRawHeader("Authorization", job->downloadAuthorization);
            request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
        }
        auto *reply = network.get(request);
        connect(reply, &QNetworkReply::readyRead, this, [job, reply] { job->archive.write(reply->readAll()); });
        connect(reply, &QNetworkReply::finished, this, [this, job, reply] {
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
                job->sender->send({native ? std::move(package) : std::move(gcode), job->filename,
                    slice.value("bed_type").toString(), native ? gcodePath : QString{}, md5}, job->connection);
            } catch (const std::exception &error) {
                finish(job, QString::fromUtf8(error.what()));
            }
        });
    }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("Mirdain Forward");
    QApplication::setQuitOnLastWindowClosed(false);
    try {
        Forwarder forwarder;
        if (!forwarder.listen()) {
            QMessageBox::critical(nullptr, "Mirdain Forward", "Cannot listen on 127.0.0.1:18744. Is Mirdain Forward already running?");
            return 1;
        }
        return app.exec();
    } catch (const std::exception &error) {
        QMessageBox::critical(nullptr, "Mirdain Forward", QString::fromUtf8(error.what()));
        return 1;
    }
}
