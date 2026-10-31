#include "PrinterSender.h"
#include "ElegooSender.h"
#include "PrusaSender.h"
#include "BambuSender.h"

#include <stdexcept>

std::unique_ptr<PrinterSender> PrinterSender::create(const QJsonObject &connection)
{
    const auto protocol = connection.value("protocol").toString();
    if (protocol == "elegoo_sdcp")
        return std::make_unique<ElegooSender>(connection);
    if (protocol == "prusalink")
        return std::make_unique<PrusaSender>(connection);
    if (protocol == "bambu_lan")
        return std::make_unique<BambuSender>(connection);
    throw std::runtime_error(("Unsupported printer protocol: " + protocol).toStdString());
}

QUrl PrinterSender::connectionUrl(const QJsonObject &connection, const QString &scheme,
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
