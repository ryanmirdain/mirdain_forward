#pragma once

#include "printers/PrinterSendJob.h"

#include <QNetworkAccessManager>

#include <functional>

// Marlin printers with an ESP3D Wi-Fi module, which writes uploads to the printer's SD card.
class Esp3dSender : public PrinterSender {
public:
    QString protocol() const override { return "esp3d"; }

protected:
    int defaultPort() const override { return 80; }
    std::unique_ptr<PrinterSendJob> createJob(const QJsonObject &connection) const override;
};

class Esp3dSendJob : public PrinterSendJob {
public:
    explicit Esp3dSendJob(const QJsonObject &connection, bool autoLevelBed);
    void send(PrinterFile file) override;

private:
    QNetworkAccessManager network;
    QUrl origin;
    PrinterFile file;

    // Sends one G-code line through /command?plain= and calls next with the module's reply text.
    void command(const QString &gcode, std::function<void(const QString &)> next);
    void upload();
    void startPrint();
};
