#pragma once

#include "app/online/library/AssetTypes.h"

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>

#include <optional>

namespace dafin::assets {

// The user's own files in the asset panels (Dafin):
//
//   lotties   "My Lotties" in Stickers   .json / .lottie animations
//   sounds    "My sounds" in Audio       audio files
//   titles    the Titles grid             Adobe Motion Graphics Templates (.mogrt)
//
// A file dropped on a panel is copied into this session's temporary folder and listed with a Save
// button; it disappears when Dafin closes unless the user saves it. Saved files are copied into
// the app data folder (user-library/<collection>/<id>/) and stay until the user deletes them.
// The file the user dropped is never touched. Media added to a project from here goes through
// the download manager like any online asset, so projects keep it even after it is deleted here.
class UserLibrary : public QObject
{
    Q_OBJECT
public:
    struct Collection
    {
        QString id;
        QString providerId;
        AssetKind kind;
        bool ownSidebarEntry;
    };
    static const QList<Collection> &collections();
    static const Collection *collection(const QString &id);
    static const Collection *collectionForProvider(const QString &providerId);
    static QString displayName(const QString &collection);

    struct Entry
    {
        QString id;            // content hash: the same file dropped twice is one entry
        QString title;
        QString dir;           // absolute folder that holds the entry's files
        QString file;          // the main file, relative to dir; empty for a remembered link
        QUrl link;             // a Lottie link remembered from earlier versions (no local file)
        bool saved = false;
        QDateTime added;
        QVariantMap meta;      // format, bytes, originalName; per collection: preview, poster,
                               // texts, text, preset, duration, width, height

        QString filePath() const;
        // A meta value that names a file inside dir ("preview", "poster"), made absolute.
        QString metaFile(const QString &key) const;
    };

    explicit UserLibrary(QString root, QObject *parent = nullptr);

    // Unsaved entries first, newest first; then saved ones, newest first.
    QList<Entry> entries(const QString &collection) const;
    std::optional<Entry> entry(const QString &collection, const QString &id) const;
    int unsavedCount(const QString &collection) const;

    struct ImportResult
    {
        QStringList added;      // entry ids, in the order the files were given
        int duplicates = 0;     // already in the library
        QStringList rejected;   // names of files that are not this collection's type
        QString error;          // the first problem worth telling the user about
    };
    ImportResult importFiles(const QString &collection, const QStringList &paths);
    // A file Dafin fetched for the user (a pasted Lottie link). Kept as a saved entry.
    std::optional<Entry> addFetchedFile(const QString &collection, const QString &path, const QString &title,
                                        const QUrl &source);

    bool save(const QString &collection, const QString &id, QString *error = nullptr);
    int saveAll(const QString &collection);
    bool remove(const QString &collection, const QString &id);

    static bool accepts(const QString &collection, const QString &path);
    static QStringList fileSuffixes(const QString &collection);

    // Links the old catalog kept under "My Lotties" move here, once.
    void migrateCatalogLinks(const QString &catalogPath);

    // Moving the library to another computer: every saved Lottie, sound and title in one
    // .dafinlib file (a ZIP with dafin-library.json and the entries' files), and the import that
    // adds such a file's entries as saved ones, skipping any already here.
    struct TransferResult
    {
        int count = 0;          // entries exported, or imported
        int duplicates = 0;     // import: already in the library
        int skipped = 0;        // import: entries that could not be restored
        int unsaved = 0;        // export: this session's unsaved files, which are left out
        QString error;          // empty when it worked
    };
    TransferResult exportArchive(const QString &path) const;
    TransferResult importArchive(const QString &path);
    int savedCount() const;
    static QString archiveSuffix() { return QStringLiteral("dafinlib"); }

    QString root() const { return m_root; }

signals:
    // Entries were added or removed.
    void changed(const QString &collection);
    // One entry changed in place (it was saved).
    void entryChanged(const QString &collection, const QString &id);

private:
    QString savedDir(const QString &collection) const;
    QString sessionDir(const QString &collection) const;
    QString manifestPath(const QString &collection) const;
    void loadManifest(const QString &collection);
    bool writeManifest(const QString &collection) const;
    bool contains(const QString &collection, const QString &id) const;
    // Copies `source` into `dir` and fills in what the panel shows. Empty with `error` when the
    // file is not usable for the collection.
    std::optional<Entry> makeEntry(const QString &collection, const QString &source, const QString &id,
                                   const QString &dir, QString *error) const;

    QString m_root;
    QHash<QString, QList<Entry>> m_saved;
    QHash<QString, QList<Entry>> m_session;
};

} // namespace dafin::assets
