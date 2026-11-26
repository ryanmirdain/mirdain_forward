#pragma once

#include "printers/PrinterSendJob.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>

#include <functional>

class QNetworkReply;

// Printers attached to an OctoPrint server, through its REST API.
class OctoPrintSender : public PrinterSender {
public:
    QString protocol() const override { return "octoprint"; }

protected:
    int defaultPort() const override { return 80; }
    std::unique_ptr<PrinterSendJob> createJob(const QJsonObject &connection) const override;
};

class OctoPrintSendJob : public PrinterSendJob {
public:
    explicit OctoPrintSendJob(const QJsonObject &connection, bool autoLevelBed);
    void send(PrinterFile file) override;

private:
    QNetworkAccessManager network;
    QUrl origin;
    QByteArray apiKey;
    PrinterFile file;

    QNetworkRequest request(const QString &path) const;
    void get(const QString &path, std::function<void(int status, const QJsonObject &)> next);
    void checkPrinter();
    void upload();
};
