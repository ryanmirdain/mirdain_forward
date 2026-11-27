#include "printers/BambuDiscovery.h"

#include <QHostInfo>
#include <QNetworkDatagram>
#include <QTimer>
#include <QUdpSocket>

#include <memory>

static QByteArray search(int port)
{
    return "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:" + QByteArray::number(port) + "\r\nMAN: \"ssdp:discover\"\r\nMX: 1\r\n"
        "ST: urn:bambulab-com:device:3dprinter:1\r\n\r\n";
}

static QString usn(const QByteArray &packet)
{
    for (const auto &line : packet.split('\n')) {
        const auto colon = line.indexOf(':');
        if (colon > 0 && line.left(colon).trimmed().compare("USN", Qt::CaseInsensitive) == 0)
            return QString::fromUtf8(line.mid(colon + 1)).trimmed();
    }
    return {};
}

void findBambuSerial(const QString &host, QObject *context, std::function<void(const QString &serial)> done)
{
    QHostInfo::lookupHost(host, context, [context, done](const QHostInfo &info) {
        QHostAddress printer;
        for (const auto &address : info.addresses())
            if (address.protocol() == QAbstractSocket::IPv4Protocol) { printer = address; break; }
        if (printer.isNull()) {
            done({});
            return;
        }
        auto *socket = new QUdpSocket(context);
        auto *timeout = new QTimer(socket);
        auto finished = std::make_shared<bool>(false);
        const auto finish = [socket, done, finished](const QString &serial) {
            if (*finished) return;
            *finished = true;
            socket->deleteLater();
            done(serial);
        };
        QObject::connect(socket, &QUdpSocket::readyRead, socket, [socket, printer, finish] {
            while (socket->hasPendingDatagrams()) {
                const auto datagram = socket->receiveDatagram();
                if (!datagram.senderAddress().isEqual(printer, QHostAddress::TolerantConversion)) continue;
                const auto serial = usn(datagram.data());
                if (!serial.isEmpty()) return finish(serial);
            }
        });
        QObject::connect(timeout, &QTimer::timeout, socket, [finish] { finish({}); });
        socket->bind(QHostAddress::AnyIPv4, 0);
        // UDP can drop a packet; ask on both ports twice before giving up.
        for (const auto port : {2021, 1990}) socket->writeDatagram(search(port), printer, port);
        QTimer::singleShot(1000, socket, [socket, printer] {
            for (const auto port : {2021, 1990}) socket->writeDatagram(search(port), printer, port);
        });
        timeout->setSingleShot(true);
        timeout->start(3000);
    });
}
