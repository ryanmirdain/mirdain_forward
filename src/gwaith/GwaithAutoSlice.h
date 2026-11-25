#pragma once

#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <QTimer>

#include <functional>
#include <map>

class Account;
class QNetworkAccessManager;
class QWebSocket;

// One drop of models on a printer, sliced by Gwaith in the background: uploads the models to a
// new Gwaith conversation, asks Gwaith to slice them for the printer, and follows the
// conversation over Gwaith's chat socket. From its events it reports a status and captures the
// slicing session Gwaith creates, so the session and its chat can open in the slicer. Gwaith
// runs on its server; this only watches, and reconnects to catch up if the socket drops.
class GwaithAutoSlice : public QObject {
    Q_OBJECT

public:
    enum class Phase { Uploading, Running, Done, Failed };
    struct Printer { QString id; QString name; };

    // notes are the user's instructions for Gwaith from the drop panel; empty when skipped.
    GwaithAutoSlice(Account &account, QNetworkAccessManager &network, qint64 organization,
        const QStringList &paths, const Printer &printer, const QString &notes, QObject *parent = nullptr);
    void start();

    Phase phase() const { return currentPhase; }
    bool finished() const { return currentPhase == Phase::Done || currentPhase == Phase::Failed; }
    // The model's name, or the count of models ("3 files").
    QString title() const;
    // What Gwaith is doing, or the outcome.
    QString status() const { return currentStatus; }
    qint64 organization() const { return organizationId; }
    QString conversationId() const { return conversation; }
    // The slicing session Gwaith is working in; empty until it has created one, and again once
    // Gwaith disconnects from it.
    QString sessionId() const { return session; }
    // Gwaith ended its session (disconnected from it), so there is none to open.
    bool sessionEnded() const { return ended; }
    // The printer the models were dropped on; id is empty when none was chosen.
    const Printer &target() const { return printer; }
    // The last sliced plate archive (.mirdain) Gwaith attached to the conversation, if any.
    QString archiveName() const { return archive.name; }
    bool hasArchive() const { return !archive.id.isEmpty(); }
    // A download link for that archive, or an error.
    void archiveDownload(std::function<void(const QString &url, const QString &error)> done);

signals:
    void changed();

private:
    Account &account;
    QNetworkAccessManager &network;
    qint64 organizationId;
    QStringList paths;
    Printer printer;
    QString notes;
    QString conversation;
    QString requestId;
    Phase currentPhase = Phase::Uploading;
    QString currentStatus;
    QString session;
    bool ended = false;
    struct { QString id; QString name; } archive;
    QWebSocket *socket = nullptr;
    QTimer ping;
    bool sent = false;
    bool accepted = false;
    int reconnects = 0;
    // Tool names by call ID, to tell which tool produced each result.
    std::map<QString, QString> toolCalls;

    void update(Phase phase, const QString &status);
    void fail(const QString &error);
    void upload(int index);
    void connectSocket();
    void received(const QJsonObject &event);
    void send();
    void readMessage(const QJsonObject &message);
    void catchUp();
    QString prompt() const;
};
