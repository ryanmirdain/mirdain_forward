#pragma once

#include "printers/PrinterSendJob.h"

#include <QStringList>
#include <QTcpSocket>
#include <QTimer>

#include <functional>

// Older Flashforge printers (Adventurer 3/4, Guider, Creator, Artemis) through the
// FlashPrint G-code console on TCP port 8899.
class FlashforgeLegacySender : public PrinterSender {
public:
    QString protocol() const override { return "flashforge_legacy"; }

protected:
    int defaultPort() const override { return 8899; }
    std::unique_ptr<PrinterSendJob> createJob(const QJsonObject &connection) const override;
};

class FlashforgeLegacySendJob : public PrinterSendJob {
public:
    explicit FlashforgeLegacySendJob(const QJsonObject &connection, bool autoLevelBed);
    void send(PrinterFile file) override;

private:
    QTcpSocket socket;
    QTimer timeout;
    QString host;
    int port = 8899;
    PrinterFile file;
    QByteArray received;
    QStringList replyLines;
    std::function<void(const QStringList &)> afterReply;
    qint64 dataSent = 0;

    // Sends one ~-prefixed command and calls next with its reply lines once "ok" arrives.
    void command(const QString &text, std::function<void(const QStringList &)> next);
    void sendData();
    void finish(const QString &error = {});
    bool done = false;
};
