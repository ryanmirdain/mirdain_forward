#pragma once

#include "printers/PrinterSendJob.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QTimer>

#include <functional>
#include <memory>

class QNetworkReply;

// Snapmaker 2.0 (A150, A250, A350) through the HTTP API on port 8080.
class SnapmakerSender : public PrinterSender {
public:
    QString protocol() const override { return "snapmaker_http"; }

protected:
    int defaultPort() const override { return 8080; }
    std::unique_ptr<PrinterSendJob> createJob(const QJsonObject &connection) const override;

private:
    // The token approved on the touchscreen, kept in memory so later sends need no approval.
    std::shared_ptr<QString> approvedToken = std::make_shared<QString>();
};

class SnapmakerSendJob : public PrinterSendJob {
public:
    SnapmakerSendJob(const QJsonObject &connection, std::shared_ptr<QString> approvedToken);
    void send(PrinterFile file) override;

private:
    QNetworkAccessManager network;
    QTimer approval;
    QUrl origin;
    std::shared_ptr<QString> token;
    PrinterFile file;
    int approvalPolls = 0;

    QNetworkRequest request(const QString &path) const;
    void connectPrinter();
    // Calls next with the HTTP status and status JSON; 204 means the token awaits approval.
    void readStatus(std::function<void(int, const QJsonObject &)> next);
    void awaitApproval();
    void checkIdle(const QJsonObject &status);
    void upload();
    void startPrint();
};
