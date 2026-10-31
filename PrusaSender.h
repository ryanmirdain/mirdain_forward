#pragma once

#include "PrinterSendJob.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#include <functional>

class PrusaSender : public PrinterSender {
public:
    QString protocol() const override { return "prusalink"; }

protected:
    int defaultPort() const override { return 80; }
    std::unique_ptr<PrinterSendJob> createJob(const QJsonObject &connection) const override;
};

class PrusaSendJob : public PrinterSendJob {
public:
    explicit PrusaSendJob(const QJsonObject &connection, bool autoLevelBed);
    void send(PrinterFile file) override;

private:
    QNetworkAccessManager network;
    QUrl origin;
    QByteArray apiKey;
    QString storage;
    PrinterFile file;
    bool modern = false;

    QNetworkRequest request(const QString &path) const;
    bool accepted(QNetworkReply *reply);
    void get(const QString &path, std::function<void(const QJsonObject &)> next);
    void checkPrinter();
    void chooseStorage();
    void upload();
};
