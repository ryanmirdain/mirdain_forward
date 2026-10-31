#pragma once

#include "PrinterSendJob.h"

#include <QMqttClient>
#include <QJsonArray>
#include <QProcess>
#include <QTemporaryFile>
#include <QTimer>

class BambuSender : public PrinterSender {
public:
    QString protocol() const override { return "bambu_lan"; }
    PrinterFile::Format fileFormat() const override { return PrinterFile::Format::Gcode3mf; }
    bool supportsAutoLevelBed() const override { return true; }

protected:
    int defaultPort() const override { return 8883; }
    std::unique_ptr<PrinterSendJob> createJob(const QJsonObject &connection) const override;
};

class BambuSendJob : public PrinterSendJob {
public:
    explicit BambuSendJob(const QJsonObject &connection, bool autoLevelBed);
    void send(PrinterFile file) override;

private:
    QMqttClient mqtt;
    QProcess transfer;
    QTemporaryFile archive;
    QTimer timeout;
    QUrl ftpUrl;
    QString serial;
    QString accessCode;
    QString sequence;
    QJsonArray amsMapping;
    bool useAms = false;
    PrinterFile file;
    bool awaitingIdle = false;
    bool uploaded = false;
    bool startSent = false;
    bool done = false;

    bool publish(const QJsonObject &message);
    void checkPrinter();
    void receive(const QJsonObject &report);
    void upload();
    void startPrint();
    void finish(const QString &error = {});
};
