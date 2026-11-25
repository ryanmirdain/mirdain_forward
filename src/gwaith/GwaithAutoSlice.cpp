#include "gwaith/GwaithAutoSlice.h"
#include "app/Account.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QUuid>
#include <QWebSocket>

static const QString GwaithUrl = FORWARD_GWAITH_URL;
// Gwaith's socket admits the Mirdain sites' origins; Forward hosts the slicer.
static const QString SocketOrigin = "https://slicer.mirdainsystems.com";
static constexpr int MaxReconnects = 20;

static QString uuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

// What Gwaith is doing, from the tool it called last.
static QString toolStatus(const QString &tool)
{
    static const std::map<QString, QString> labels{
        {"slicer_create_session", "Creating a slicing session"}, {"slicer_get_session", "Reading the session"},
        {"slicer_import_file", "Importing models"}, {"slicer_select_printer", "Selecting the printer"},
        {"slicer_auto_layout", "Arranging the plate"}, {"slicer_update_object", "Positioning parts"},
        {"slicer_get_preset", "Checking settings"}, {"slicer_update_preset", "Adjusting settings"},
        {"slicer_update_filaments", "Choosing filaments"}, {"slicer_get_image", "Reviewing the plate"},
        {"slicer_slice", "Slicing"}, {"slicer_get_slice", "Checking the slice"}, {"slicer_export", "Exporting"},
    };
    const auto label = labels.find(tool);
    return label == labels.end() ? "Working" : label->second;
}

GwaithAutoSlice::GwaithAutoSlice(Account &account, QNetworkAccessManager &network, qint64 organization,
    const QStringList &paths, const Printer &printer, const QString &notes, QObject *parent)
    : QObject(parent), account(account), network(network), organizationId(organization), paths(paths), printer(printer),
      notes(notes),
      conversation(uuid()), requestId(uuid())
{
    ping.setInterval(30000);
    connect(&ping, &QTimer::timeout, this, [this] {
        if (socket) socket->sendTextMessage(R"({"type":"ping"})");
    });
}

QString GwaithAutoSlice::title() const
{
    return paths.size() == 1 ? QFileInfo(paths.first()).fileName() : QString("%1 files").arg(paths.size());
}

void GwaithAutoSlice::start()
{
    update(Phase::Uploading, "Uploading models");
    upload(0);
}

void GwaithAutoSlice::update(Phase phase, const QString &status)
{
    currentPhase = phase;
    currentStatus = status;
    emit changed();
}

void GwaithAutoSlice::fail(const QString &error)
{
    ping.stop();
    if (socket) {
        socket->disconnect(this);
        socket->close();
        socket->deleteLater();
        socket = nullptr;
    }
    update(Phase::Failed, error);
}

// Each model becomes an attachment in the conversation: reserve, upload, then confirm.
void GwaithAutoSlice::upload(int index)
{
    if (index == paths.size()) {
        connectSocket();
        return;
    }
    account.session([this, index](const QString &token, qint64) {
        if (token.isEmpty()) {
            fail("Sign in to Mirdain Forward to slice with Gwaith.");
            return;
        }
        const QFileInfo file(paths[index]);
        const auto base = QString("%1/api/v1/organizations/%2/conversations/%3/files").arg(GwaithUrl).arg(organizationId).arg(conversation);
        QNetworkRequest reserve{QUrl(base)};
        reserve.setRawHeader("Authorization", "Bearer " + token.toUtf8());
        reserve.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        const auto type = file.suffix().toLower() == "3mf" ? "model/3mf" : "model/stl";
        auto *reply = network.post(reserve, QJsonDocument(QJsonObject{{"name", file.fileName()}, {"content_type", type},
            {"size_bytes", file.size()}}).toJson(QJsonDocument::Compact));
        connect(reply, &QNetworkReply::finished, this, [this, reply, index, token, base, file] {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                fail("Gwaith could not take the models: " + reply->errorString());
                return;
            }
            const auto reservation = QJsonDocument::fromJson(reply->readAll()).object();
            QNetworkRequest put{QUrl(reservation.value("upload_url").toString())};
            const auto headers = reservation.value("upload_headers").toObject();
            for (auto header = headers.begin(); header != headers.end(); ++header)
                put.setRawHeader(header.key().toUtf8(), header.value().toString().toUtf8());
            auto *source = new QFile(file.absoluteFilePath());
            if (!source->open(QIODevice::ReadOnly)) {
                delete source;
                fail("Could not read " + file.fileName() + ".");
                return;
            }
            auto *uploaded = network.put(put, source);
            source->setParent(uploaded);
            connect(uploaded, &QNetworkReply::finished, this, [this, uploaded, index, token, base, reservation, file] {
                uploaded->deleteLater();
                if (uploaded->error() != QNetworkReply::NoError) {
                    fail("Uploading " + file.fileName() + " failed: " + uploaded->errorString());
                    return;
                }
                QNetworkRequest complete{QUrl(base + "/" + reservation.value("part_id").toString() + "/complete")};
                complete.setRawHeader("Authorization", "Bearer " + token.toUtf8());
                auto *confirmed = network.post(complete, QByteArray());
                connect(confirmed, &QNetworkReply::finished, this, [this, confirmed, index] {
                    confirmed->deleteLater();
                    if (confirmed->error() != QNetworkReply::NoError) {
                        fail("Gwaith could not take the models: " + confirmed->errorString());
                        return;
                    }
                    upload(index + 1);
                });
            });
        });
    });
}

void GwaithAutoSlice::archiveDownload(std::function<void(const QString &url, const QString &error)> done)
{
    account.session([this, done](const QString &token, qint64) {
        if (token.isEmpty()) {
            done({}, "Sign in to Mirdain Forward to send the plate.");
            return;
        }
        QNetworkRequest request{QUrl(QString("%1/api/v1/organizations/%2/conversations/%3/files/%4/download")
            .arg(GwaithUrl).arg(organizationId).arg(conversation, archive.id))};
        request.setRawHeader("Authorization", "Bearer " + token.toUtf8());
        auto *reply = network.get(request);
        connect(reply, &QNetworkReply::finished, this, [reply, done] {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                done({}, "Couldn’t download Gwaith’s plate: " + reply->errorString());
                return;
            }
            done(QJsonDocument::fromJson(reply->readAll()).object().value("download_url").toString(), {});
        });
    });
}

// The request, as plain text: the user isn't watching, so Gwaith works through it on its own
// and leaves Forward a plate to send.
QString GwaithAutoSlice::prompt() const
{
    QStringList names;
    for (const auto &path : paths) names.append(QFileInfo(path).fileName());
    const auto target = printer.name.isEmpty() ? QString("my printer")
        : QString("my printer \"%1\"%2").arg(printer.name, printer.id.isEmpty() ? QString() : " (custom printer ID " + printer.id + ")");
    auto request = "Slice " + names.join(", ") + " for " + target + ".\n\n"
        "I dropped these models on the printer in Mirdain Forward, my desktop app, which runs this in the background, so "
        "no slicing session is open yet: create one. Keep questions to a minimum and make reasonable choices yourself; "
        "ask only when you can't continue without me. Before you finish, export the sliced plate as a .mirdain with "
        "slicer_export, which Forward sends to the printer. Leave the slicing session open so I can review it in the slicer.";
    if (!notes.isEmpty()) request += "\n\nMy notes: " + notes;
    return request;
}

void GwaithAutoSlice::connectSocket()
{
    account.session([this](const QString &token, qint64) {
        if (token.isEmpty()) {
            fail("Sign in to Mirdain Forward to slice with Gwaith.");
            return;
        }
        if (currentPhase == Phase::Uploading) update(Phase::Running, "Starting Gwaith");
        socket = new QWebSocket(SocketOrigin, QWebSocketProtocol::VersionLatest, this);
        QUrl url(QString("%1/api/v1/organizations/%2/socket").arg(GwaithUrl).arg(organizationId));
        url.setScheme(url.scheme() == "https" ? "wss" : "ws");
        connect(socket, &QWebSocket::connected, this, [this, token] {
            socket->sendTextMessage(QJsonDocument(QJsonObject{{"type", "authenticate"}, {"access_token", token}}).toJson(QJsonDocument::Compact));
        });
        connect(socket, &QWebSocket::textMessageReceived, this, [this](const QString &text) {
            received(QJsonDocument::fromJson(text.toUtf8()).object());
        });
        // Gwaith keeps working without the socket, which also closes when the token expires:
        // reconnect with a fresh one and catch up from the conversation's history.
        connect(socket, &QWebSocket::disconnected, this, [this] {
            ping.stop();
            socket->deleteLater();
            socket = nullptr;
            if (finished()) return;
            if (++reconnects > MaxReconnects) {
                fail("Lost the connection to Gwaith.");
                return;
            }
            QTimer::singleShot(3000, this, [this] { if (!finished() && !socket) connectSocket(); });
        });
        socket->open(url);
    });
}

void GwaithAutoSlice::received(const QJsonObject &event)
{
    const auto type = event.value("type").toString();
    if (type == "authenticated") {
        ping.start();
        if (sent) {
            catchUp();
            return;
        }
        sent = true;
        send();
        return;
    }
    // The socket carries every conversation of this account in the organization.
    if (event.contains("conversation_id") && event.value("conversation_id").toString() != conversation) return;
    if (type == "error") {
        fail("Gwaith: " + event.value("error").toString());
        return;
    }
    if (type == "accepted" && currentStatus == "Starting Gwaith") update(Phase::Running, "Reading the models");
    if (type == "accepted" || type == "message") {
        if (event.contains("message")) readMessage(event.value("message").toObject());
        for (const auto &message : event.value("messages").toArray()) readMessage(message.toObject());
    }
    if (type == "completed") {
        const auto message = event.value("message").toObject();
        if (!event.value("error").isNull() && !event.value("error").isUndefined()) {
            fail("Gwaith stopped: " + event.value("error").toVariant().toString());
            return;
        }
        ping.stop();
        socket->close();
        update(message.value("status").toString() == "failed" ? Phase::Failed : Phase::Done,
            message.value("status").toString() == "failed" ? "Gwaith could not finish" : "Done");
    }
}

void GwaithAutoSlice::send()
{
    socket->sendTextMessage(QJsonDocument(QJsonObject{{"type", "send"}, {"conversation_id", conversation},
        {"request_id", requestId}, {"content", prompt()}}).toJson(QJsonDocument::Compact));
}

// Tool calls give the status; slicer_create_session's result gives the session.
void GwaithAutoSlice::readMessage(const QJsonObject &message)
{
    // The request's own message has its ID.
    if (message.value("id").toString() == requestId) accepted = true;
    const auto type = message.value("type").toString();
    const auto content = message.value("content").toObject();
    if (type == "gwaith") {
        QString latest;
        for (const auto &value : content.value("parts").toArray()) {
            const auto part = value.toObject();
            // Gwaith attaches what it exports, such as a sliced plate archive, once uploaded.
            if (part.value("type").toString() == "file" && part.value("state").toString() == "ready"
                && part.value("name").toString().endsWith(".mirdain", Qt::CaseInsensitive)) {
                archive = {part.value("file_id").toString(), part.value("name").toString()};
                continue;
            }
            if (part.value("type").toString() != "tool_call") continue;
            toolCalls[part.value("call_id").toString()] = part.value("name").toString();
            latest = part.value("name").toString();
        }
        if (!latest.isEmpty() && !finished()) update(Phase::Running, toolStatus(latest));
        return;
    }
    if (type != "tool_response") return;
    const auto tool = toolCalls[content.value("call_id").toString()];
    // Once Gwaith disconnects from its session, the run no longer refers to it.
    if (tool == "disconnect_from_slicing_session") {
        if (!session.isEmpty()) {
            session.clear();
            ended = true;
            emit changed();
        }
        return;
    }
    if (tool != "slicer_create_session" && !session.isEmpty()) return;
    for (const auto &value : content.value("parts").toArray()) {
        const auto id = value.toObject().value("value").toObject().value("session_id").toString();
        if (!id.isEmpty() && id != session) {
            session = id;
            ended = false;
            emit changed();
        }
    }
}

// After a reconnect: replay the conversation's history, which also shows whether Gwaith
// finished while the socket was down.
void GwaithAutoSlice::catchUp()
{
    account.session([this](const QString &token, qint64) {
        if (token.isEmpty()) return;
        QNetworkRequest request{QUrl(QString("%1/api/v1/organizations/%2/conversations/%3/messages?after=0&limit=200")
            .arg(GwaithUrl).arg(organizationId).arg(conversation))};
        request.setRawHeader("Authorization", "Bearer " + token.toUtf8());
        auto *reply = network.get(request);
        connect(reply, &QNetworkReply::finished, this, [this, reply] {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError || finished()) return;
            QJsonObject last;
            for (const auto &value : QJsonDocument::fromJson(reply->readAll()).array()) {
                const auto message = value.toObject();
                readMessage(message);
                if (message.value("type").toString() == "gwaith") last = message;
            }
            // The request never reached Gwaith before the socket dropped.
            if (!accepted && socket) {
                send();
                return;
            }
            const auto status = last.value("status").toString();
            if (last.isEmpty() || status == "running") return;
            ping.stop();
            if (socket) socket->close();
            update(status == "completed" ? Phase::Done : Phase::Failed, status == "completed" ? "Done" : "Gwaith could not finish");
        });
    });
}
