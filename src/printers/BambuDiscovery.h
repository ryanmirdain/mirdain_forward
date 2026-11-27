#pragma once

#include <QString>

#include <functional>

class QObject;

// Asks the Bambu Lab printer at host for its serial number with an SSDP M-SEARCH sent straight
// to it on UDP 2021 and 1990, the ports Bambu printers answer discovery on. The reply's USN
// header is the serial. done gets an empty string when host does not resolve or no printer
// answers within a few seconds. Callbacks run on context's thread and stop if it is destroyed.
void findBambuSerial(const QString &host, QObject *context, std::function<void(const QString &serial)> done);
