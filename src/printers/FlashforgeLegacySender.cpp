#include "printers/FlashforgeLegacySender.h"

#include <QUuid>

#include <utility>

namespace {
constexpr qint64 chunkSize = 4096;
}

std::unique_ptr<PrinterSendJob> FlashforgeLegacySender::createJob(const QJsonObject &connection) const
{
    return std::make_unique<FlashforgeLegacySendJob>(connection, autoLevelBedEnabled());
}

FlashforgeLegacySendJob::FlashforgeLegacySendJob(const QJsonObject &connection, bool autoLevelBed)
    : PrinterSendJob(autoLevelBed)
{
    const auto url = connectionUrl(connection, "tcp", 8899);
    host = url.host();
    port = url.port();
    timeout.setSingleShot(true);
    timeout.setInterval(15000);
    connect(&timeout, &QTimer::timeout, this, [this] { finish("The Flashforge printer did not answer within 15 seconds."); });
    connect(&socket, &QTcpSocket::errorOccurred, this, [this] { finish("Flashforge connection failed: " + socket.errorString()); });
    connect(&socket, &QTcpSocket::bytesWritten, this, [this] {
        // Data is written in chunks after ~M28; continue once the previous chunk has left the buffer.
        if (dataSent > 0 && dataSent < file.data.size() && socket.bytesToWrite() == 0) sendData();
    });
    connect(&socket, &QTcpSocket::readyRead, this, [this] {
        received += socket.readAll();
        qsizetype end;
        while ((end = received.indexOf('\n')) >= 0) {
            const auto line = QString::fromUtf8(received.left(end)).trimmed();
            received.remove(0, end + 1);
            replyLines << line;
            if (line.compare("ok", Qt::CaseInsensitive) == 0 && afterReply) {
                timeout.stop();
                std::exchange(afterReply, nullptr)(std::exchange(replyLines, {}));
            }
        }
    });
}

void FlashforgeLegacySendJob::finish(const QString &error)
{
    if (done) return;
    done = true;
    timeout.stop();
    socket.disconnect(this);
    socket.abort();
    if (error.isEmpty()) emit completed();
    else emit failed(error);
}

void FlashforgeLegacySendJob::command(const QString &text, std::function<void(const QStringList &)> next)
{
    afterReply = std::move(next);
    timeout.start();
    socket.write(("~" + text + "\r\n").toUtf8());
}

void FlashforgeLegacySendJob::send(PrinterFile input)
{
    file = std::move(input);
    // The firmware stores files under 0:/user/ and accepts only plain ASCII names.
    file.filename = file.filename.chopped(6).left(32) + "-" + QUuid::createUuid().toString(QUuid::Id128).left(6) + ".gcode";
    emit progress("Connecting to Flashforge…");
    connect(&socket, &QTcpSocket::connected, this, [this] {
        command("M601 S1", [this](const QStringList &) {            // take control of the printer
            command("M650", [this](const QStringList &) {           // legacy (non-Klipper) firmware session
                command("M119", [this](const QStringList &status) { // endstop and machine status
                    const bool ready = status.join('\n').contains("MachineStatus: READY", Qt::CaseInsensitive);
                    if (!ready) {
                        finish("The Flashforge printer is busy or not ready. The current job was left running.");
                        return;
                    }
                    emit progress(QString("Uploading Flashforge G-code… 0% · 0.0/%1 MiB").arg(file.data.size() / 1048576.0, 0, 'f', 1));
                    command(QString("M28 %1 0:/user/%2").arg(file.data.size()).arg(file.filename), [this](const QStringList &) {
                        sendData();
                    });
                });
            });
        });
    });
    socket.connectToHost(host, port);
}

void FlashforgeLegacySendJob::sendData()
{
    timeout.start(); // Restarted per chunk: fails only when the printer stops accepting data.
    socket.write(file.data.mid(dataSent, chunkSize));
    dataSent = qMin(dataSent + chunkSize, file.data.size());
    if (dataSent % (chunkSize * 256) == 0)
        emit progress(QString("Uploading Flashforge G-code… %1% · %2/%3 MiB").arg(int(dataSent * 100 / file.data.size()))
            .arg(dataSent / 1048576.0, 0, 'f', 1).arg(file.data.size() / 1048576.0, 0, 'f', 1));
    if (dataSent < file.data.size()) return;
    emit progress("Flashforge upload complete - Saving file…");
    // The firmware needs time to flush the file before it accepts ~M29.
    timeout.stop();
    QTimer::singleShot(3000, this, [this] {
        command("M29", [this](const QStringList &) {
            emit progress("Starting print…");
            command("M23 0:/user/" + file.filename, [this](const QStringList &) { finish(); });
        });
    });
}
