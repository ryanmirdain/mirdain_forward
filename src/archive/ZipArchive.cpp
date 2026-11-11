#include "archive/ZipArchive.h"

#include <QBuffer>
#include <QScopeGuard>

#include <limits>
#include <stdexcept>

ZipArchive::ZipArchive(QIODevice &device)
{
    zip.m_pIO_opaque = &device;
    zip.m_pRead = [](void *opaque, mz_uint64 offset, void *buffer, size_t size) -> size_t {
        auto &input = *static_cast<QIODevice *>(opaque);
        if (!input.seek(qint64(offset))) return 0;
        const auto count = input.read(static_cast<char *>(buffer), qint64(size));
        return count < 0 ? 0 : size_t(count);
    };
    if (!mz_zip_reader_init(&zip, device.size(), 0))
        throw std::runtime_error("Cannot open ZIP archive.");
}

ZipArchive::~ZipArchive()
{
    mz_zip_reader_end(&zip);
}

QByteArray ZipArchive::read(const QString &member)
{
    const auto name = member.toUtf8();
    const auto index = mz_zip_reader_locate_file(&zip, name.constData(), nullptr, MZ_ZIP_FLAG_CASE_SENSITIVE);
    mz_zip_archive_file_stat info;
    if (index < 0 || !mz_zip_reader_file_stat(&zip, mz_uint(index), &info))
        throw std::runtime_error(("Cannot find archive member: " + member).toStdString());
    if (info.m_uncomp_size > mz_uint64((std::numeric_limits<qsizetype>::max)()))
        throw std::runtime_error("Archive member is too large.");
    QByteArray bytes(qsizetype(info.m_uncomp_size), Qt::Uninitialized);
    if (!mz_zip_reader_extract_to_mem(&zip, mz_uint(index), bytes.data(), size_t(bytes.size()), 0))
        throw std::runtime_error(("Cannot read archive member: " + member).toStdString());
    return bytes;
}

QStringList ZipArchive::members()
{
    QStringList names;
    for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&zip); ++i) {
        const auto size = mz_zip_reader_get_filename(&zip, i, nullptr, 0);
        QByteArray name(size, Qt::Uninitialized);
        if (!mz_zip_reader_get_filename(&zip, i, name.data(), size))
            throw std::runtime_error("Cannot list ZIP archive members.");
        names.append(QString::fromUtf8(name.constData()));
    }
    return names;
}

QByteArray ZipArchive::withoutDirectory(const QString &directory)
{
    QByteArray bytes;
    QBuffer output(&bytes);
    output.open(QIODevice::WriteOnly);
    mz_zip_archive target{};
    target.m_pIO_opaque = &output;
    target.m_pWrite = [](void *opaque, mz_uint64 offset, const void *buffer, size_t size) -> size_t {
        auto &device = *static_cast<QIODevice *>(opaque);
        if (!device.seek(qint64(offset))) return 0;
        const auto count = device.write(static_cast<const char *>(buffer), qint64(size));
        return count < 0 ? 0 : size_t(count);
    };
    if (!mz_zip_writer_init(&target, 0))
        throw std::runtime_error("Cannot create the printer plate archive.");
    const auto cleanup = qScopeGuard([&] { mz_zip_writer_end(&target); });
    const auto names = members();
    for (qsizetype index = 0; index < names.size(); ++index) {
        const auto parts = QString(names[index]).replace('\\', '/').split('/', Qt::SkipEmptyParts);
        if (parts.contains(directory)) continue;
        // Clone compressed entries so G-code, checksums and metadata stay intact.
        if (!mz_zip_writer_add_from_zip_reader(&target, &zip, mz_uint(index)))
            throw std::runtime_error("Cannot copy a printer plate archive member.");
    }
    if (!mz_zip_writer_finalize_archive(&target))
        throw std::runtime_error("Cannot finalize the printer plate archive.");
    return bytes;
}
