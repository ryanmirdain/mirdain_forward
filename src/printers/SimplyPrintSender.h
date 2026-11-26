#pragma once

#include "printers/PrinterSendJob.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>

#include <functional>

class QNetworkReply;

// Printers linked to a SimplyPrint account, through SimplyPrint's cloud API.
class SimplyPrintSender : public PrinterSender {
public:
    QString protocol() const override { return "simplyprint"; }

protected:
    int defaultPort() const override { return 443; }
    std::unique_ptr<PrinterSendJob> createJob(const QJsonObject &connection) const override;
};

class SimplyPrintSendJob : public PrinterSendJob {
public:
    explicit SimplyPrintSendJob(const QJsonObject &connection, bool autoLevelBed);
    void send(PrinterFile file) override;

private:
    QNetworkAccessManager network;
    QUrl api;
    QUrl files;
    QByteArray apiKey;
    QString printerId;
    PrinterFile file;

    QNetworkRequest request(QUrl base, const QString &path, const QString &query = {}) const;
    // Calls next with the JSON body when the request succeeds and SimplyPrint reports status true.
    void call(QNetworkReply *reply, const QString &action, std::function<void(const QJsonObject &)> next);
    void upload();
    void createJob(const QString &fileId);
};
