#pragma once

#include "printers/PrinterSendJob.h"

#include <QNetworkAccessManager>

// Printers attached to a Repetier-Server, through its HTTP API.
class RepetierSender : public PrinterSender {
public:
    QString protocol() const override { return "repetier"; }

protected:
    int defaultPort() const override { return 3344; }
    std::unique_ptr<PrinterSendJob> createJob(const QJsonObject &connection) const override;
};

class RepetierSendJob : public PrinterSendJob {
public:
    explicit RepetierSendJob(const QJsonObject &connection, bool autoLevelBed);
    void send(PrinterFile file) override;

private:
    QNetworkAccessManager network;
    QUrl origin;
    QByteArray apiKey;
    QString slug;
    PrinterFile file;

    void upload();
};
