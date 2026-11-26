#pragma once

#include "printers/PrinterSendJob.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QTimer>

#include <functional>

class QNetworkReply;

// Dremel 3D45 and 3D40 through the printer's HTTP command interface.
class DremelSender : public PrinterSender {
public:
    QString protocol() const override { return "dremel_lan"; }

protected:
    int defaultPort() const override { return 80; }
    std::unique_ptr<PrinterSendJob> createJob(const QJsonObject &connection) const override;
};

class DremelSendJob : public PrinterSendJob {
public:
    explicit DremelSendJob(const QJsonObject &connection, bool autoLevelBed);
    void send(PrinterFile file) override;

private:
    QNetworkAccessManager network;
    QTimer confirmation;
    QUrl origin;
    PrinterFile file;
    int confirmationPolls = 0;

    QNetworkRequest request(const QString &path) const;
    // Posts a /command body and calls next with the JSON reply after HTTP success.
    void command(const QByteArray &body, const QString &action, std::function<void(const QJsonObject &)> next);
    void upload();
    void startPrint();
    void confirmStart();
};
