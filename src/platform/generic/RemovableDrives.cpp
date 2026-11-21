#include "platform/RemovableDrives.h"

#include <QProcess>

QList<QStorageInfo> removableDrives()
{
    QList<QStorageInfo> drives;
    for (const auto &volume : QStorageInfo::mountedVolumes()) {
        const auto path = volume.rootPath();
        if (volume.isValid() && volume.isReady() && !volume.isReadOnly()
            && (path.startsWith("/media/") || path.startsWith("/run/media/")))
            drives.append(volume);
    }
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
    runEject("sh", {"-c", "udisksctl unmount -b \"$1\" && udisksctl power-off -b \"$1\"", "sh",
        QString::fromUtf8(drive.device())}, context, std::move(done));
}
