#include "PrinterSender.h"
#include "PrinterSendJob.h"
#include "ElegooSender.h"
#include "PrusaSender.h"
#include "BambuSender.h"

#include <stdexcept>
#include <utility>

PrinterSender::PrinterSender() = default;
PrinterSender::~PrinterSender() = default;

std::unique_ptr<PrinterSender> PrinterSender::create(const QString &protocol)
{
    if (protocol == "elegoo_sdcp")
        return std::make_unique<ElegooSender>();
    if (protocol == "prusalink")
        return std::make_unique<PrusaSender>();
    if (protocol == "bambu_lan")
        return std::make_unique<BambuSender>();
    throw std::runtime_error(("Unsupported printer protocol: " + protocol).toStdString());
}

void PrinterSender::loadJson(const QJsonObject &json)
{
    printerId = json.value("custom_printer_id").toString();
    customName = json.value("custom_printer_name").toString();
    printerName = json.value("name").toString();
    autoLevelBed = json.value("settings").toObject().value("auto_level_bed").toBool(true);
}

QJsonObject PrinterSender::toJson() const
{
    return {{"custom_printer_id", printerId.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(printerId)},
        {"custom_printer_name", customName.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(customName)},
        {"name", printerName}, {"protocol", protocol()},
        {"settings", QJsonObject{{"auto_level_bed", autoLevelBed}}}};
}

void PrinterSender::setIdentity(const QString &id, const QString &name, const QString &fallbackName)
{
    printerId = id;
    customName = name;
    printerName = name.isEmpty() ? fallbackName : name;
}

QString PrinterSender::connectionKey(const QJsonObject &connection) const
{
    return PrinterSendJob::connectionUrl(connection,
        QString(protocol()).replace('_', '-'), defaultPort()).toString();
}

void PrinterSender::send(PrinterFile file, const QJsonObject &connection)
{
    activeJob = createJob(connection);
    connect(activeJob.get(), &PrinterSendJob::progress, this, &PrinterSender::progress);
    connect(activeJob.get(), &PrinterSendJob::completed, this, [this] {
        activeJob.release()->deleteLater();
        emit completed();
    });
    connect(activeJob.get(), &PrinterSendJob::failed, this, [this](const QString &message) {
        activeJob.release()->deleteLater();
        emit failed(message);
    });
    activeJob->send(std::move(file));
}

QUrl PrinterSendJob::connectionUrl(const QJsonObject &connection, const QString &scheme,
    int defaultPort, const QString &portKey)
{
    const auto host = connection.value("host").toString().trimmed();
    const auto port = connection.value(portKey).toDouble(defaultPort);
    QUrl url;
    url.setScheme(scheme);
    url.setHost(host);
    if (host.isEmpty() || !url.isValid() || (connection.contains(portKey) && !connection.value(portKey).isDouble())
        || port < 1 || port > 65535 || port != int(port))
        throw std::runtime_error(("connection requires a hostname/IP and an optional integer "
            + portKey + " (default " + QString::number(defaultPort) + ").").toStdString());
    url.setPort(int(port));
    return url;
}
