#include "platform/RemovableDrives.h"

#include <QDir>
#include <QMetaObject>

#include <qt_windows.h>
#include <winioctl.h>

QList<QStorageInfo> removableDrives()
{
    QList<QStorageInfo> drives;
    for (const auto &volume : QStorageInfo::mountedVolumes()) {
        if (!volume.isValid() || !volume.isReady() || volume.isReadOnly()) continue;
        if (GetDriveTypeW(reinterpret_cast<const wchar_t *>(QDir::toNativeSeparators(volume.rootPath()).utf16())) == DRIVE_REMOVABLE)
            drives.append(volume);
    }
    return drives;
}

void ejectDrive(const QStorageInfo &drive, QObject *context, std::function<void(const QString &)> done)
{
    // Lock, dismount, allow removal, then eject the volume, as Explorer's Eject does.
    const auto path = "\\\\.\\" + QDir::toNativeSeparators(drive.rootPath()).left(2);
    QString error;
    HANDLE volume = CreateFileW(reinterpret_cast<const wchar_t *>(path.utf16()), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (volume == INVALID_HANDLE_VALUE) error = qt_error_string(int(GetLastError()));
    else {
        DWORD bytes = 0;
        PREVENT_MEDIA_REMOVAL allow{FALSE};
        if (!DeviceIoControl(volume, FSCTL_LOCK_VOLUME, nullptr, 0, nullptr, 0, &bytes, nullptr))
            error = "The drive is in use by another app.";
        else if (!DeviceIoControl(volume, FSCTL_DISMOUNT_VOLUME, nullptr, 0, nullptr, 0, &bytes, nullptr)
            || !DeviceIoControl(volume, IOCTL_STORAGE_MEDIA_REMOVAL, &allow, sizeof allow, nullptr, 0, &bytes, nullptr)
            || !DeviceIoControl(volume, IOCTL_STORAGE_EJECT_MEDIA, nullptr, 0, nullptr, 0, &bytes, nullptr))
            error = qt_error_string(int(GetLastError()));
        CloseHandle(volume);
    }
    QMetaObject::invokeMethod(context, [done = std::move(done), error] { done(error); }, Qt::QueuedConnection);
}
