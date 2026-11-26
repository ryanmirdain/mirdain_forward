#pragma once

#include "printers/PrinterSendJob.h"

#include <QNetworkAccessManager>
#include <QTimer>
#include <QWebSocket>

// Creality K1 and K2 family printers on stock firmware: HTTP upload and a WebSocket
// control channel, as used by Creality Print.
class CrealitySender : public PrinterSender {
public:
    QString protocol() const override { return "creality_lan"; }

protected:
    int defaultPort() const override { return 80; }
    std::unique_ptr<PrinterSendJob> createJob(const QJsonObject &connection) const override;
};

class CrealitySendJob : public PrinterSendJob {
public:
    explicit CrealitySendJob(const QJsonObject &connection, bool autoLevelBed);
    void send(PrinterFile file) override;

private:
    enum class Phase { CheckIdle, Uploading, Starting };
    QNetworkAccessManager network;
    QWebSocket socket;
    QTimer timeout;
    QUrl origin;
    QUrl control;
    QString gcodeRoot;
    PrinterFile file;
    Phase phase = Phase::CheckIdle;
    bool startSent = false;
    bool done = false;

    void readModel();
    void openControl(Phase next);
    void receive(const QString &text);
    void upload();
    void finish(const QString &error = {});
};
