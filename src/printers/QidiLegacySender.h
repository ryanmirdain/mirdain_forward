#pragma once

#include "printers/PrinterSendJob.h"

#include <QTimer>
#include <QUdpSocket>

#include <functional>

// Older QIDI printers (X-Plus, X-Max, X-CF Pro) through QIDI's UDP protocol on port 3000.
class QidiLegacySender : public PrinterSender {
public:
    QString protocol() const override { return "qidi_legacy"; }

protected:
    int defaultPort() const override { return 3000; }
    std::unique_ptr<PrinterSendJob> createJob(const QJsonObject &connection) const override;
};

class QidiLegacySendJob : public PrinterSendJob {
public:
    explicit QidiLegacySendJob(const QJsonObject &connection, bool autoLevelBed);
    void send(PrinterFile file) override;

private:
    QUdpSocket socket;
    QTimer timeout;
    QByteArray pendingDatagram;
    std::function<void(const QString &)> onReply;
    int attempts = 0;
    PrinterFile file;
    qint64 offset = 0;

    // Sends one datagram and calls next with the reply, retrying up to three times.
    void request(const QByteArray &datagram, std::function<void(const QString &)> next);
    // As request(), but fails the job on a reply containing "Error".
    void command(const QString &text, const QString &action, std::function<void(const QString &)> next);
    void sendBlock();
};
