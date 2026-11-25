#include "printers/BambuFilaments.h"
#include "printers/BambuMqtt.h"
#include "printers/PrinterSendJob.h"

#include <QJsonDocument>
#include <QMqttSubscription>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QUuid>

#include <algorithm>
#include <stdexcept>

static QString idString(const QJsonValue &value)
{
    return value.toVariant().toString();
}

// Status deltas may contain only one AMS/tray or a subset of its fields
// (mirdain_printer_client BambuBackend._merge_inventory).
static QJsonValue merge(const QJsonValue &previous, const QJsonValue &update)
{
    if (update.isObject()) {
        const auto object = update.toObject();
        // An ID-only tray explicitly has no material configuration.
        if (object.size() == 1 && object.contains("id")) return object;
        auto merged = previous.toObject();
        for (auto it = object.begin(); it != object.end(); ++it)
            merged.insert(it.key(), merge(merged.value(it.key()), it.value()));
        return merged;
    }
    const auto array = update.toArray();
    if (update.isArray() && !array.isEmpty() && std::all_of(array.begin(), array.end(),
            [](const QJsonValue &item) { return item.isObject() && item.toObject().contains("id"); })) {
        auto merged = previous.toArray();
        for (const auto &item : array) {
            const auto id = idString(item.toObject().value("id"));
            const auto existing = std::find_if(merged.begin(), merged.end(),
                [&id](const QJsonValue &entry) { return idString(entry.toObject().value("id")) == id; });
            if (existing == merged.end()) merged.append(item);
            else *existing = merge(*existing, item);
        }
        return merged;
    }
    return update;
}

BambuFilaments::BambuFilaments(const QJsonObject &connection, const QString &printerModel)
    : printerModel(printerModel)
{
    const auto url = PrinterSendJob::connectionUrl(connection, "mqtts", 8883);
    serial = connection.value("serial_number").toString().trimmed();
    const auto accessCode = connection.value("access_code").toString().trimmed();
    if (serial.isEmpty() || serial.contains('/') || serial.contains('+') || serial.contains('#') || accessCode.isEmpty())
        throw std::runtime_error("Bambu LAN requires serial_number and access_code from the printer's LAN settings.");
    mqtt.setHostname(url.host());
    mqtt.setPort(url.port());
    mqtt.setProtocolVersion(QMqttClient::MQTT_3_1_1);
    mqtt.setClientId("mirdain_" + QUuid::createUuid().toString(QUuid::Id128).left(12));
    mqtt.setUsername("bblp");
    mqtt.setPassword(accessCode);
    timeout.setSingleShot(true);
    connect(&timeout, &QTimer::timeout, this, [this] {
        finish("Bambu filament status timed out. Check the address, serial number, LAN access code, and Developer Mode.");
    });
}

void BambuFilaments::start()
{
    connect(&mqtt, &QMqttClient::connected, this, [this] {
        auto *subscription = mqtt.subscribe(QMqttTopicFilter("device/" + serial + "/report"));
        if (!subscription) {
            finish("Bambu report subscription failed.");
            return;
        }
        connect(subscription, &QMqttSubscription::stateChanged, this, [this](QMqttSubscription::SubscriptionState state) {
            if (state == QMqttSubscription::Subscribed)
                mqtt.publish(QMqttTopicName("device/" + serial + "/request"), QJsonDocument(QJsonObject{{"pushing",
                    QJsonObject{{"command", "pushall"}, {"sequence_id", "0"}, {"version", 1}, {"push_target", 1}}}})
                    .toJson(QJsonDocument::Compact), 0);
            else if (state == QMqttSubscription::Error) finish("Bambu report subscription was rejected.");
        });
    });
    connect(&mqtt, &QMqttClient::errorChanged, this, [this](QMqttClient::ClientError error) {
        if (error != QMqttClient::NoError)
            finish(bambuMqttError(mqtt, error));
    });
    connect(&mqtt, &QMqttClient::disconnected, this, [this] { finish("Bambu disconnected before reporting filaments."); });
    connect(&mqtt, &QMqttClient::messageReceived, this, [this](const QByteArray &bytes, const QMqttTopicName &) {
        receive(QJsonDocument::fromJson(bytes).object().value("print").toObject());
    });
    // Bambu LAN services use the printer's self-signed certificate.
    auto tls = QSslConfiguration::defaultConfiguration();
    tls.setPeerVerifyMode(QSslSocket::VerifyNone);
    timeout.start(15000);
    mqtt.connectToHostEncrypted(tls);
}

void BambuFilaments::receive(const QJsonObject &report)
{
    if (done) return;
    for (const QString &key : QStringList{"ams", "vt_tray", "vir_slot"})
        if (report.contains(key)) inventory.insert(key, merge(inventory.value(key), report.value(key)));
    // The pushall response is a full status report, including gcode_state and the inventory.
    if (report.contains("gcode_state") && (inventory.contains("ams") || inventory.contains("vt_tray") || inventory.contains("vir_slot"))) {
        emit completed(units(inventory, printerModel));
        finish();
    }
}

// Port of mirdain_printer_client BambuBackend._filament_units.
QJsonArray BambuFilaments::units(const QJsonObject &inventory, const QString &model)
{
    const auto filament = [](const QJsonObject &tray, const QJsonValue &extruder) {
        const auto color = tray.value("tray_color").toString();
        const auto material = tray.value("tray_info_idx").toString();
        return QJsonObject{{"slot_id", idString(tray.value("id"))},
            {"material_id", material.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(material)},
            {"color", (color.size() == 6 || color.size() == 8) && color != "00000000"
                ? QJsonValue("#" + color.left(6).toUpper()) : QJsonValue(QJsonValue::Null)},
            {"extruder", extruder}};
    };
    const auto hex = [](const QJsonValue &value) { return value.toString().toULongLong(nullptr, 16); };
    const bool p1 = model == "Bambu Lab P1P" || model == "Bambu Lab P1S";
    QList<QJsonObject> result;

    const auto amsReport = inventory.value("ams").toObject();
    for (const auto &entry : amsReport.value("ams").toArray()) {
        const auto ams = entry.toObject();
        const int amsId = idString(ams.value("id")).toInt();
        const bool hasInfo = ams.contains("info");
        const auto info = hex(ams.value("info"));
        // Bambu encodes AMS type in bits 0..3 and extruder in bits 8..11.
        QJsonValue extruder = QJsonValue::Null;
        if (hasInfo) {
            const int bits = (info >> 8) & 0xF;
            if (bits != 0xE && bits != 0xF) extruder = bits; // Shared/switchable or disconnected otherwise.
        } else if (p1 || model == "Bambu Lab X1C") {
            extruder = 0;
        }
        const int amsType = hasInfo ? int(info & 0xF) : 1;
        // AMS HT IDs start at 128; mixed AMS Lite uses separate presence bits.
        const int amsBit = amsType == 5 ? 12 : (amsId >= 128 ? 4 + amsId - 128 : amsId);
        if (amsReport.contains("ams_exist_bits") && !(hex(amsReport.value("ams_exist_bits")) & (1ull << amsBit)))
            continue;
        QJsonArray filaments;
        for (const auto &trayEntry : ams.value("tray").toArray()) {
            const auto tray = trayEntry.toObject();
            const int slot = idString(tray.value("id")).toInt();
            const int trayBit = amsType == 5 ? 24 + slot : (amsId >= 128 ? 16 + amsId - 128 : amsId * 4 + slot);
            if (amsReport.contains("tray_exist_bits")) {
                if (!(hex(amsReport.value("tray_exist_bits")) & (1ull << trayBit))) continue;
            } else if (tray.value("tray_type").toString().isEmpty()) {
                continue;
            }
            filaments.append(filament(tray, extruder));
        }
        result.append({{"id", QString::number(amsId)}, {"ams_type", amsType}, {"filaments", filaments}});
    }

    QJsonArray external;
    if (inventory.contains("vir_slot")) {
        for (const auto &entry : inventory.value("vir_slot").toArray()) {
            const auto tray = entry.toObject();
            if (tray.value("tray_type").toString().isEmpty()) continue;
            const auto id = idString(tray.value("id"));
            external.append(filament(tray, p1 ? QJsonValue(0)
                : id == "255" ? QJsonValue(0) : id == "254" ? QJsonValue(1) : QJsonValue(QJsonValue::Null)));
        }
    } else {
        const auto tray = inventory.value("vt_tray").toObject();
        if (!tray.value("tray_type").toString().isEmpty()) external.append(filament(tray, 0));
    }
    if (!external.isEmpty()) result.append({{"id", QJsonValue::Null}, {"filaments", external}});

    const auto unitKey = [](const QJsonObject &unit) {
        const auto id = unit.value("id");
        return id.isNull() ? -1 : id.toString().toInt();
    };
    std::sort(result.begin(), result.end(), [&](const QJsonObject &a, const QJsonObject &b) { return unitKey(a) < unitKey(b); });
    QJsonArray sorted;
    for (auto unit : result) {
        QList<QJsonObject> filaments;
        for (const auto &item : unit.value("filaments").toArray()) filaments.append(item.toObject());
        std::sort(filaments.begin(), filaments.end(), [](const QJsonObject &a, const QJsonObject &b) {
            return a.value("slot_id").toString().toInt() < b.value("slot_id").toString().toInt();
        });
        QJsonArray ordered;
        for (const auto &item : filaments) ordered.append(item);
        unit.insert("filaments", ordered);
        sorted.append(unit);
    }
    return sorted;
}

void BambuFilaments::finish(const QString &error)
{
    if (done) return;
    done = true;
    timeout.stop();
    mqtt.disconnect(this);
    mqtt.disconnectFromHost();
    if (!error.isEmpty()) emit failed(error);
}
