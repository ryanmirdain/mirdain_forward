#pragma once

#include <QList>
#include <QStorageInfo>
#include <QString>

#include <functional>

class QObject;

// Mounted, writable USB drives and SD cards, excluding disk images and network volumes.
QList<QStorageInfo> removableDrives();

// Unmounts and ejects drive's device, then calls done on context's thread with an
// empty string on success or the reason it could not be ejected.
void ejectDrive(const QStorageInfo &drive, QObject *context, std::function<void(const QString &error)> done);
