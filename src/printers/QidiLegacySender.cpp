#include "printers/QidiLegacySender.h"

#include <QRegularExpression>
#include <QUuid>

#include <utility>

namespace {
constexpr qint64 blockSize = 1280;
constexpr int maxAttempts = 3;
}

std::unique_ptr<PrinterSendJob> QidiLegacySender::createJob(const QJsonObject &connection) const
{
    return std::make_unique<QidiLegacySendJob>(connection, autoLevelBedEnabled());
}

QidiLegacySendJob::QidiLegacySendJob(const QJsonObject &connection, bool autoLevelBed)
    : PrinterSendJob(autoLevelBed)
{
    const auto url = connectionUrl(connection, "udp", 3000);
    socket.connectToHost(url.host(), url.port());
    timeout.setSingleShot(true);
    timeout.setInterval(2000);
    connect(&timeout, &QTimer::timeout, this, [this] {
        if (++attempts >= maxAttempts) {
            onReply = nullptr;
            emit failed("The QIDI printer did not answer. Check its address and that it is on.");
            return;
        }
        socket.write(pendingDatagram);
        timeout.start();
    });
    connect(&socket, &QUdpSocket::readyRead, this, [this] {
        while (socket.hasPendingDatagrams()) {
            QByteArray reply(socket.pendingDatagramSize(), Qt::Uninitialized);
            socket.readDatagram(reply.data(), reply.size());
            if (!onReply) continue;
            timeout.stop();
            std::exchange(onReply, nullptr)(QString::fromLatin1(reply));
        }
    });
}

void QidiLegacySendJob::request(const QByteArray &datagram, std::function<void(const QString &)> next)
{
    pendingDatagram = datagram;
    onReply = std::move(next);
    attempts = 0;
    socket.write(pendingDatagram);
    timeout.start();
}

void QidiLegacySendJob::command(const QString &text, const QString &action, std::function<void(const QString &)> next)
{
    request(text.toLatin1(), [this, action, next = std::move(next)](const QString &reply) {
        if (reply.contains("Error")) {
            emit failed(QString("QIDI rejected the %1: %2").arg(action, reply.trimmed()));
            return;
        }
        next(reply);
    });
}

void QidiLegacySendJob::send(PrinterFile input)
{
    file = std::move(input);
    file.filename = file.filename.chopped(6).left(24) + "-" + QUuid::createUuid().toString(QUuid::Id128).left(6) + ".gcode";
    emit progress("Connecting to QIDI printer…");
    command("M4001", "connection", [this](const QString &) {
        command("M4000", "status request", [this](const QString &status) {
            // "D:<printed>/<total>/<idle>": the last field is 1 while the printer is idle.
            const auto progress = QRegularExpression(R"(D:(\d+)/(\d+)/(\d+))").match(status);
            if (!progress.hasMatch() || progress.captured(3) != "1") {
                emit failed("The QIDI printer is busy or did not report its status. The current job was left running.");
                return;
            }
            emit this->progress(QString("Uploading QIDI G-code… 0% · 0.0/%1 MiB").arg(file.data.size() / 1048576.0, 0, 'f', 1));
            command("M28 " + file.filename, "upload", [this](const QString &) { sendBlock(); });
        });
    });
}

// Each block is the data, its file offset (4 bytes, little-endian), an XOR checksum of both, and 0x83.
void QidiLegacySendJob::sendBlock()
{
    if (offset >= file.data.size()) {
        emit progress("QIDI upload complete - Starting print…");
        command("M29 " + file.filename, "file save", [this](const QString &) {
            command(QString("M6030 \":%1\" I1").arg(file.filename), "print start", [this](const QString &) { emit completed(); });
        });
        return;
    }
    auto block = file.data.mid(offset, blockSize);
    const auto position = quint32(offset);
    for (int shift = 0; shift < 32; shift += 8) block.append(char((position >> shift) & 0xff));
    char checksum = 0;
    for (const char byte : block) checksum ^= byte;
    block.append(checksum);
    block.append(char(0x83));
    const auto next = qMin(offset + blockSize, file.data.size());
    request(block, [this, next](const QString &reply) {
        if (const auto resend = QRegularExpression(R"(resend (\d+))").match(reply); resend.hasMatch()) {
            offset = resend.captured(1).toLongLong();
        } else if (reply.contains("ok")) {
            offset = next;
            if (offset % (blockSize * 800) == 0)
                emit progress(QString("Uploading QIDI G-code… %1% · %2/%3 MiB").arg(int(offset * 100 / file.data.size()))
                    .arg(offset / 1048576.0, 0, 'f', 1).arg(file.data.size() / 1048576.0, 0, 'f', 1));
        } else {
            emit failed("QIDI rejected an upload block: " + reply.trimmed());
            return;
        }
        sendBlock();
    });
}
