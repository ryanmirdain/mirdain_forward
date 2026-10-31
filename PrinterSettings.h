#pragma once

#include "PrinterSender.h"

#include <map>

class QMenu;

class PrinterSettings {
public:
    explicit PrinterSettings(QMenu &menu);
    PrinterSender *remember(const QJsonObject &request);
    void setBusy(bool busy);

private:
    QMenu &menu;
    QString path;
    std::map<QString, std::unique_ptr<PrinterSender>> printers;

    QJsonObject toJson() const;
    void save(const QJsonObject &updated) const;
    void rebuildMenu();
};
