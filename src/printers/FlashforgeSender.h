#pragma once

#include "printers/PrinterSendJob.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>

class QNetworkReply;

// Flashforge printers with the LAN-mode HTTP API (Adventurer 5M series, AD5X).
class FlashforgeSender : public PrinterSender {
public:
    QString protocol() const override { return "flashforge_lan"; }
    bool supportsAutoLevelBed() const override { return true; }

protected:
    int defaultPort() const override { return 8898; }
    std::unique_ptr<PrinterSendJob> createJob(const QJsonObject &connection) const override;
};

class FlashforgeSendJob : public PrinterSendJob {
public:
    explicit FlashforgeSendJob(const QJsonObject &connection, bool autoLevelBed);
    void send(PrinterFile file) override;

private:
    QNetworkAccessManager network;
    QUrl origin;
    QByteArray serial;
    QByteArray checkCode;
    PrinterFile file;

    QNetworkRequest request(const QString &path) const;
    // Null when the reply failed; failed() has then been emitted.
    QJsonObject result(QNetworkReply *reply, const QString &action);
    void upload();
};
