#include "ZipArchive.h"

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
