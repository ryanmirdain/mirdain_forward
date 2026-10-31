#pragma once

#include "PrinterSender.h"

#include <QNetworkAccessManager>
#include <QTimer>
#include <QUdpSocket>
#include <QUrl>
#include <QWebSocket>

class ElegooSender : public PrinterSender {
public:
    explicit ElegooSender(const QJsonObject &connection);
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
