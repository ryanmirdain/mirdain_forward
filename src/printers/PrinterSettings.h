#pragma once

#include "printers/PrinterSender.h"

#include <QJsonArray>
#include <QList>

#include <map>

class QAction;
class QMenu;
class AppConfig;

class PrinterSettings {
public:
    PrinterSettings(AppConfig &config, QMenu &menu);
    PrinterSender *remember(const QJsonObject &request);
    // Replaces the printers with an organization's Forward printers: its slicer printers that
    // have a connection. Printers that stay keep their local settings, such as auto-level.
    void replaceWithOrganization(const QJsonArray &slicerPrinters);
    // The printers in the menu, by name. id is the custom printer ID, which is empty for
    // printers set up without one.
    struct Entry { QString id; QString name; };
    QList<Entry> entries() const;
    void setBusy(bool busy);
    bool busy() const { return transferring; }

private:
    QMenu &menu;
    AppConfig &config;
    QAction *separator;
    bool transferring = false;
    QList<QAction *> printerActions;
    std::map<QString, std::unique_ptr<PrinterSender>> printers;

    QJsonObject toJson() const;
    void rebuildMenu();
};
