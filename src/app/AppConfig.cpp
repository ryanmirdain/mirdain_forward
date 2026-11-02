#include "app/AppConfig.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>

#include <stdexcept>

AppConfig::AppConfig()
{
    // Keep the existing filename and printer data in place.
    path = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("printers.json");
    QFile file(path);
    if (!file.exists()) return;
    if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error(file.errorString().toStdString());
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        throw std::runtime_error(("Invalid app settings: " + path).toStdString());
    data = document.object();
    for (const auto &name : {QStringLiteral("printers"), QStringLiteral("preferences")})
        if (data.contains(name) && !data.value(name).isObject())
            throw std::runtime_error(("Invalid settings section: " + name).toStdString());
}

QJsonObject AppConfig::section(const QString &name) const
{
    return data.value(name).toObject();
}

void AppConfig::setSection(const QString &name, const QJsonObject &value)
{
    auto updated = data;
    updated.insert(name, value);
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        throw std::runtime_error(("Cannot create settings directory: " + path).toStdString());
    QSaveFile file(path);
    const auto bytes = QJsonDocument(updated).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        throw std::runtime_error(("Cannot save app settings: " + file.errorString()).toStdString());
    data = updated;
}
