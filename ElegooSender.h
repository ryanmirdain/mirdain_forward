#pragma once

#include "PrinterSendJob.h"

#include <QNetworkAccessManager>
#include <QTimer>
#include <QUdpSocket>
#include <QUrl>
#include <QWebSocket>

class ElegooSender : public PrinterSender {
public:
    QString protocol() const override { return "elegoo_sdcp"; }
    bool supportsAutoLevelBed() const override { return true; }

protected:
    int defaultPort() const override { return 3030; }
    std::unique_ptr<PrinterSendJob> createJob(const QJsonObject &connection) const override;
};

class ElegooSendJob : public PrinterSendJob {
public:
    explicit ElegooSendJob(const QJsonObject &connection, bool autoLevelBed);
    void send(PrinterFile file) override;

private:
    QNetworkAccessManager network;
    QUdpSocket discovery;
    QWebSocket socket;
    QTimer confirmationTimeout;
    QUrl uploadUrl;
    PrinterFile file;
    QByteArray md5;
    QByteArray uuid;
    qsizetype offset = 0;
    QString mainboardId;
    QString commandId;
    bool startSent = false;

    void upload();
    void startPrint();
    void connectPrinter();
    void sendCommand(int command, const QJsonObject &parameters = {});
    void finish(const QString &error = {});
};
