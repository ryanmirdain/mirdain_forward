#pragma once

#include "printers/PrinterSender.h"

#include <QList>

#include <map>

class QAction;
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
    QAction *separator;
    QList<QAction *> printerActions;
    std::map<QString, std::unique_ptr<PrinterSender>> printers;

    QJsonObject toJson() const;
    void rebuildMenu();
};
