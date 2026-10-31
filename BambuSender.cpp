#include "BambuSender.h"

#include <QJsonDocument>
#include <QMqttSubscription>
#include <QRandomGenerator>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QUuid>

#include <stdexcept>
#include <utility>

static QString curlQuote(QString value)
{
    value.replace("\\", "\\\\");
    value.replace("\"", "\\\"");
    value.replace("\r", "\\r");
    value.replace("\n", "\\n");
    return "\"" + value + "\"";
}

BambuSender::BambuSender(const QJsonObject &connection)
{
    const auto mqttUrl = connectionUrl(connection, "mqtts", 8883);
    ftpUrl = connectionUrl(connection, "ftps", 990, "ftp_port");
    serial = connection.value("serial_number").toString().trimmed();
    accessCode = connection.value("access_code").toString().trimmed();
    if (serial.isEmpty() || serial.contains('/') || serial.contains('+') || serial.contains('#') || accessCode.isEmpty())
        throw std::runtime_error("Bambu LAN requires serial_number and access_code from the printer's LAN settings.");
    useAms = connection.value("use_ams").toBool();
    amsMapping = connection.value("ams_mapping").toArray();
    if (useAms && amsMapping.isEmpty())
        throw std::runtime_error("Set ams_mapping when use_ams is true (one slot ID per sliced filament).");
    for (const auto &slot : amsMapping)
        if (!slot.isDouble() || slot.toDouble() != slot.toInt() || slot.toInt() < -1 || slot.toInt() > 254)
            throw std::runtime_error("ams_mapping must contain integer slot IDs from -1 to 254.");
    if (!useAms) amsMapping = QJsonArray{254};
    mqtt.setHostname(mqttUrl.host());
    mqtt.setPort(mqttUrl.port());
    mqtt.setProtocolVersion(QMqttClient::MQTT_3_1_1);
    mqtt.setClientId("mirdain_" + QUuid::createUuid().toString(QUuid::Id128).left(12));
    mqtt.setUsername("bblp");
    mqtt.setPassword(accessCode);
    timeout.setSingleShot(true);
    connect(&timeout, &QTimer::timeout, this, [this] {
        finish(startSent
            ? "Bambu print confirmation timed out. Check the printer before retrying; start is not retried."
            : "Bambu connection or status timed out. Check the address, serial number, LAN access code, and Developer Mode.");
    });
}

void BambuSender::send(PrinterFile input)
{
    file = std::move(input);
    emit progress("Connecting to Bambu LAN…");
    connect(&mqtt, &QMqttClient::connected, this, [this] {
        auto *subscription = mqtt.subscribe(QMqttTopicFilter("device/" + serial + "/report"));
        if (!subscription) {
            finish("Bambu report subscription failed.");
            return;
        }
        connect(subscription, &QMqttSubscription::stateChanged, this, [this](QMqttSubscription::SubscriptionState state) {
            if (state == QMqttSubscription::Subscribed) checkPrinter();
            else if (state == QMqttSubscription::Error) finish("Bambu report subscription was rejected.");
        });
    });
    connect(&mqtt, &QMqttClient::errorChanged, this, [this](QMqttClient::ClientError error) {
        if (error != QMqttClient::NoError)
            finish(QString("Bambu MQTT connection failed (%1). Check the LAN access code and Developer Mode.").arg(int(error)));
    });
    connect(&mqtt, &QMqttClient::disconnected, this, [this] {
        finish("Bambu disconnected before print confirmation. Check the printer before retrying.");
    });
    connect(&mqtt, &QMqttClient::messageReceived, this, [this](const QByteArray &bytes, const QMqttTopicName &) {
        receive(QJsonDocument::fromJson(bytes).object().value("print").toObject());
    });
    // Bambu LAN services use the printer's self-signed certificate.
    auto tls = QSslConfiguration::defaultConfiguration();
    tls.setPeerVerifyMode(QSslSocket::VerifyNone);
    timeout.start(15000);
    mqtt.connectToHostEncrypted(tls);
}

bool BambuSender::publish(const QJsonObject &message)
{
    if (mqtt.publish(QMqttTopicName("device/" + serial + "/request"),
            QJsonDocument(message).toJson(QJsonDocument::Compact), 0) >= 0) return true;
    finish("Could not send the Bambu command.");
    return false;
}

void BambuSender::checkPrinter()
{
    awaitingIdle = true;
    timeout.start(15000);
    publish({{"pushing", QJsonObject{{"command", "pushall"}, {"sequence_id", "0"}, {"version", 1}, {"push_target", 1}}}});
}

void BambuSender::receive(const QJsonObject &report)
{
    if (done) return;
    if (awaitingIdle && report.contains("gcode_state")) {
        awaitingIdle = false;
        const auto state = report.value("gcode_state").toString();
        if (state != "IDLE" && state != "FINISH" && state != "FAILED") {
            finish("Bambu printer is busy. The current job was left running.");
            return;
        }
        if (uploaded) startPrint();
        else upload();
        return;
    }
    if (startSent && report.value("sequence_id").toVariant().toString() == sequence) {
        const auto result = report.value("result").toString();
        const auto code = report.value("err_code").toVariant().toString();
        if ((!code.isEmpty() && code != "0") || (!result.isEmpty() && result.compare("success", Qt::CaseInsensitive) != 0)) {
            finish("Bambu rejected start: " + QString::fromUtf8(QJsonDocument(report).toJson(QJsonDocument::Compact)));
        } else if (result.compare("success", Qt::CaseInsensitive) == 0) {
            finish();
        }
    }
}

void BambuSender::upload()
{
    timeout.stop();
    if (!archive.open() || archive.write(file.data) != file.data.size() || !archive.flush()) {
        finish("Cannot write the temporary Bambu plate archive.");
        return;
    }
    emit progress("Uploading Bambu plate over FTPS…");
    ftpUrl.setPath("/" + file.filename);
    connect(&transfer, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) finish("Cannot launch curl for the Bambu FTPS upload: " + transfer.errorString());
    });
    connect(&transfer, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        if (done) return;
        if (status != QProcess::NormalExit || code != 0) {
            finish("Bambu FTPS upload failed: " + QString::fromUtf8(transfer.readAllStandardError()).trimmed());
            return;
        }
        uploaded = true;
        emit progress("Bambu upload complete - Checking printer…");
        checkPrinter();
    });
    // Config goes over stdin so the LAN access code is absent from process arguments.
    // -q prevents a user's .curlrc from changing authentication or enabling logs.
    transfer.start(MIRDAIN_CURL_PATH, {"-q", "--config", "-"});
    const auto config = "url = " + curlQuote(ftpUrl.toString(QUrl::FullyEncoded))
        + "\nuser = " + curlQuote("bblp:" + accessCode)
        + "\nupload-file = " + curlQuote(archive.fileName())
        + "\ninsecure\nssl-reqd\ndisable-epsv\nftp-skip-pasv-ip\nnoproxy = \"*\""
          "\nconnect-timeout = 15\nmax-time = 180\nsilent\nshow-error\n";
    transfer.write(config.toUtf8());
    transfer.closeWriteChannel();
}

void BambuSender::startPrint()
{
    sequence = QString::number(QRandomGenerator::global()->bounded(1u, 0x7fffffffu));
    startSent = true;
    emit progress("Starting Bambu print…");
    timeout.start(30000);
    publish({{"print", QJsonObject{{"sequence_id", sequence}, {"command", "project_file"},
        {"param", file.gcodePath}, {"url", "ftp:///" + file.filename}, {"file", file.filename},
        {"md5", QString::fromLatin1(file.gcodeMd5).toUpper()},
        {"project_id", "0"}, {"profile_id", "0"}, {"task_id", "0"}, {"subtask_id", "0"},
        {"subtask_name", file.filename}, {"plate_idx", 0}, {"bed_type", "auto"},
        {"bed_leveling", true}, {"bed_levelling", true}, {"flow_cali", false}, {"vibration_cali", false},
        {"layer_inspect", false}, {"timelapse", false}, {"use_ams", useAms}, {"ams_mapping", amsMapping}}}});
}

void BambuSender::finish(const QString &error)
{
    if (done) return;
    done = true;
    timeout.stop();
    mqtt.disconnect(this);
    mqtt.disconnectFromHost();
    transfer.disconnect(this);
    if (transfer.state() != QProcess::NotRunning) transfer.kill();
    if (error.isEmpty()) emit completed();
    else emit failed(error);
}
