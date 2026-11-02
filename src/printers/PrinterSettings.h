#pragma once

#include "printers/PrinterSender.h"

#include <map>

class QMenu;
class AppConfig;

class PrinterSettings {
public:
    PrinterSettings(AppConfig &config, QMenu &menu);
    PrinterSender *remember(const QJsonObject &request);
    void setBusy(bool busy);

private:
    QMenu &menu;
    AppConfig &config;
    std::map<QString, std::unique_ptr<PrinterSender>> printers;

    QJsonObject toJson() const;
    void rebuildMenu();
};
