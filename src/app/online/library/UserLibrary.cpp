#include "app/online/library/UserLibrary.h"

#include "app/online/library/MogrtInfo.h"
#include "app/online/library/SessionFiles.h"
#include "core/base/ZipArchive.h"
#include "core/project/sources/DotLottie.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>

#include <algorithm>

namespace dafin::assets {

namespace {

constexpr qint64 kMaxLottieBytes = 64LL * 1024 * 1024;
constexpr qint64 kMaxFileBytes = 2LL * 1024 * 1024 * 1024;
const QString kLotties = QStringLiteral("lotties");
const QString kSounds = QStringLiteral("sounds");
const QString kTitles = QStringLiteral("titles");

QString contentId(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha1);
    if (!hash.addData(&f))
        return {};
    return QString::fromLatin1(hash.result().toHex().left(16));
}

QString linkId(const QUrl &url)
{
    return QStringLiteral("link-")
           + QString::fromLatin1(QCryptographicHash::hash(url.toEncoded(), QCryptographicHash::Sha1).toHex().left(12));
}

// "big_red-heart (2)" → "Big red heart (2)"
QString titleFromFileName(const QString &path)
{
    QString title = QFileInfo(path).completeBaseName();
    title.replace(QLatin1Char('_'), QLatin1Char(' '));
    title.replace(QRegularExpression(QStringLiteral("(?<=\\S)-(?=\\S)")), QStringLiteral(" "));
    title = title.simplified();
    if (!title.isEmpty())
        title[0] = title.at(0).toUpper();
    return title.isEmpty() ? QObject::tr("Untitled") : title;
}

// Keeps letters of any script; drops only what file systems or shells trip over.
QString storedFileName(const QString &path)
{
    const QFileInfo info(path);
    QString base = info.completeBaseName();
    base.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|\\x00-\\x1f]+")), QStringLiteral("_"));
    base = base.simplified().left(80);
    if (base.isEmpty() || base.startsWith(QLatin1Char('.')))
        base = QStringLiteral("file") + base;
    return base + QLatin1Char('.') + info.suffix().toLower();
}

bool copyFileTo(const QString &source, const QString &dest)
{
    QFile::remove(dest);
    if (!QFile::copy(source, dest))
        return false;
    // Files copied off read-only media keep that flag; the library has to be able to delete them.
    QFile::setPermissions(dest, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadUser | QFile::WriteUser);
    return true;
}

bool copyDirectory(const QString &from, const QString &to)
{
    QDir().mkpath(to);
    QDirIterator it(from, QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);
    const QDir base(from);
    while (it.hasNext()) {
        const QString source = it.next();
        const QString dest = to + QLatin1Char('/') + base.relativeFilePath(source);
        QDir().mkpath(QFileInfo(dest).absolutePath());
        if (!copyFileTo(source, dest))
            return false;
    }
    return true;
}

bool isLottieDocument(const QJsonObject &o)
{
    return o.contains(QStringLiteral("layers")) && (o.contains(QStringLiteral("v")) || o.contains(QStringLiteral("fr")));
}

QString sizeLabel(qint64 bytes)
{
    if (bytes >= 1024 * 1024)
        return QStringLiteral("%1 MB").arg(double(bytes) / (1024.0 * 1024.0), 0, 'f', 1);
    return QStringLiteral("%1 KB").arg(qMax<qint64>(1, (bytes + 1023) / 1024));
}

const QString kArchiveFormat = QStringLiteral("dafin-library");
const QString kArchiveManifest = QStringLiteral("dafin-library.json");
constexpr int kArchiveVersion = 1;

// An entry id as the library makes them: a content hash, or "link-" and a hash.
bool validEntryId(const QString &id)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9_-]{1,64}$"));
    return pattern.match(id).hasMatch();
}

// A path inside an entry's folder, as an archive names it: relative, and never above the folder.
bool safeRelativePath(const QString &path)
{
    if (path.isEmpty() || QDir::isAbsolutePath(path) || path.startsWith(QLatin1Char('/'))
        || path.contains(QLatin1Char('\\')) || path.contains(QLatin1Char(':')))
        return false;
    const QStringList parts = path.split(QLatin1Char('/'));
    return std::none_of(parts.cbegin(), parts.cend(), [](const QString &part) {
        return part.isEmpty() || part == QLatin1String(".") || part == QLatin1String("..");
    });
}

QJsonObject entryToJson(const UserLibrary::Entry &e)
{
    QJsonObject o{{QStringLiteral("id"), e.id},
                  {QStringLiteral("title"), e.title},
                  {QStringLiteral("added"), e.added.toString(Qt::ISODate)},
                  {QStringLiteral("meta"), QJsonObject::fromVariantMap(e.meta)}};
    if (!e.file.isEmpty())
        o.insert(QStringLiteral("file"), e.file);
    if (e.link.isValid())
        o.insert(QStringLiteral("link"), e.link.toString());
    return o;
}

} // namespace

// ─── collections ─────────────────────────────────────────────────────────────────────────────

const QList<UserLibrary::Collection> &UserLibrary::collections()
{
    static const QList<Collection> list{
        {kLotties, QStringLiteral("my-lotties"), AssetKind::Sticker, true},
        {kSounds, QStringLiteral("my-sounds"), AssetKind::SoundEffect, true},
        // Imported templates sit in the main Titles grid; they get no category of their own.
        {kTitles, QStringLiteral("my-titles"), AssetKind::Title, false},
    };
    return list;
}

const UserLibrary::Collection *UserLibrary::collection(const QString &id)
{
    for (const Collection &c : collections()) {
        if (c.id == id)
            return &c;
    }
    return nullptr;
}

const UserLibrary::Collection *UserLibrary::collectionForProvider(const QString &providerId)
{
    for (const Collection &c : collections()) {
        if (c.providerId == providerId)
            return &c;
    }
    return nullptr;
}

QString UserLibrary::displayName(const QString &collection)
{
    if (collection == kLotties)
        return tr("My Lotties");
    if (collection == kSounds)
        return tr("My sounds");
    if (collection == kTitles)
        return tr("My titles");
    return collection;
}

QStringList UserLibrary::fileSuffixes(const QString &collection)
{
    if (collection == kLotties)
        return {QStringLiteral("json"), QStringLiteral("lottie")};
    if (collection == kSounds)
        return {QStringLiteral("mp3"), QStringLiteral("wav"), QStringLiteral("ogg"), QStringLiteral("oga"),
                QStringLiteral("opus"), QStringLiteral("flac"), QStringLiteral("m4a"), QStringLiteral("aac"),
                QStringLiteral("wma"), QStringLiteral("aif"), QStringLiteral("aiff"), QStringLiteral("caf"),
                QStringLiteral("mka"), QStringLiteral("ac3"), QStringLiteral("amr")};
    if (collection == kTitles)
        return {QStringLiteral("mogrt")};
    return {};
}

bool UserLibrary::accepts(const QString &collection, const QString &path)
{
    const QFileInfo info(path);
    return info.isFile() && fileSuffixes(collection).contains(info.suffix().toLower());
}

// ─── entries ─────────────────────────────────────────────────────────────────────────────────

QString UserLibrary::Entry::filePath() const
{
    return file.isEmpty() ? QString() : dir + QLatin1Char('/') + file;
}

QString UserLibrary::Entry::metaFile(const QString &key) const
{
    const QString rel = meta.value(key).toString();
    return rel.isEmpty() ? QString() : dir + QLatin1Char('/') + rel;
}

UserLibrary::UserLibrary(QString root, QObject *parent)
    : QObject(parent)
    , m_root(std::move(root))
{
    for (const Collection &c : collections())
        loadManifest(c.id);
}

QString UserLibrary::savedDir(const QString &collection) const
{
    return m_root + QLatin1Char('/') + collection;
}

QString UserLibrary::sessionDir(const QString &collection) const
{
    return session::root() + QStringLiteral("/user-library/") + collection;
}

QString UserLibrary::manifestPath(const QString &collection) const
{
    return savedDir(collection) + QStringLiteral("/library.json");
}

void UserLibrary::loadManifest(const QString &collection)
{
    QList<Entry> &list = m_saved[collection];
    list.clear();
    QFile f(manifestPath(collection));
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonArray items = QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("items")).toArray();
    for (const QJsonValue &v : items) {
        const QJsonObject o = v.toObject();
        Entry e;
        e.id = o.value(QStringLiteral("id")).toString();
        if (e.id.isEmpty() || e.id.contains(QLatin1Char('/')) || e.id.contains(QLatin1Char('\\')) || e.id.startsWith(QLatin1Char('.')))
            continue;
        e.title = o.value(QStringLiteral("title")).toString();
        e.file = QFileInfo(o.value(QStringLiteral("file")).toString()).fileName();
        e.link = QUrl(o.value(QStringLiteral("link")).toString());
        e.added = QDateTime::fromString(o.value(QStringLiteral("added")).toString(), Qt::ISODate);
        e.meta = o.value(QStringLiteral("meta")).toObject().toVariantMap();
        e.dir = savedDir(collection) + QLatin1Char('/') + e.id;
        e.saved = true;
        // A file removed by hand outside Dafin: drop the entry rather than show a dead card.
        if (!e.file.isEmpty() && !QFileInfo::exists(e.filePath()))
            continue;
        if (e.file.isEmpty() && !e.link.isValid())
            continue;
        list.append(e);
    }
}

bool UserLibrary::writeManifest(const QString &collection) const
{
    QDir().mkpath(savedDir(collection));
    QJsonArray items;
    for (const Entry &e : m_saved.value(collection))
        items.append(entryToJson(e));
    QSaveFile out(manifestPath(collection));
    if (!out.open(QIODevice::WriteOnly))
        return false;
    out.write(QJsonDocument(QJsonObject{{QStringLiteral("version"), 1}, {QStringLiteral("items"), items}})
                  .toJson(QJsonDocument::Indented));
    return out.commit();
}

QList<UserLibrary::Entry> UserLibrary::entries(const QString &collection) const
{
    QList<Entry> out = m_session.value(collection);
    out.append(m_saved.value(collection));
    return out;
}

std::optional<UserLibrary::Entry> UserLibrary::entry(const QString &collection, const QString &id) const
{
    for (const Entry &e : m_session.value(collection)) {
        if (e.id == id)
            return e;
    }
    for (const Entry &e : m_saved.value(collection)) {
        if (e.id == id)
            return e;
    }
    return std::nullopt;
}

bool UserLibrary::contains(const QString &collection, const QString &id) const
{
    return entry(collection, id).has_value();
}

int UserLibrary::unsavedCount(const QString &collection) const
{
    return int(m_session.value(collection).size());
}

std::optional<UserLibrary::Entry> UserLibrary::makeEntry(const QString &collection, const QString &source,
                                                         const QString &id, const QString &dir,
                                                         QString *error) const
{
    const QFileInfo info(source);
    const QString name = info.fileName();
    if (info.size() <= 0 || info.size() > kMaxFileBytes) {
        *error = tr("%1 is empty or too large.").arg(name);
        return std::nullopt;
    }

    Entry e;
    e.id = id;
    e.dir = dir;
    e.title = titleFromFileName(source);
    e.added = QDateTime::currentDateTimeUtc();
    e.meta.insert(QStringLiteral("originalName"), name);
    e.meta.insert(QStringLiteral("bytes"), info.size());
    e.meta.insert(QStringLiteral("format"), info.suffix().toUpper());
    e.meta.insert(QStringLiteral("sizeLabel"), sizeLabel(info.size()));

    QDir(dir).removeRecursively();
    if (!QDir().mkpath(dir)) {
        *error = tr("Can't write to %1.").arg(QDir::toNativeSeparators(dir));
        return std::nullopt;
    }
    const auto fail = [&dir, error](const QString &message) {
        QDir(dir).removeRecursively();
        *error = message;
        return std::nullopt;
    };

    e.file = storedFileName(source);
    if (!copyFileTo(source, e.filePath()))
        return fail(tr("Couldn't copy %1.").arg(name));

    if (collection == kLotties) {
        if (info.suffix().compare(QLatin1String("lottie"), Qt::CaseInsensitive) == 0) {
            // The preview renderer reads plain Lottie JSON, so the bundle is unpacked once.
            QString unpackError;
            const QStringList animations = drift::unpackDotLottie(e.filePath(), dir + QStringLiteral("/unpacked"),
                                                                  &unpackError);
            if (animations.isEmpty())
                return fail(tr("%1 is not a Lottie animation.").arg(name));
            e.meta.insert(QStringLiteral("preview"), QDir(dir).relativeFilePath(animations.first()));
        } else {
            if (info.size() > kMaxLottieBytes)
                return fail(tr("%1 is too large for a Lottie animation.").arg(name));
            QFile f(e.filePath());
            const QJsonObject doc = f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object()
                                                                : QJsonObject();
            if (!isLottieDocument(doc))
                return fail(tr("%1 is not a Lottie animation. On LottieFiles, download the \"Lottie JSON\" file.").arg(name));
            e.meta.insert(QStringLiteral("preview"), e.file);
            e.meta.insert(QStringLiteral("width"), doc.value(QStringLiteral("w")).toInt());
            e.meta.insert(QStringLiteral("height"), doc.value(QStringLiteral("h")).toInt());
            const double fps = doc.value(QStringLiteral("fr")).toDouble();
            if (fps > 0)
                e.meta.insert(QStringLiteral("duration"),
                              (doc.value(QStringLiteral("op")).toDouble() - doc.value(QStringLiteral("ip")).toDouble()) / fps);
            const QString animationName = doc.value(QStringLiteral("nm")).toString().trimmed();
            // Exports often carry a useful name where the file is called "data.json".
            if (!animationName.isEmpty()
                && QFileInfo(source).completeBaseName().compare(QLatin1String("data"), Qt::CaseInsensitive) == 0)
                e.title = animationName;
        }
    } else if (collection == kTitles) {
        QString readError;
        const std::optional<mogrt::Info> tmpl = mogrt::read(e.filePath(), &readError);
        if (!tmpl)
            return fail(readError.isEmpty() ? tr("%1 is not a Motion Graphics Template.").arg(name) : readError);
        if (!tmpl->title.isEmpty())
            e.title = tmpl->title;
        if (!tmpl->posterEntry.isEmpty()) {
            const QString poster = QStringLiteral("poster.") + QFileInfo(tmpl->posterEntry).suffix().toLower();
            if (mogrt::extract(e.filePath(), tmpl->posterEntry, dir + QLatin1Char('/') + poster, 32LL * 1024 * 1024))
                e.meta.insert(QStringLiteral("poster"), poster);
        }
        if (!tmpl->previewEntry.isEmpty()) {
            const QString preview = QStringLiteral("preview.") + QFileInfo(tmpl->previewEntry).suffix().toLower();
            if (mogrt::extract(e.filePath(), tmpl->previewEntry, dir + QLatin1Char('/') + preview))
                e.meta.insert(QStringLiteral("previewVideo"), preview);
        }
        QVariantList texts;
        for (const mogrt::TextField &f : tmpl->texts)
            texts.append(QVariantMap{{QStringLiteral("label"), f.label}, {QStringLiteral("text"), f.text}});
        e.meta.insert(QStringLiteral("texts"), texts);
        e.meta.insert(QStringLiteral("text"), mogrt::titleText(*tmpl));
        e.meta.insert(QStringLiteral("preset"), mogrt::titlePreset(*tmpl));
        e.meta.insert(QStringLiteral("duration"), tmpl->durationSeconds);
        e.meta.insert(QStringLiteral("width"), tmpl->width);
        e.meta.insert(QStringLiteral("height"), tmpl->height);
        e.meta.insert(QStringLiteral("authorApp"), tmpl->authorApp);
    }
    return e;
}

UserLibrary::ImportResult UserLibrary::importFiles(const QString &collection, const QStringList &paths)
{
    ImportResult result;
    if (!this->collection(collection))
        return result;
    QList<Entry> added;
    for (const QString &path : paths) {
        const QFileInfo info(path);
        if (!accepts(collection, path)) {
            result.rejected.append(info.fileName());
            continue;
        }
        const QString id = contentId(path);
        if (id.isEmpty()) {
            if (result.error.isEmpty())
                result.error = tr("Couldn't read %1.").arg(info.fileName());
            continue;
        }
        const bool dup = contains(collection, id)
                         || std::any_of(added.cbegin(), added.cend(), [&id](const Entry &e) { return e.id == id; });
        if (dup) {
            ++result.duplicates;
            continue;
        }
        QString error;
        const std::optional<Entry> e = makeEntry(collection, path, id, sessionDir(collection) + QLatin1Char('/') + id, &error);
        if (!e) {
            result.rejected.append(info.fileName());
            if (result.error.isEmpty())
                result.error = error;
            continue;
        }
        added.append(*e);
        result.added.append(id);
    }
    if (!added.isEmpty()) {
        // Newest first, keeping the dropped order among themselves.
        QList<Entry> &session = m_session[collection];
        session = added + session;
        emit changed(collection);
    }
    return result;
}

std::optional<UserLibrary::Entry> UserLibrary::addFetchedFile(const QString &collection, const QString &path,
                                                              const QString &title, const QUrl &source)
{
    if (!this->collection(collection))
        return std::nullopt;
    const QString id = contentId(path);
    if (id.isEmpty())
        return std::nullopt;
    if (const std::optional<Entry> existing = entry(collection, id)) {
        if (!existing->saved)
            save(collection, id);
        return entry(collection, id);
    }
    QString error;
    std::optional<Entry> e = makeEntry(collection, path, id, savedDir(collection) + QLatin1Char('/') + id, &error);
    if (!e)
        return std::nullopt;
    if (!title.trimmed().isEmpty())
        e->title = title.trimmed();
    if (source.isValid())
        e->meta.insert(QStringLiteral("source"), source.toString());
    e->saved = true;
    m_saved[collection].prepend(*e);
    writeManifest(collection);
    emit changed(collection);
    return e;
}

bool UserLibrary::save(const QString &collection, const QString &id, QString *error)
{
    QList<Entry> &session = m_session[collection];
    const auto it = std::find_if(session.begin(), session.end(), [&id](const Entry &e) { return e.id == id; });
    if (it == session.end())
        return contains(collection, id); // already saved
    Entry e = *it;
    const QString dest = savedDir(collection) + QLatin1Char('/') + id;
    QDir(dest).removeRecursively();
    // Copied, not moved: cards and previews of this session may still point at the session copy,
    // which goes away with the rest of the session folder when Dafin closes.
    if (!copyDirectory(e.dir, dest)) {
        QDir(dest).removeRecursively();
        if (error)
            *error = tr("Couldn't save %1 to %2.").arg(e.title, QDir::toNativeSeparators(savedDir(collection)));
        return false;
    }
    e.dir = dest;
    e.saved = true;
    session.erase(it);
    m_saved[collection].prepend(e);
    if (!writeManifest(collection)) {
        if (error)
            *error = tr("Couldn't write the library list in %1.").arg(QDir::toNativeSeparators(savedDir(collection)));
    }
    emit entryChanged(collection, id);
    return true;
}

int UserLibrary::saveAll(const QString &collection)
{
    const QList<Entry> pending = m_session.value(collection);
    int saved = 0;
    // Oldest first, so the saved list keeps the order the files were added in.
    for (auto it = pending.crbegin(); it != pending.crend(); ++it) {
        if (save(collection, it->id))
            ++saved;
    }
    return saved;
}

bool UserLibrary::remove(const QString &collection, const QString &id)
{
    for (QList<Entry> *list : {&m_session[collection], &m_saved[collection]}) {
        for (int i = 0; i < list->size(); ++i) {
            if (list->at(i).id != id)
                continue;
            const Entry e = list->takeAt(i);
            if (!e.dir.isEmpty())
                QDir(e.dir).removeRecursively();
            if (e.saved)
                writeManifest(collection);
            emit changed(collection);
            return true;
        }
    }
    return false;
}

void UserLibrary::migrateCatalogLinks(const QString &catalogPath)
{
    QSettings settings;
    const QString flag = QStringLiteral("UserLibrary/catalogLinksMigrated");
    if (settings.value(flag, false).toBool())
        return;
    QFile f(catalogPath);
    if (!f.open(QIODevice::ReadOnly))
        return; // nothing to move yet; try again next start
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject())
        return; // a catalog being edited by hand: leave it for a later start
    bool any = false;
    for (const QJsonValue &v : doc.object().value(QStringLiteral("sources")).toArray()) {
        const QJsonObject source = v.toObject();
        if (source.value(QStringLiteral("id")).toString() != QLatin1String("my-lotties"))
            continue;
        for (const QJsonValue &itemValue : source.value(QStringLiteral("items")).toArray()) {
            const QJsonObject item = itemValue.toObject();
            const QUrl url(item.value(QStringLiteral("url")).toString());
            if (!url.isValid() || (url.scheme() != QLatin1String("https") && url.scheme() != QLatin1String("http")))
                continue;
            const QString id = linkId(url);
            if (contains(kLotties, id))
                continue;
            Entry e;
            e.id = id;
            e.title = item.value(QStringLiteral("title")).toString(QFileInfo(url.path()).completeBaseName());
            e.link = url;
            e.saved = true;
            e.added = QDateTime::currentDateTimeUtc();
            e.dir = savedDir(kLotties) + QLatin1Char('/') + id;
            e.meta.insert(QStringLiteral("format"), QStringLiteral("LINK"));
            m_saved[kLotties].append(e);
            any = true;
        }
    }
    if (any) {
        writeManifest(kLotties);
        emit changed(kLotties);
    }
    settings.setValue(flag, true);
}

// ─── moving the library to another computer ─────────────────────────────────────────────────

int UserLibrary::savedCount() const
{
    int n = 0;
    for (const Collection &c : collections())
        n += int(m_saved.value(c.id).size());
    return n;
}

UserLibrary::TransferResult UserLibrary::exportArchive(const QString &path) const
{
    TransferResult result;
    for (const Collection &c : collections())
        result.unsaved += int(m_session.value(c.id).size());

    // The manifest lists every file of every entry, under a neutral name in the archive: the
    // names on disk can hold characters archive readers refuse ("..", ":").
    struct Pending
    {
        QString archivePath;
        QString source;
    };
    QList<Pending> files;
    QJsonObject collectionsJson;
    for (const Collection &c : collections()) {
        QJsonArray items;
        for (const Entry &e : m_saved.value(c.id)) {
            QJsonObject item = entryToJson(e);
            QJsonArray list;
            if (!e.file.isEmpty()) {
                if (!QFileInfo::exists(e.filePath()))
                    continue; // removed by hand since Dafin started: nothing to carry over
                const QDir base(e.dir);
                QDirIterator it(e.dir, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
                int n = 0;
                while (it.hasNext()) {
                    const QString source = it.next();
                    const QString archivePath = QStringLiteral("files/%1/%2/%3").arg(c.id, e.id).arg(n++);
                    list.append(QJsonObject{{QStringLiteral("zip"), archivePath},
                                            {QStringLiteral("path"), base.relativeFilePath(source)}});
                    files.append({archivePath, source});
                }
            }
            item.insert(QStringLiteral("files"), list);
            items.append(item);
            ++result.count;
        }
        collectionsJson.insert(c.id, items);
    }
    if (result.count == 0) {
        result.error = result.unsaved > 0
                           ? tr("Nothing is saved yet. Press Save on the files you want to keep, then export.")
                           : tr("There is nothing to export yet. Add Lotties, sounds or .mogrt titles first.");
        return result;
    }

    const QJsonObject manifest{{QStringLiteral("format"), kArchiveFormat},
                               {QStringLiteral("version"), kArchiveVersion},
                               {QStringLiteral("exported"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
                               {QStringLiteral("collections"), collectionsJson}};
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly)) {
        result.error = tr("Can't write %1.").arg(QDir::toNativeSeparators(path));
        result.count = 0;
        return result;
    }
    drift::zip::Writer zip(&out);
    bool ok = zip.addData(kArchiveManifest, QJsonDocument(manifest).toJson(QJsonDocument::Indented));
    for (const Pending &p : std::as_const(files)) {
        if (!ok)
            break;
        ok = zip.addFile(p.archivePath, p.source);
    }
    ok = ok && zip.finish();
    if (!ok || !out.commit()) {
        if (ok)
            result.error = tr("Can't write %1.").arg(QDir::toNativeSeparators(path));
        else
            result.error = tr("Couldn't export the library: %1.").arg(zip.errorString());
        out.cancelWriting();
        result.count = 0;
    }
    return result;
}

UserLibrary::TransferResult UserLibrary::importArchive(const QString &path)
{
    TransferResult result;
    const QString name = QFileInfo(path).fileName();
    QFile archive(path);
    if (!archive.open(QIODevice::ReadOnly)) {
        result.error = tr("Can't open %1.").arg(name);
        return result;
    }
    QHash<QString, drift::zip::Entry> byPath;
    for (const drift::zip::Entry &z : drift::zip::readEntries(archive, nullptr))
        byPath.insert(z.path, z);
    const auto manifestEntry = byPath.constFind(kArchiveManifest);
    QByteArray manifestBytes;
    if (manifestEntry == byPath.cend() || manifestEntry->uncompressedSize > 32u * 1024 * 1024
        || !drift::zip::extractEntry(archive, *manifestEntry, manifestBytes)) {
        result.error = tr("%1 is not a Dafin library export.").arg(name);
        return result;
    }
    const QJsonObject manifest = QJsonDocument::fromJson(manifestBytes).object();
    if (manifest.value(QStringLiteral("format")).toString() != kArchiveFormat) {
        result.error = tr("%1 is not a Dafin library export.").arg(name);
        return result;
    }
    if (manifest.value(QStringLiteral("version")).toInt() > kArchiveVersion) {
        result.error = tr("%1 was exported by a newer Dafin. Update Dafin, then import it again.").arg(name);
        return result;
    }

    const QJsonObject collectionsJson = manifest.value(QStringLiteral("collections")).toObject();
    for (const Collection &c : collections()) {
        QList<Entry> imported;
        for (const QJsonValue &v : collectionsJson.value(c.id).toArray()) {
            const QJsonObject o = v.toObject();
            Entry e;
            e.id = o.value(QStringLiteral("id")).toString();
            if (!validEntryId(e.id)) {
                ++result.skipped;
                continue;
            }
            const bool here = std::any_of(imported.cbegin(), imported.cend(), [&e](const Entry &x) { return x.id == e.id; });
            if (here || contains(c.id, e.id)) {
                // Dropped this session but not saved yet: it is wanted, so keep it.
                if (!here && !entry(c.id, e.id)->saved)
                    save(c.id, e.id);
                ++result.duplicates;
                continue;
            }
            e.title = o.value(QStringLiteral("title")).toString().trimmed();
            e.file = QFileInfo(o.value(QStringLiteral("file")).toString()).fileName();
            e.link = QUrl(o.value(QStringLiteral("link")).toString());
            e.added = QDateTime::fromString(o.value(QStringLiteral("added")).toString(), Qt::ISODate);
            if (!e.added.isValid())
                e.added = QDateTime::currentDateTimeUtc();
            e.meta = o.value(QStringLiteral("meta")).toObject().toVariantMap();
            e.saved = true;
            e.dir = savedDir(c.id) + QLatin1Char('/') + e.id;
            if (e.title.isEmpty())
                e.title = e.file.isEmpty() ? tr("Untitled") : titleFromFileName(e.file);

            if (e.file.isEmpty()) {
                // A remembered Lottie link: nothing to unpack.
                const bool web = e.link.scheme() == QLatin1String("https") || e.link.scheme() == QLatin1String("http");
                if (c.id != kLotties || !e.link.isValid() || !web) {
                    ++result.skipped;
                    continue;
                }
                imported.append(e);
                continue;
            }
            if (!fileSuffixes(c.id).contains(QFileInfo(e.file).suffix().toLower())) {
                ++result.skipped;
                continue;
            }

            QDir(e.dir).removeRecursively();
            bool ok = QDir().mkpath(e.dir);
            for (const QJsonValue &f : o.value(QStringLiteral("files")).toArray()) {
                if (!ok)
                    break;
                const QString rel = f.toObject().value(QStringLiteral("path")).toString();
                const auto z = byPath.constFind(f.toObject().value(QStringLiteral("zip")).toString());
                if (!safeRelativePath(rel) || z == byPath.cend() || qint64(z->uncompressedSize) > kMaxFileBytes) {
                    ok = false;
                    break;
                }
                QByteArray data;
                if (!drift::zip::extractEntry(archive, *z, data)) {
                    ok = false;
                    break;
                }
                const QString dest = e.dir + QLatin1Char('/') + rel;
                QDir().mkpath(QFileInfo(dest).absolutePath());
                QFile out(dest);
                ok = out.open(QIODevice::WriteOnly) && out.write(data) == data.size();
            }
            if (!ok || !QFileInfo::exists(e.filePath())) {
                QDir(e.dir).removeRecursively();
                ++result.skipped;
                continue;
            }
            // Files the cards read: only ones that came out of this archive into this folder.
            for (const QString key : {QStringLiteral("preview"), QStringLiteral("poster"), QStringLiteral("previewVideo")}) {
                const QString rel = e.meta.value(key).toString();
                if (!rel.isEmpty() && (!safeRelativePath(rel) || !QFileInfo::exists(e.dir + QLatin1Char('/') + rel)))
                    e.meta.remove(key);
            }
            imported.append(e);
        }
        if (imported.isEmpty())
            continue;
        QList<Entry> &saved = m_saved[c.id];
        saved = imported + saved;
        writeManifest(c.id);
        result.count += int(imported.size());
        emit changed(c.id);
    }
    if (result.count == 0 && result.duplicates == 0 && result.error.isEmpty())
        result.error = result.skipped > 0 ? tr("Nothing in %1 could be imported.").arg(name)
                                          : tr("%1 has nothing to import.").arg(name);
    return result;
}

} // namespace dafin::assets
