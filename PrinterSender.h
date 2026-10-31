#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QUrl>

#include <memory>

struct PrinterFile {
    enum class Format { Gcode, Gcode3mf };
    QByteArray data;
    QString filename;
    QString bedType;
    QString gcodePath; // Member inside a native .gcode.3mf archive.
    QByteArray gcodeMd5;
};

// One sender per request. Implementations upload the file and request printing.
class PrinterSender : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    static std::unique_ptr<PrinterSender> create(const QJsonObject &connection);
    virtual PrinterFile::Format fileFormat() const { return PrinterFile::Format::Gcode; }
    virtual void send(PrinterFile file) = 0;

signals:
    void progress(const QString &message);
    void completed(); // The printer accepted the print request.
    void failed(const QString &message);

protected:
    static QUrl connectionUrl(const QJsonObject &connection, const QString &scheme,
        int defaultPort, const QString &portKey = "port");
};
