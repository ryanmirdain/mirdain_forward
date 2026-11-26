#pragma once

#include "printers/PrinterSendJob.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>

#include <functional>

class QNetworkReply;

// Klipper printers through Moonraker's HTTP API.
class MoonrakerSender : public PrinterSender {
public:
    QString protocol() const override { return "moonraker"; }

protected:
    int defaultPort() const override { return 7125; }
    std::unique_ptr<PrinterSendJob> createJob(const QJsonObject &connection) const override;
};

class MoonrakerSendJob : public PrinterSendJob {
public:
    explicit MoonrakerSendJob(const QJsonObject &connection, bool autoLevelBed);
    void send(PrinterFile file) override;

private:
    QNetworkAccessManager network;
    QUrl origin;
    QByteArray apiKey;
    PrinterFile file;

    QNetworkRequest request(const QString &path, const QString &query = {}) const;
    void get(const QString &path, const QString &query, std::function<void(const QJsonObject &)> next);
    void checkPrinter();
    void upload();
};
