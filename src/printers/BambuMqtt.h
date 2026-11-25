#pragma once

#include <QIODevice>
#include <QMqttClient>
#include <QString>

// Why a Bambu printer's MQTT connection failed, naming the address tried. A transport error
// (the connection itself) gives the socket's reason and points at the address; a rejected login
// points at the access code.
inline QString bambuMqttError(const QMqttClient &mqtt, QMqttClient::ClientError error)
{
    const auto address = QString("%1:%2").arg(mqtt.hostname()).arg(mqtt.port());
    if (error == QMqttClient::BadUsernameOrPassword || error == QMqttClient::NotAuthorized)
        return QString("Bambu at %1 rejected the LAN access code (%2). Check the access code and Developer Mode.")
            .arg(address).arg(int(error));
    const auto reason = mqtt.transport() ? mqtt.transport()->errorString() : QString();
    return QString("Couldn’t connect to the Bambu printer at %1 (MQTT error %2%3). Check the printer’s address and that it’s "
        "on this network, in LAN mode.").arg(address).arg(int(error)).arg(reason.isEmpty() ? QString() : ": " + reason);
}
