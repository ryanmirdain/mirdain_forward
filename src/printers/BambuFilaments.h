#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QMqttClient>
#include <QTimer>

// Reads a Bambu printer's loaded spools (AMS units and external spools) over LAN MQTT.
// The result matches mirdain_printer_client's telemetry and mirdain_server's
// PrinterFilamentUnit list: [{"id": "0" | null, "ams_type": 1, "filaments":
// [{"slot_id": "0", "material_id": "GFA00" | null, "color": "#RRGGBB" | null, "extruder": 0 | null}]}].
class BambuFilaments : public QObject {
    Q_OBJECT

public:
    // printerModel is the slicer's printer_model, e.g. "Bambu Lab P1S"; it fills in
    // extruders for firmware that omits AMS info bits.
    BambuFilaments(const QJsonObject &connection, const QString &printerModel);
    void start();
    static QJsonArray units(const QJsonObject &inventory, const QString &printerModel);

signals:
    void completed(const QJsonArray &units);
    void failed(const QString &message);

private:
    QMqttClient mqtt;
    QTimer timeout;
    QString serial;
    QString printerModel;
    QJsonObject inventory; // Merged "ams", "vt_tray", and "vir_slot" reports.
    bool done = false;

    void receive(const QJsonObject &report);
    void finish(const QString &error = {});
};
