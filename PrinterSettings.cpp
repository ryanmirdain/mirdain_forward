#include "PrinterSettings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMenu>
#include <QMessageBox>
#include <QSaveFile>
#include <QStandardPaths>

#include <stdexcept>

PrinterSettings::PrinterSettings(QMenu &printerMenu) : menu(printerMenu)
{
    path = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("printers.json");
    QFile file(path);
    if (file.exists()) {
        if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error(file.errorString().toStdString());
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(file.readAll(), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()
            || !document.object().value("printers").isObject())
            throw std::runtime_error(("Invalid printer settings: " + path).toStdString());
        const auto saved = document.object().value("printers").toObject();
        for (auto it = saved.constBegin(); it != saved.constEnd(); ++it) {
            const auto json = it.value().toObject();
            auto sender = PrinterSender::create(json.value("protocol").toString());
            sender->loadJson(json);
            printers.emplace(it.key(), std::move(sender));
        }
    }
    rebuildMenu();
}

QJsonObject PrinterSettings::toJson() const
{
    QJsonObject json;
    for (const auto &[key, sender] : printers) json.insert(key, sender->toJson());
    return json;
}

void PrinterSettings::save(const QJsonObject &updated) const
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        throw std::runtime_error(("Cannot create printer settings directory: " + path).toStdString());
    QSaveFile file(path);
    const auto data = QJsonDocument(QJsonObject{{"printers", updated}}).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
        throw std::runtime_error(("Cannot save printer settings: " + file.errorString()).toStdString());
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
        save(updated);
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
    menu.setEnabled(!busy);
}

void PrinterSettings::rebuildMenu()
{
    menu.clear();
    if (printers.empty()) menu.addAction("Printers appear after Send and print")->setEnabled(false);
    for (const auto &[key, retained] : printers) {
        auto *sender = retained.get();
        auto *submenu = menu.addMenu(sender->name());
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
                save(toJson());
            } catch (const std::exception &error) {
                sender->setAutoLevelBedEnabled(previous);
                level->setChecked(previous);
                QMessageBox::critical(&menu, "Mirdain Forward", QString::fromUtf8(error.what()));
            }
        });
    }
}
