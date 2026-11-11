#include "printers/BambuFtpsUpload.h"

#include <QNetworkProxy>
#include <QRegularExpression>
#include <QSslConfiguration>

#include <openssl/ssl.h>

BambuFtpsUpload::BambuFtpsUpload(QObject *parent) : QObject(parent)
{
    // Link the runtime explicitly so deployment tools bundle OpenSSL for Qt's
    // session-reuse backend, including on machines without OpenSSL installed.
    OPENSSL_init_ssl(0, nullptr);
    timeout.setSingleShot(true);
    progressTimer.setInterval(1000);
    connect(&timeout, &QTimer::timeout, this, [this] { fail("connection or transfer stalled."); });
    connect(&progressTimer, &QTimer::timeout, this, &BambuFtpsUpload::reportProgress);
    connect(&control, &QSslSocket::readyRead, this, &BambuFtpsUpload::readReplies);
    connect(&control, &QSslSocket::errorOccurred, this, [this] { fail(control.errorString()); });
    connect(&data, &QSslSocket::errorOccurred, this, [this](QAbstractSocket::SocketError error) {
        if (dataClosed && error == QAbstractSocket::RemoteHostClosedError) return;
        fail(data.errorString());
    });
    connect(&control, &QSslSocket::disconnected, this, [this] {
        fail("control connection closed before upload confirmation.");
    });
    connect(&data, &QSslSocket::connected, this, [this] {
        // As in ftplib, open TCP first, send STOR, then negotiate data TLS
        // after the server's preliminary transfer reply.
        command(Step::Store, "STOR " + filename.toUtf8());
    });
    connect(&data, &QSslSocket::encrypted, this, [this] {
        progressClock.start();
        progressTimer.start();
        writeData();
        if (!dataClosed) reportProgress();
    });
    connect(&data, &QSslSocket::encryptedBytesWritten, this, [this] {
        timeout.start(15000);
        // Refill after Qt has finished handling the underlying socket write.
        QMetaObject::invokeMethod(this, &BambuFtpsUpload::writeData, Qt::QueuedConnection);
    });
}

void BambuFtpsUpload::start(const QUrl &endpoint, const QString &code, const QString &path,
    const QString &name, const QByteArray &bytes)
{
    url = endpoint;
    accessCode = code;
    directory = path;
    filename = name;
    archive = bytes;
    stopped = false;
    if (QSslSocket::activeBackend() != "openssl") {
        fail("Qt's OpenSSL TLS backend is required for Bambu data-session reuse.");
        return;
    }
    auto tls = QSslConfiguration::defaultConfiguration();
    tls.setPeerVerifyMode(QSslSocket::VerifyNone);
    tls.setSslOption(QSsl::SslOptionDisableSessionPersistence, false);
    control.setSslConfiguration(tls);
    control.setProxy(QNetworkProxy::NoProxy);
    data.setProxy(QNetworkProxy::NoProxy);
    timeout.start(15000);
    control.connectToHostEncrypted(url.host(), url.port());
}

void BambuFtpsUpload::stop()
{
    stopped = true;
    timeout.stop();
    progressTimer.stop();
    control.abort();
    data.abort();
}

void BambuFtpsUpload::fail(const QString &message)
{
    if (stopped) return;
    stop();
    emit failed("Bambu FTPS upload failed: " + message);
}

void BambuFtpsUpload::command(Step next, const QByteArray &value)
{
    step = next;
    timeout.start(15000);
    control.write(value + "\r\n");
}

void BambuFtpsUpload::readReplies()
{
    while (!stopped && control.canReadLine()) {
        const auto line = control.readLine().trimmed();
        if (step == Step::Welcome && line.contains("vsFTPd")) vsFtpd = true;
        if (!multilineCode.isEmpty()) {
            if (!line.startsWith(multilineCode + ' ')) continue;
            multilineCode.clear();
        } else if (line.size() >= 4 && line[3] == '-') {
            multilineCode = line.first(3);
            continue;
        }
        reply(line.first(3).toInt(), line);
    }
}

void BambuFtpsUpload::reply(int code, const QByteArray &line)
{
    // Never include server text from USER/PASS replies: it may echo credentials.
    const auto reject = [this, code] {
        fail(QString("FTP reply %1 at stage %2. Check the LAN access code and writable printer storage.")
            .arg(code).arg(int(step)));
    };
    switch (step) {
    case Step::Welcome:
        if (code == 120) return;
        if (code != 220) return reject();
        command(Step::User, "USER bblp");
        break;
    case Step::User:
        if (code == 230) command(Step::Pbsz, "PBSZ 0");
        else if (code == 331) command(Step::Password, "PASS " + accessCode.toUtf8());
        else reject();
        break;
    case Step::Password:
        if (code != 230) return reject();
        command(Step::Pbsz, "PBSZ 0");
        break;
    case Step::Pbsz:
        if (code != 200) return reject();
        command(Step::Prot, "PROT P");
        break;
    case Step::Prot:
        if (code != 200) return reject();
        command(Step::Type, "TYPE I");
        break;
    case Step::Type:
        if (code != 200) return reject();
        command(Step::Directory, "CWD " + directory.toUtf8());
        break;
    case Step::Directory:
        if (code != 250) return reject();
        command(Step::Passive, "PASV");
        break;
    case Step::Passive: {
        if (code != 227) return reject();
        const auto match = QRegularExpression("\\((?:[0-9]+,){4}([0-9]+),([0-9]+)\\)")
            .match(QString::fromUtf8(line));
        if (!match.hasMatch() || match.captured(1).toInt() > 255 || match.captured(2).toInt() > 255) {
            fail("invalid passive port reply.");
            return;
        }
        // Includes the serialized control TLS session for data-session reuse.
        data.setSslConfiguration(control.sslConfiguration());
        data.setPeerVerifyName(url.host());
        // Ignore the PASV address; use the same printer as the control socket.
        data.connectToHost(control.peerAddress(), match.captured(1).toInt() * 256 + match.captured(2).toInt());
        break;
    }
    case Step::Store:
        if (code != 125 && code != 150) return reject();
        step = Step::Complete;
        data.startClientEncryption();
        break;
    case Step::Complete:
        if (code != 226 && code != 250) return reject();
        if (offset != archive.size() || data.bytesToWrite() || data.encryptedBytesToWrite()) {
            fail("printer confirmed an incomplete upload.");
            return;
        }
        stop();
        emit completed();
        break;
    }
}

void BambuFtpsUpload::writeData()
{
    if (stopped || dataClosed || step != Step::Complete || !data.isEncrypted()) return;
    // Keep a bounded pipeline full instead of feeding one chunk per callback.
    // Use the same 8 KiB writes as the printer client's storbinary_bambu.
    constexpr qint64 queueLimit = 256 * 1024;
    while (offset < archive.size() && data.bytesToWrite() + data.encryptedBytesToWrite() < queueLimit) {
        const auto count = data.write(archive.constData() + offset, qMin<qsizetype>(8192, archive.size() - offset));
        if (count < 0) return fail(data.errorString());
        if (count == 0) return;
        offset += count;
    }
    if (offset != archive.size() || data.bytesToWrite() || data.encryptedBytesToWrite()) return;
    dataClosed = true;
    progressTimer.stop();
    emit progress("Waiting for Bambu FTPS upload confirmation…");
    // Port the printer client's storbinary_bambu workaround: P1P hangs on
    // TLS unwrap, while vsFTPd needs close_notify to accept the upload.
    if (vsFtpd) data.disconnectFromHost();
    else data.abort();
}

void BambuFtpsUpload::reportProgress()
{
    // Exclude both Qt's plaintext queue and its encrypted socket queue. TLS
    // framing makes this an approximation until the final buffers have drained.
    const auto sent = qMax(lastProgressBytes,
        qMax<qint64>(0, offset - data.bytesToWrite() - data.encryptedBytesToWrite()));
    const auto elapsed = progressClock.restart();
    const double kibPerSecond = elapsed > 0 ? (sent - lastProgressBytes) * 1000.0 / elapsed / 1024 : 0;
    lastProgressBytes = sent;
    const int percent = archive.isEmpty() ? 100 : int(sent * 100 / archive.size());
    emit progress(QString("Uploading Bambu… %1% · %2/%3 MiB · %4 KiB/s")
        .arg(percent).arg(sent / 1048576.0, 0, 'f', 1)
        .arg(archive.size() / 1048576.0, 0, 'f', 1).arg(kibPerSecond, 0, 'f', 0));
}
