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

class PrinterSendJob;

// One sender per registered printer. Construction and JSON loading stay offline.
class PrinterSender : public QObject {
    Q_OBJECT

public:
    PrinterSender();
    ~PrinterSender() override;
    static std::unique_ptr<PrinterSender> create(const QString &protocol);
    virtual QString protocol() const = 0;
    virtual PrinterFile::Format fileFormat() const { return PrinterFile::Format::Gcode; }
    virtual bool supportsAutoLevelBed() const { return false; }
    bool autoLevelBedEnabled() const { return autoLevelBed; }
    void setAutoLevelBedEnabled(bool enabled) { autoLevelBed = enabled; }
    virtual void loadJson(const QJsonObject &json);
    virtual QJsonObject toJson() const;
    QString name() const { return printerName; }
    QString customPrinterId() const { return printerId; }
    void setIdentity(const QString &id, const QString &customName, const QString &fallbackName);
    QString connectionKey(const QJsonObject &connection) const;
    void send(PrinterFile file, const QJsonObject &connection);

signals:
    void progress(const QString &message);
    void completed(); // The printer accepted the print request.
    void failed(const QString &message);

protected:
    virtual int defaultPort() const = 0;
    virtual std::unique_ptr<PrinterSendJob> createJob(const QJsonObject &connection) const = 0;

private:
    QString printerId;
    QString customName;
    QString printerName;
    bool autoLevelBed = true;
    std::unique_ptr<PrinterSendJob> activeJob;
};
