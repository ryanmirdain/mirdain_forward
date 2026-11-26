#pragma once

#include "printers/PrinterSendJob.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrlQuery>

#include <functional>

class QNetworkReply;

// RepRapFirmware 3 printers on Duet boards, standalone (rr_ requests) or with an SBC (DSF).
class DuetSender : public PrinterSender {
public:
    QString protocol() const override { return "duet"; }

protected:
    int defaultPort() const override { return 80; }
    std::unique_ptr<PrinterSendJob> createJob(const QJsonObject &connection) const override;
};

class DuetSendJob : public PrinterSendJob {
public:
    explicit DuetSendJob(const QJsonObject &connection, bool autoLevelBed);
    void send(PrinterFile file) override;

private:
    QNetworkAccessManager network;
    QTimer confirmation;
    QUrl origin;
    QString password;
    QByteArray sessionKey;
    PrinterFile file;
    bool sbc = false;
    int confirmationPolls = 0;

    QNetworkRequest request(const QString &path, const QUrlQuery &query = {}) const;
    // Calls next with the response body after HTTP success; otherwise emits failed() with action.
    void call(QNetworkReply *reply, const QString &action, std::function<void(const QByteArray &)> next);
    void connectBoard();
    void readStatus(std::function<void(const QString &)> next);
    void checkIdle();
    void upload();
    void startPrint();
    void confirmStart();
};
