#include "core/base/ZipArchive.h"

#include <QDateTime>
#include <QObject>

#include <zlib.h>

namespace drift::zip {
namespace {

constexpr quint32 kZipLocalHeaderMagic = 0x04034b50;
constexpr quint32 kZipCentralDirMagic = 0x02014b50;
constexpr quint32 kZipEOCDMagic = 0x06054b50;

quint16 readU16(const char *p)
{
    const auto *b = reinterpret_cast<const uchar *>(p);
    return static_cast<quint16>(b[0] | (b[1] << 8));
}

quint32 readU32(const char *p)
{
    const auto *b = reinterpret_cast<const uchar *>(p);
    return static_cast<quint32>(b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24));
}

bool safeRelativePath(const QString &path)
{
    if (path.isEmpty() || path.startsWith(QLatin1Char('/')) || path.startsWith(QLatin1Char('\\')))
        return false;
    if (path.contains(QStringLiteral("..")) || path.contains(QLatin1Char(':')))
        return false;
    return true;
}

qint64 findEOCD(QFile &file)
{
    const qint64 fileSize = file.size();
    if (fileSize < 22)
        return -1;

    const qint64 scanLen = qMin(fileSize, static_cast<qint64>(65535 + 22));
    if (!file.seek(fileSize - scanLen))
        return -1;

    const QByteArray buf = file.read(scanLen);
    for (int i = buf.size() - 22; i >= 0; --i) {
        if (static_cast<uchar>(buf[i]) == 0x50
            && static_cast<uchar>(buf[i + 1]) == 0x4b
            && static_cast<uchar>(buf[i + 2]) == 0x05
            && static_cast<uchar>(buf[i + 3]) == 0x06) {
            return fileSize - scanLen + i;
        }
    }
    return -1;
}

QByteArray decompressRawDeflate(const QByteArray &compressed, quint32 uncompressedSize)
{
    z_stream strm;
    std::memset(&strm, 0, sizeof(strm));
    // -MAX_WBITS tells zlib to decode raw deflate without zlib/gzip headers
    if (inflateInit2(&strm, -MAX_WBITS) != Z_OK)
        return {};

    strm.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(compressed.constData()));
    strm.avail_in = static_cast<uInt>(compressed.size());

    QByteArray out;
    const int initialCapacity = uncompressedSize > 0 ? static_cast<int>(uncompressedSize)
                                                     : static_cast<int>(compressed.size() * 3);
    out.resize(initialCapacity);
    strm.next_out = reinterpret_cast<Bytef *>(out.data());
    strm.avail_out = static_cast<uInt>(out.size());

    int ret = inflate(&strm, Z_FINISH);
    if (ret == Z_STREAM_END) {
        out.resize(out.size() - strm.avail_out);
        inflateEnd(&strm);
        return out;
    }

    // Dynamic buffer loop if size estimation didn't match
    inflateEnd(&strm);
    std::memset(&strm, 0, sizeof(strm));
    if (inflateInit2(&strm, -MAX_WBITS) != Z_OK)
        return {};

    strm.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(compressed.constData()));
    strm.avail_in = static_cast<uInt>(compressed.size());

    out.clear();
    char buffer[32768];
    ret = Z_OK;
    while (ret == Z_OK || ret == Z_BUF_ERROR) {
        strm.next_out = reinterpret_cast<Bytef *>(buffer);
        strm.avail_out = sizeof(buffer);
        ret = inflate(&strm, Z_NO_FLUSH);
        const int have = sizeof(buffer) - strm.avail_out;
        if (have > 0)
            out.append(buffer, have);
        if (ret == Z_STREAM_END)
            break;
    }
    inflateEnd(&strm);
    return (ret == Z_STREAM_END || !out.isEmpty()) ? out : QByteArray{};
}

} // namespace

bool looksLikeZip(const QByteArray &head)
{
    return head.size() >= 4 && readU32(head.constData()) == kZipLocalHeaderMagic;
}

QList<Entry> readEntries(QFile &file, QString *error)
{
    const qint64 eocdPos = findEOCD(file);
    if (eocdPos < 0) {
        if (error)
            *error = QObject::tr("Not a valid ZIP archive");
        return {};
    }

    if (!file.seek(eocdPos))
        return {};

    const QByteArray eocdData = file.read(22);
    if (eocdData.size() < 22)
        return {};

    const quint16 totalEntries = readU16(eocdData.constData() + 10);
    const quint32 cdOffset = readU32(eocdData.constData() + 16);

    if (!file.seek(cdOffset))
        return {};

    QList<Entry> entries;
    entries.reserve(totalEntries);

    for (int i = 0; i < totalEntries; ++i) {
        const QByteArray cdHeader = file.read(46);
        if (cdHeader.size() < 46)
            break;

        if (readU32(cdHeader.constData()) != kZipCentralDirMagic)
            break;

        Entry entry;
        entry.method = readU16(cdHeader.constData() + 10);
        entry.compressedSize = readU32(cdHeader.constData() + 20);
        entry.uncompressedSize = readU32(cdHeader.constData() + 24);
        const quint16 fileNameLen = readU16(cdHeader.constData() + 28);
        const quint16 extraLen = readU16(cdHeader.constData() + 30);
        const quint16 commentLen = readU16(cdHeader.constData() + 32);
        entry.localOffset = readU32(cdHeader.constData() + 42);

        const QByteArray nameData = file.read(fileNameLen);
        entry.path = QString::fromUtf8(nameData);
        entry.isDir = entry.path.endsWith(QLatin1Char('/')) || entry.path.endsWith(QLatin1Char('\\'));

        // Skip extra and comment bytes in Central Directory
        if (extraLen + commentLen > 0)
            file.seek(file.pos() + extraLen + commentLen);

        if (safeRelativePath(entry.path))
            entries.append(entry);
    }

    return entries;
}

bool extractEntry(QFile &file, const Entry &entry, QByteArray &outData)
{
    if (entry.isDir)
        return true;

    if (!file.seek(entry.localOffset))
        return false;

    const QByteArray localHeader = file.read(30);
    if (localHeader.size() < 30 || readU32(localHeader.constData()) != kZipLocalHeaderMagic)
        return false;

    const quint16 localNameLen = readU16(localHeader.constData() + 26);
    const quint16 localExtraLen = readU16(localHeader.constData() + 28);

    const qint64 dataOffset = entry.localOffset + 30 + localNameLen + localExtraLen;
    if (!file.seek(dataOffset))
        return false;

    const QByteArray rawData = file.read(entry.compressedSize);
    if (rawData.size() != static_cast<int>(entry.compressedSize))
        return false;

    if (entry.method == 0) {
        // Stored
        outData = rawData;
        return true;
    } else if (entry.method == 8) {
        // Deflated
        outData = decompressRawDeflate(rawData, entry.uncompressedSize);
        return !outData.isEmpty() || entry.uncompressedSize == 0;
    }

    return false;
}

// ─── writer ──────────────────────────────────────────────────────────────────────────────────

namespace {

constexpr qint64 kZipMax32 = 0xFFFFFFFFLL;
constexpr quint16 kZipUtf8Flag = 0x0800;
constexpr quint16 kZipVersion = 20;

void appendU16(QByteArray &out, quint16 v)
{
    out.append(char(v & 0xff));
    out.append(char((v >> 8) & 0xff));
}

void appendU32(QByteArray &out, quint32 v)
{
    for (int shift = 0; shift < 32; shift += 8)
        out.append(char((v >> shift) & 0xff));
}

} // namespace

Writer::Writer(QIODevice *device)
    : m_device(device)
{
    const QDateTime now = QDateTime::currentDateTime();
    const QDate d = now.date();
    const QTime t = now.time();
    m_dosTime = quint16((t.hour() << 11) | (t.minute() << 5) | (t.second() / 2));
    m_dosDate = quint16((qMax(0, d.year() - 1980) << 9) | (d.month() << 5) | d.day());
}

bool Writer::fail(const QString &message)
{
    if (!m_failed)
        m_error = message;
    m_failed = true;
    return false;
}

bool Writer::begin(const QString &path, Record &record)
{
    if (m_failed || m_finished || !m_device || !m_device->isWritable())
        return fail(QObject::tr("The archive can't be written"));
    if (m_records.size() >= 0xFFFF)
        return fail(QObject::tr("Too many files for one archive"));
    record.name = path.toUtf8();
    if (record.name.isEmpty() || record.name.size() > 0xFFFF)
        return fail(QObject::tr("Invalid name in the archive: %1").arg(path));
    const qint64 offset = m_device->pos();
    if (offset > kZipMax32)
        return fail(QObject::tr("The archive would be larger than 4 GB"));
    record.offset = quint32(offset);

    QByteArray header;
    appendU32(header, kZipLocalHeaderMagic);
    appendU16(header, kZipVersion);
    appendU16(header, kZipUtf8Flag);
    appendU16(header, 0); // stored
    appendU16(header, m_dosTime);
    appendU16(header, m_dosDate);
    appendU32(header, 0); // crc, patched by end()
    appendU32(header, 0); // compressed size
    appendU32(header, 0); // uncompressed size
    appendU16(header, quint16(record.name.size()));
    appendU16(header, 0); // extra
    header.append(record.name);
    if (m_device->write(header) != header.size())
        return fail(m_device->errorString());
    return true;
}

bool Writer::end(Record &record, quint32 crc, qint64 size)
{
    if (size > kZipMax32 || m_device->pos() > kZipMax32)
        return fail(QObject::tr("The archive would be larger than 4 GB"));
    record.crc = crc;
    record.size = quint32(size);
    const qint64 after = m_device->pos();
    QByteArray patch;
    appendU32(patch, record.crc);
    appendU32(patch, record.size);
    appendU32(patch, record.size);
    if (!m_device->seek(qint64(record.offset) + 14) || m_device->write(patch) != patch.size()
        || !m_device->seek(after))
        return fail(m_device->errorString());
    m_records.append(record);
    return true;
}

bool Writer::addData(const QString &path, const QByteArray &data)
{
    Record record;
    if (!begin(path, record))
        return false;
    if (data.size() > kZipMax32)
        return fail(QObject::tr("The archive would be larger than 4 GB"));
    if (m_device->write(data) != data.size())
        return fail(m_device->errorString());
    const quint32 crc = quint32(crc32(crc32(0L, Z_NULL, 0),
                                      reinterpret_cast<const Bytef *>(data.constData()), uInt(data.size())));
    return end(record, crc, data.size());
}

bool Writer::addFile(const QString &path, const QString &sourcePath)
{
    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly))
        return fail(QObject::tr("Can't read %1").arg(sourcePath));
    if (source.size() > kZipMax32)
        return fail(QObject::tr("%1 is larger than 4 GB").arg(sourcePath));
    Record record;
    if (!begin(path, record))
        return false;
    uLong crc = crc32(0L, Z_NULL, 0);
    qint64 total = 0;
    QByteArray chunk;
    while (!source.atEnd()) {
        chunk = source.read(1 << 20);
        if (chunk.isEmpty() && source.error() != QFileDevice::NoError)
            return fail(QObject::tr("Can't read %1").arg(sourcePath));
        if (m_device->write(chunk) != chunk.size())
            return fail(m_device->errorString());
        crc = crc32(crc, reinterpret_cast<const Bytef *>(chunk.constData()), uInt(chunk.size()));
        total += chunk.size();
    }
    return end(record, quint32(crc), total);
}

bool Writer::finish()
{
    if (m_failed || m_finished)
        return !m_failed && m_finished;
    const qint64 directoryOffset = m_device->pos();
    QByteArray directory;
    for (const Record &r : std::as_const(m_records)) {
        appendU32(directory, kZipCentralDirMagic);
        appendU16(directory, kZipVersion); // made by: MS-DOS, 2.0
        appendU16(directory, kZipVersion);
        appendU16(directory, kZipUtf8Flag);
        appendU16(directory, 0); // stored
        appendU16(directory, m_dosTime);
        appendU16(directory, m_dosDate);
        appendU32(directory, r.crc);
        appendU32(directory, r.size);
        appendU32(directory, r.size);
        appendU16(directory, quint16(r.name.size()));
        appendU16(directory, 0); // extra
        appendU16(directory, 0); // comment
        appendU16(directory, 0); // disk
        appendU16(directory, 0); // internal attributes
        appendU32(directory, 0); // external attributes
        appendU32(directory, r.offset);
        directory.append(r.name);
    }
    if (directoryOffset + directory.size() > kZipMax32)
        return fail(QObject::tr("The archive would be larger than 4 GB"));
    QByteArray tail;
    appendU32(tail, kZipEOCDMagic);
    appendU16(tail, 0);
    appendU16(tail, 0);
    appendU16(tail, quint16(m_records.size()));
    appendU16(tail, quint16(m_records.size()));
    appendU32(tail, quint32(directory.size()));
    appendU32(tail, quint32(directoryOffset));
    appendU16(tail, 0); // comment
    if (m_device->write(directory) != directory.size() || m_device->write(tail) != tail.size())
        return fail(m_device->errorString());
    m_finished = true;
    return true;
}

} // namespace drift::zip
