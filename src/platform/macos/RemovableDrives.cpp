#include "platform/RemovableDrives.h"

#include <QProcess>

#include <CoreFoundation/CoreFoundation.h>
#include <DiskArbitration/DiskArbitration.h>

static bool flag(CFDictionaryRef description, CFStringRef key)
{
    const auto value = CFDictionaryGetValue(description, key);
    return value && CFBooleanGetValue(static_cast<CFBooleanRef>(value));
}

QList<QStorageInfo> removableDrives()
{
    QList<QStorageInfo> drives;
    DASessionRef session = DASessionCreate(kCFAllocatorDefault);
    for (const auto &volume : QStorageInfo::mountedVolumes()) {
        if (!volume.isValid() || !volume.isReady() || volume.isReadOnly() || volume.isRoot()) continue;
        const auto path = volume.rootPath().toUtf8();
        CFURLRef url = CFURLCreateFromFileSystemRepresentation(kCFAllocatorDefault,
            reinterpret_cast<const UInt8 *>(path.constData()), path.size(), true);
        DADiskRef disk = DADiskCreateFromVolumePath(kCFAllocatorDefault, session, url);
        CFRelease(url);
        if (!disk) continue; // Network and synthetic volumes have no disk.
        if (CFDictionaryRef description = DADiskCopyDescription(disk)) {
            const auto protocol = static_cast<CFStringRef>(CFDictionaryGetValue(description, kDADiskDescriptionDeviceProtocolKey));
            const bool diskImage = protocol && CFStringCompare(protocol, CFSTR("Virtual Interface"), 0) == kCFCompareEqualTo;
            const bool removable = flag(description, kDADiskDescriptionMediaRemovableKey)
                || !flag(description, kDADiskDescriptionDeviceInternalKey);
            if (removable && !diskImage) drives.append(volume);
            CFRelease(description);
        }
        CFRelease(disk);
    }
    CFRelease(session);
    return drives;
}

static void runEject(const QString &program, const QStringList &arguments, QObject *context,
    std::function<void(const QString &)> done)
{
    auto *process = new QProcess(context);
    process->setProcessChannelMode(QProcess::MergedChannels);
    QObject::connect(process, &QProcess::finished, context, [process, done](int code, QProcess::ExitStatus status) {
        process->deleteLater();
        done(status == QProcess::NormalExit && code == 0 ? QString{}
            : QString::fromLocal8Bit(process->readAll()).trimmed());
    });
    QObject::connect(process, &QProcess::errorOccurred, context, [process, done](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        process->deleteLater();
        done(process->errorString());
    });
    process->start(program, arguments);
}

void ejectDrive(const QStorageInfo &drive, QObject *context, std::function<void(const QString &)> done)
{
    // diskutil unmounts every volume on the physical device before ejecting it.
    runEject("/usr/sbin/diskutil", {"eject", QString::fromUtf8(drive.device())}, context, std::move(done));
}
