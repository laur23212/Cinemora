#pragma once

#include <QByteArray>
#include <QFile>
#include <QIODevice>
#include <QList>
#include <QString>

// Minimal ZIP support (stored and deflated entries, no zip64): enough for the archives Drift
// opens — .mogrt templates and .lottie animation bundles — and, for writing, Dafin's library
// exports.

namespace drift::zip {

struct Entry
{
    QString path;
    quint16 method = 0;
    quint32 compressedSize = 0;
    quint32 uncompressedSize = 0;
    quint32 localOffset = 0;
    bool isDir = false;
};

// True when the bytes start with a local file header — the cheap "is this a zip" probe.
bool looksLikeZip(const QByteArray &head);

// The central directory, minus entries whose path escapes the archive root.
QList<Entry> readEntries(QFile &file, QString *error);

bool extractEntry(QFile &file, const Entry &entry, QByteArray &outData);

// Dafin: writes stored (uncompressed) entries, which is all its own archives need — what goes in
// them is media that is compressed already. No zip64: every entry and the archive as a whole stay
// under 4 GiB, with at most 65535 entries; past that the writer fails instead of writing a file
// readers would misread. Names are written as UTF-8.
class Writer
{
public:
    // `device` must be open for writing and seekable (a QFile or QSaveFile).
    explicit Writer(QIODevice *device);

    bool addData(const QString &path, const QByteArray &data);
    // Streams the file in, so a large one is never held in memory.
    bool addFile(const QString &path, const QString &sourcePath);
    // Writes the central directory; nothing can be added afterwards.
    bool finish();

    QString errorString() const { return m_error; }

private:
    struct Record
    {
        QByteArray name;
        quint32 crc = 0;
        quint32 size = 0;
        quint32 offset = 0;
    };
    bool begin(const QString &path, Record &record);
    bool end(Record &record, quint32 crc, qint64 size);
    bool fail(const QString &message);

    QIODevice *m_device;
    QList<Record> m_records;
    quint16 m_dosTime = 0;
    quint16 m_dosDate = 0;
    bool m_failed = false;
    bool m_finished = false;
    QString m_error;
};

} // namespace drift::zip
