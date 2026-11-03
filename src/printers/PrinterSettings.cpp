#include "printers/PrinterSettings.h"
#include "app/AppConfig.h"

#include <QMenu>
#include <QMessageBox>

#include <stdexcept>

PrinterSettings::PrinterSettings(AppConfig &config, QMenu &trayMenu)
    : menu(trayMenu), config(config), separator(menu.addSeparator())
{
    const auto saved = config.section("printers");
    for (auto it = saved.constBegin(); it != saved.constEnd(); ++it) {
        const auto json = it.value().toObject();
        auto sender = PrinterSender::create(json.value("protocol").toString());
        sender->loadJson(json);
        printers.emplace(it.key(), std::move(sender));
    }
    rebuildMenu();
}

QJsonObject PrinterSettings::toJson() const
{
    QJsonObject json;
    for (const auto &[key, sender] : printers) json.insert(key, sender->toJson());
    return json;
}

PrinterSender *PrinterSettings::remember(const QJsonObject &request)
{
    const auto connection = request.value("connection").toObject();
    auto candidate = PrinterSender::create(connection.value("protocol").toString());
    const auto endpoint = candidate->connectionKey(connection);
    const auto id = request.value("custom_printer_id").toString();
    const auto key = id.isEmpty() ? "endpoint:" + endpoint : "custom:" + id;
    const auto existing = printers.find(key);
    if (existing != printers.end()) candidate->loadJson(existing->second->toJson());
    candidate->setIdentity(id, request.value("custom_printer_name").toString(), endpoint);
    const auto json = candidate->toJson();
    if (existing == printers.end() || existing->second->toJson() != json) {
        auto updated = toJson();
        updated.insert(key, json);
        config.setSection("printers", updated);
        if (existing != printers.end() && existing->second->protocol() == candidate->protocol())
            existing->second->loadJson(json);
        else
            printers[key] = std::move(candidate);
        rebuildMenu();
    }
    return printers.at(key).get();
}

void PrinterSettings::setBusy(bool busy)
{
    for (auto *action : printerActions)
        if (action->menu()) action->setEnabled(!busy);
}

void PrinterSettings::rebuildMenu()
{
    for (auto *action : printerActions) {
        if (auto *submenu = action->menu()) delete submenu;
        else delete action;
    }
    printerActions.clear();
    if (printers.empty()) {
        auto *empty = new QAction("Printers appear after Send and print", &menu);
        empty->setEnabled(false);
        menu.insertAction(separator, empty);
        printerActions.append(empty);
    }
    for (const auto &[key, retained] : printers) {
        auto *sender = retained.get();
        auto *submenu = new QMenu(sender->name(), &menu);
        menu.insertMenu(separator, submenu);
        printerActions.append(submenu->menuAction());
        if (!sender->customPrinterId().isEmpty()) {
            auto *identity = submenu->addAction("ID: " + sender->customPrinterId());
            identity->setEnabled(false);
        }
        auto *level = submenu->addAction("Auto-level bed");
        level->setCheckable(true);
        level->setChecked(sender->autoLevelBedEnabled());
        if (!sender->supportsAutoLevelBed()) {
            level->setEnabled(false);
            submenu->addAction("Sender does not support this setting")->setEnabled(false);
        }
        QObject::connect(level, &QAction::triggered, submenu, [this, sender, level](bool enabled) {
            const bool previous = sender->autoLevelBedEnabled();
            sender->setAutoLevelBedEnabled(enabled);
            try {
                config.setSection("printers", toJson());
            } catch (const std::exception &error) {
                sender->setAutoLevelBedEnabled(previous);
                level->setChecked(previous);
                QMessageBox::critical(&menu, "Mirdain Forward", QString::fromUtf8(error.what()));
            }
        });
    }
}
