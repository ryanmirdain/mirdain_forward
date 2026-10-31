#pragma once

#include "PrinterSender.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#include <functional>

class PrusaSender : public PrinterSender {
public:
    explicit PrusaSender(const QJsonObject &connection);
    void send(PrinterFile file) override;

private:
    QNetworkAccessManager network;
    QUrl origin;
    QByteArray apiKey;
    QString storage;
    PrinterFile file;
    bool modern = false;

    QNetworkRequest request(const QString &path) const;
    bool accepted(QNetworkReply *reply);
    void get(const QString &path, std::function<void(const QJsonObject &)> next);
    void checkPrinter();
    void chooseStorage();
    void upload();
};
