#pragma once

#include "printers/PrinterSendJob.h"

class QNetworkReply;

// Reports an HTTP upload's percentage, MiB, and KiB/s through the job at most once per
// second, then that the upload finished and the printer's response is pending.
void reportUploadProgress(PrinterSendJob *job, QNetworkReply *reply, const QString &label, qint64 size);
