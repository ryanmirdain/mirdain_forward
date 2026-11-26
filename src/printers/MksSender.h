#pragma once

#include "printers/PrinterSendJob.h"

#include <QNetworkAccessManager>
#include <QStringList>
#include <QTcpSocket>
#include <QTimer>

#include <functional>

// Printers with an MKS Robin Wi-Fi module: HTTP upload to the SD card and a TCP G-code console.
class MksSender : public PrinterSender {
public:
    QString protocol() const override { return "mks_wifi"; }

protected:
    int defaultPort() const override { return 80; }
    std::unique_ptr<PrinterSendJob> createJob(const QJsonObject &connection) const override;
};

class MksSendJob : public PrinterSendJob {
public:
    explicit MksSendJob(const QJsonObject &connection, bool autoLevelBed);
    void send(PrinterFile file) override;

private:
    QNetworkAccessManager network;
    QTcpSocket console;
    QTimer consoleTimeout;
    QUrl origin;
    QUrl consoleUrl;
    PrinterFile file;
    QByteArray received;
    QStringList replyLines;
    QStringList pending;
    std::function<void(const QStringList &)> afterCommands;

    // Sends each G-code line, waiting for its "ok", then calls next with every reply line.
    void runCommands(const QStringList &commands, std::function<void(const QStringList &)> next);
    void sendNextCommand();
    void upload();
};
