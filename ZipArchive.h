#pragma once

#include <QByteArray>
#include <QIODevice>
#include <QStringList>
#include <miniz.h>

// Reads members into memory; archive paths are never extracted to the filesystem.
class ZipArchive {
public:
    explicit ZipArchive(QIODevice &device);
    ~ZipArchive();
    QByteArray read(const QString &member);
    QStringList members();

private:
    Q_DISABLE_COPY_MOVE(ZipArchive)
    mz_zip_archive zip{};
};
