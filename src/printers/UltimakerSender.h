#pragma once

#include "printers/PrinterSendJob.h"

#include <QNetworkAccessManager>
#include <QJsonDocument>
#include <QNetworkRequest>
#include <QTimer>

#include <functional>
#include <memory>

class QNetworkReply;

// UltiMaker S-line printers (S3, S5, S7) through their local HTTP API.
class UltimakerSender : public PrinterSender {
public:
    QString protocol() const override { return "ultimaker_api"; }

    // The id and key approved on the printer, kept in memory so later sends need no approval.
    struct Credentials { QString id; QString key; };

protected:
    int defaultPort() const override { return 80; }
    std::unique_ptr<PrinterSendJob> createJob(const QJsonObject &connection) const override;

private:
    std::shared_ptr<Credentials> approved = std::make_shared<Credentials>();
};

class UltimakerSendJob : public PrinterSendJob {
public:
    UltimakerSendJob(const QJsonObject &connection, std::shared_ptr<UltimakerSender::Credentials> approved);
    void send(PrinterFile file) override;

private:
    QNetworkAccessManager network;
    QTimer approval;
    QUrl origin;
    std::shared_ptr<UltimakerSender::Credentials> credentials;
    PrinterFile file;
    int approvalPolls = 0;

    QNetworkRequest request(const QString &path) const;
    void get(const QString &path, std::function<void(int, const QJsonDocument &)> next);
    void verifyCredentials();
    void requestCredentials();
    void awaitApproval();
    void checkIdle();
    void upload();
};
