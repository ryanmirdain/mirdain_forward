#pragma once

#include "PrinterSender.h"

// Temporary transport state for one print. Constructing a job does not connect.
class PrinterSendJob : public QObject {
    Q_OBJECT

public:
    explicit PrinterSendJob(bool autoLevelBed) : autoLevelBed(autoLevelBed) {}
    virtual void send(PrinterFile file) = 0;
    static QUrl connectionUrl(const QJsonObject &connection, const QString &scheme,
        int defaultPort, const QString &portKey = "port");

signals:
    void progress(const QString &message);
    void completed();
    void failed(const QString &message);

protected:
    bool autoLevelBed;
};
