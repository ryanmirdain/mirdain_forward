#include "printers/UploadProgress.h"

#include <QElapsedTimer>
#include <QNetworkReply>

void reportUploadProgress(PrinterSendJob *job, QNetworkReply *reply, const QString &label, qint64 size)
{
    emit job->progress(QString("Uploading %1… 0% · 0.0/%2 MiB · 0 KiB/s").arg(label).arg(size / 1048576.0, 0, 'f', 1));
    QElapsedTimer clock;
    clock.start();
    QObject::connect(reply, &QNetworkReply::uploadProgress, job,
        [job, label, clock, lastBytes = qint64(0)](qint64 sent, qint64 total) mutable {
            if (total <= 0) return;
            if (sent == total) {
                emit job->progress(label + " upload complete - Waiting for print response…");
                return;
            }
            const auto elapsed = clock.elapsed();
            if (elapsed < 1000) return;
            const double kibPerSecond = qMax<qint64>(0, sent - lastBytes) * 1000.0 / elapsed / 1024;
            lastBytes = sent;
            clock.restart();
            emit job->progress(QString("Uploading %1… %2% · %3/%4 MiB · %5 KiB/s")
                .arg(label).arg(int(sent * 100 / total)).arg(sent / 1048576.0, 0, 'f', 1)
                .arg(total / 1048576.0, 0, 'f', 1).arg(kibPerSecond, 0, 'f', 0));
        });
}
