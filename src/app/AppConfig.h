#pragma once

#include <QJsonObject>
#include <QString>

class AppConfig {
public:
    AppConfig();
    QJsonObject section(const QString &name) const;
    void setSection(const QString &name, const QJsonObject &value);

private:
    QString path;
    QJsonObject data;
};
