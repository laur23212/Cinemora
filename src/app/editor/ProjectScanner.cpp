#include "app/editor/ProjectScanner.h"

#include "core/interchange/ImportedTimelineProject.h"
#include "core/interchange/filmora/FilmoraProjectParser.h"
#include "core/interchange/premiere/PremiereProjectParser.h"
#include "core/project/Project.h"
#include "engine/media/thumbnails/MediaThumbnail.h"
#include "engine/project/ProjectBundle.h"

#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QtConcurrent>

#ifdef Q_OS_WIN
#include <QProcess>
#include <qt_windows.h>
#endif

#include <algorithm>
#include <limits>
#include <string>

namespace {

const QString kEnabledKey = QStringLiteral("projectScan/enabled");
const QString kFoldersKey = QStringLiteral("projectScan/folders");
const QString kHiddenKey = QStringLiteral("projectScan/hidden");
const QString kViewKey = QStringLiteral("startScreen/view");
constexpr int kCacheVersion = 1;
constexpr int kBatchSize = 6;

// Folders that never hold a project worth listing, or hold hundreds of copies of one (Premiere
// writes an auto-save every few minutes next to the project).
bool skipFolder(const QString &name)
{
    static const QSet<QString> names{
        QStringLiteral("adobe premiere pro auto-save"),
        QStringLiteral("auto-save"),
        QStringLiteral("adobe premiere pro preview files"),
        QStringLiteral("adobe premiere pro video previews"),
        QStringLiteral("adobe premiere pro audio previews"),
        QStringLiteral("node_modules"),
        QStringLiteral("$recycle.bin"),
        QStringLiteral("system volume information"),
        QStringLiteral("appdata"),
    };
    return name.startsWith(QLatin1Char('.')) || names.contains(name.toLower());
}

// The picture a card shows when Dafin has none saved: the first thing the edit shows, from the
// visual clip that starts earliest (the top track wins a tie), a second into it.
QString coverFromProject(const drift::Project &project)
{
    const drift::Clip *first = nullptr;
    for (const drift::Track &track : project.tracks()) {
        for (const drift::Clip &clip : track.clips) {
            if (clip.type != drift::ClipType::Video && clip.type != drift::ClipType::Image)
                continue;
            if (clip.path.isEmpty() || !QFileInfo(clip.path).isFile())
                continue;
            // A frame from a video kept only in the cloud would download all of it.
            if (ProjectScanner::isCloudOnly(clip.path))
                continue;
            if (!first || clip.timelineStart < first->timelineStart)
                first = &clip;
        }
    }
    if (!first)
        return {};
    if (first->type == drift::ClipType::Image)
        return first->path;
    const qint64 intoUs = std::min<qint64>(1'000'000, std::max<qint64>(0, first->timelineDuration / 2));
    return MediaThumbnail::generateAtTime(first->path, double(first->srcIn + intoUs) / 1'000'000.0);
}

QJsonObject infoToJson(const ProjectScanner::Info &info)
{
    return QJsonObject{
        {QStringLiteral("kind"), info.kind},
        {QStringLiteral("modified"), double(info.modified.toMSecsSinceEpoch())},
        {QStringLiteral("size"), double(info.sizeBytes)},
        {QStringLiteral("durationUs"), double(info.durationUs)},
        {QStringLiteral("cover"), info.cover},
    };
}

ProjectScanner::Info infoFromJson(const QString &path, const QJsonObject &o)
{
    ProjectScanner::Info info;
    info.path = path;
    info.kind = o.value(QStringLiteral("kind")).toString(ProjectScanner::kindFor(path));
    info.modified = QDateTime::fromMSecsSinceEpoch(qint64(o.value(QStringLiteral("modified")).toDouble()));
    info.sizeBytes = qint64(o.value(QStringLiteral("size")).toDouble());
    info.durationUs = qint64(o.value(QStringLiteral("durationUs")).toDouble());
    info.cover = o.value(QStringLiteral("cover")).toString();
    // A picture someone cleared from a cache is made again.
    info.inspected = info.cover.isEmpty() || QFileInfo::exists(info.cover);
    return info;
}

// Still what was read last time: same size, same modification time, picture still there.
bool stillCurrent(const ProjectScanner::Info &cached, const QFileInfo &file)
{
    return cached.inspected && cached.sizeBytes == file.size()
           && cached.modified.toMSecsSinceEpoch() == file.lastModified().toMSecsSinceEpoch();
}

} // namespace

bool ProjectScanner::isCloudOnly(const QString &path)
{
#ifdef Q_OS_WIN
    const std::wstring native = QDir::toNativeSeparators(path).toStdWString();
    const DWORD attributes = GetFileAttributesW(native.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES)
        return false;
    // FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS (0x400000), FILE_ATTRIBUTE_RECALL_ON_OPEN (0x40000)
    // and FILE_ATTRIBUTE_OFFLINE: placeholders a sync client fills on first read. Spelled out
    // because older SDK headers lack the first two.
    return (attributes & (0x400000u | 0x40000u | FILE_ATTRIBUTE_OFFLINE)) != 0;
#else
    Q_UNUSED(path);
    return false;
#endif
}

ProjectScanner::ProjectScanner(QObject *parent)
    : QObject(parent)
{
    loadCache();
}

ProjectScanner::~ProjectScanner()
{
    if (m_cancel)
        m_cancel->store(true);
    m_scan.waitForFinished();
}

QString ProjectScanner::kindFor(const QString &path)
{
    if (path.endsWith(QLatin1String(".drift"), Qt::CaseInsensitive))
        return QStringLiteral("dafin");
    if (path.endsWith(QLatin1String(".wfp"), Qt::CaseInsensitive))
        return QStringLiteral("filmora");
    if (path.endsWith(QLatin1String(".prproj"), Qt::CaseInsensitive))
        return QStringLiteral("premiere");
    return {};
}

QStringList ProjectScanner::findProjectFiles(const QStringList &roots, int maxDepth, int maxFiles,
                                             const std::atomic_bool *cancel)
{
    QStringList out;
    QSet<QString> seenFiles;
    QSet<QString> seenDirs;
    // Breadth first, so a cap that is reached keeps the projects nearest the top of each folder.
    QList<QPair<QString, int>> queue;
    for (const QString &root : roots)
        queue.append({root, 0});
    int folders = 0;
    while (!queue.isEmpty() && out.size() < maxFiles && folders < kMaxFolders) {
        if (cancel && cancel->load())
            return out;
        const auto [dirPath, depth] = queue.takeFirst();
        const QString canonicalDir = QFileInfo(dirPath).canonicalFilePath();
        if (canonicalDir.isEmpty() || seenDirs.contains(canonicalDir))
            continue;  // gone, or reached twice (a root inside another root)
        seenDirs.insert(canonicalDir);
        ++folders;
        // No symbolic links: a link back up the tree would loop, and a linked drive is the
        // user's to add as a folder of its own.
        QDirIterator it(dirPath, QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::NoSymLinks);
        while (it.hasNext()) {
            it.next();
            const QFileInfo entry = it.fileInfo();
            if (entry.isDir()) {
                if (depth < maxDepth && !skipFolder(entry.fileName()))
                    queue.append({entry.filePath(), depth + 1});
                continue;
            }
            if (kindFor(entry.fileName()).isEmpty())
                continue;
            const QString canonical = entry.canonicalFilePath();
            if (canonical.isEmpty() || seenFiles.contains(canonical))
                continue;
            seenFiles.insert(canonical);
            out.append(QDir::cleanPath(entry.absoluteFilePath()));
            if (out.size() >= maxFiles)
                break;
        }
    }
    return out;
}

ProjectScanner::Info ProjectScanner::inspect(const QString &path, const QString &thumbDir)
{
    Info info;
    info.path = path;
    info.kind = kindFor(path);
    const QFileInfo file(path);
    info.modified = file.lastModified();
    info.sizeBytes = file.size();
    info.inspected = true;
    if (!file.isFile() || isCloudOnly(path))
        return info;

    if (info.kind == QLatin1String("dafin")) {
        QString error;
        const auto manifest = drift::bundle::readManifest(path, &error);
        if (!manifest)
            return info;
        if (!manifest->projectId.isEmpty() && !thumbDir.isEmpty()) {
            const QString thumb = QDir(thumbDir).filePath(manifest->projectId + QStringLiteral(".jpg"));
            if (QFileInfo::exists(thumb))
                info.cover = thumb;
        }
        const drift::Project project = drift::Project::fromJson(manifest->document);
        info.durationUs = project.durationUs();
        if (info.cover.isEmpty())
            info.cover = coverFromProject(project);
        return info;
    }

    std::optional<drift::interchange::ImportedTimeline> timeline;
    if (info.kind == QLatin1String("filmora")) {
        timeline = drift::interchange::filmora::parse(path);
    } else if (info.kind == QLatin1String("premiere")) {
        QFile in(path);
        if (in.open(QIODevice::ReadOnly))
            timeline = drift::interchange::premiere::parse(in.readAll());
    }
    if (!timeline)
        return info;
    // Built as Open builds it, so media that moved is found the same way the opened project
    // finds it, and the picture is what the opened project shows first.
    const auto built = drift::interchange::toProject(*timeline, path);
    info.durationUs = built.project.durationUs();
    info.cover = coverFromProject(built.project);
    return info;
}

// --- Settings ---------------------------------------------------------------------------------

bool ProjectScanner::enabled() const
{
    return QSettings().value(kEnabledKey, true).toBool();
}

void ProjectScanner::setEnabled(bool enabled)
{
    if (enabled == this->enabled())
        return;
    QSettings().setValue(kEnabledKey, enabled);
    emit enabledChanged();
    rescan();
}

QStringList ProjectScanner::folders() const
{
    return QSettings().value(kFoldersKey).toStringList();
}

QStringList ProjectScanner::defaultFolders() const
{
    const QString home = QDir::cleanPath(QDir::homePath());
    QStringList out;
    for (const auto location : {QStandardPaths::DocumentsLocation, QStandardPaths::MoviesLocation,
                                QStandardPaths::DesktopLocation, QStandardPaths::DownloadLocation}) {
        const QString path = QDir::cleanPath(QStandardPaths::writableLocation(location));
        // A system without these folders reports the home folder for them; searching all of it
        // is what the user didn't choose.
        if (path.isEmpty() || path == home || out.contains(path))
            continue;
        out.append(path);
    }
    return out;
}

QString ProjectScanner::startView() const
{
    const QString view = QSettings().value(kViewKey).toString();
    return view == QLatin1String("list") ? view : QStringLiteral("grid");
}

void ProjectScanner::setStartView(const QString &view)
{
    const QString next = view == QLatin1String("list") ? view : QStringLiteral("grid");
    if (next == startView())
        return;
    QSettings().setValue(kViewKey, next);
    emit startViewChanged();
}

QStringList ProjectScanner::searchRoots() const
{
    QStringList roots;
    for (const QString &folder : defaultFolders() + folders()) {
        if (!roots.contains(folder) && QFileInfo(folder).isDir())
            roots.append(folder);
    }
    return roots;
}

bool ProjectScanner::addFolder(const QUrl &folder)
{
    const QString path = QDir::cleanPath(folder.isLocalFile() ? folder.toLocalFile() : folder.toString());
    if (path.isEmpty() || !QFileInfo(path).isDir())
        return false;
    QStringList list = folders();
    if (!list.contains(path)) {
        list.append(path);
        QSettings().setValue(kFoldersKey, list);
        emit foldersChanged();
        rescan();
    }
    return true;
}

void ProjectScanner::removeFolder(const QString &folder)
{
    QStringList list = folders();
    if (list.removeAll(folder) == 0)
        return;
    QSettings().setValue(kFoldersKey, list);
    emit foldersChanged();
    rescan();
}

void ProjectScanner::hide(const QString &path)
{
    QSettings settings;
    QStringList hidden = settings.value(kHiddenKey).toStringList();
    if (!hidden.contains(path)) {
        hidden.append(path);
        settings.setValue(kHiddenKey, hidden);
    }
    if (m_found.removeAll(path) > 0)
        emit projectsChanged();
}

void ProjectScanner::unhideAll()
{
    QSettings().remove(kHiddenKey);
    rescan();
}

void ProjectScanner::revealInFolder(const QString &path) const
{
#ifdef Q_OS_WIN
    // "/select," and the path must be separate arguments for Explorer to select the file.
    if (QProcess::startDetached(QStringLiteral("explorer.exe"),
                                {QStringLiteral("/select,"), QDir::toNativeSeparators(path)}))
        return;
#endif
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
}

// --- Cache ------------------------------------------------------------------------------------

QString ProjectScanner::cacheFile() const
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return base.isEmpty() ? QString() : QDir(base).filePath(QStringLiteral("project-scan.json"));
}

QString ProjectScanner::thumbDir() const
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return base.isEmpty() ? QString() : QDir(base).filePath(QStringLiteral("thumbs"));
}

void ProjectScanner::loadCache()
{
    QFile in(cacheFile());
    if (!in.open(QIODevice::ReadOnly))
        return;
    const QJsonObject root = QJsonDocument::fromJson(in.readAll()).object();
    if (root.value(QStringLiteral("version")).toInt() != kCacheVersion)
        return;
    const QJsonObject files = root.value(QStringLiteral("files")).toObject();
    for (auto it = files.constBegin(); it != files.constEnd(); ++it)
        m_cache.insert(it.key(), infoFromJson(it.key(), it.value().toObject()));
    // Shown straight away, before the first scan has even listed a folder; anything that went
    // meanwhile drops out when that scan lands.
    QStringList listed = root.value(QStringLiteral("found")).toVariant().toStringList();
    const QStringList hidden = QSettings().value(kHiddenKey).toStringList();
    listed.removeIf([&](const QString &p) { return hidden.contains(p) || !m_cache.contains(p); });
    m_found = listed;
}

void ProjectScanner::saveCache() const
{
    const QString path = cacheFile();
    if (path.isEmpty())
        return;
    QJsonObject files;
    // Only what is still listed or a recent, so the file doesn't grow with every folder that
    // was ever searched.
    for (const QString &p : m_found + m_extraFiles) {
        const auto it = m_cache.constFind(p);
        if (it != m_cache.constEnd() && it->inspected)
            files.insert(p, infoToJson(*it));
    }
    const QJsonObject root{
        {QStringLiteral("version"), kCacheVersion},
        {QStringLiteral("files"), files},
        {QStringLiteral("found"), QJsonArray::fromStringList(m_found)},
    };
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly))
        return;
    out.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    out.commit();
}

// --- Scanning ---------------------------------------------------------------------------------

void ProjectScanner::setExtraFiles(const QStringList &paths)
{
    m_extraFiles.clear();
    for (const QString &p : paths) {
        if (!p.startsWith(QLatin1String("content://"), Qt::CaseInsensitive) && !kindFor(p).isEmpty())
            m_extraFiles.append(p);
    }
}

ProjectScanner::Info ProjectScanner::info(const QString &path) const
{
    return m_cache.value(path);
}

void ProjectScanner::setScanning(bool scanning)
{
    if (m_scanning == scanning)
        return;
    m_scanning = scanning;
    emit scanningChanged();
}

void ProjectScanner::rescan()
{
    if (m_scanning) {
        m_rescanQueued = true;
        return;
    }
    m_rescanQueued = false;
    const QStringList roots = enabled() ? searchRoots() : QStringList();
    const QStringList extras = m_extraFiles;
    const QHash<QString, Info> cache = m_cache;
    const QString thumbs = thumbDir();
    const auto cancel = std::make_shared<std::atomic_bool>(false);
    m_cancel = cancel;
    setScanning(true);

    // `this` outlives the worker: the destructor cancels and waits for it. Calls queued on
    // `this` after it is gone are dropped by Qt.
    m_scan = QtConcurrent::run([this, roots, extras, cache, thumbs, cancel]() {
        const QStringList found = findProjectFiles(roots, kMaxDepth, kMaxFiles, cancel.get());
        if (cancel->load())
            return;

        // The list first, with what is already known (or just name and date), so cards appear
        // before any project has been read.
        QList<Info> stubs;
        QStringList toRead;
        for (const QString &path : found + extras) {
            const QFileInfo file(path);
            if (!file.isFile())
                continue;
            const auto it = cache.constFind(path);
            if (it != cache.constEnd() && stillCurrent(*it, file)) {
                stubs.append(*it);
                continue;
            }
            Info stub;
            stub.path = path;
            stub.kind = kindFor(path);
            stub.modified = file.lastModified();
            stub.sizeBytes = file.size();
            stubs.append(stub);
            if (!toRead.contains(path))
                toRead.append(path);
        }
        QMetaObject::invokeMethod(this, [this, found, stubs]() {
            applyInfos(stubs);
            listFound(found);
        }, Qt::QueuedConnection);

        QList<Info> batch;
        for (const QString &path : std::as_const(toRead)) {
            if (cancel->load())
                return;
            batch.append(inspect(path, thumbs));
            if (batch.size() >= kBatchSize) {
                QMetaObject::invokeMethod(this, [this, batch]() { applyInfos(batch); }, Qt::QueuedConnection);
                batch.clear();
            }
        }
        QMetaObject::invokeMethod(this, [this, batch]() {
            if (!batch.isEmpty())
                applyInfos(batch);
            finishScan();
        }, Qt::QueuedConnection);
    });
}

void ProjectScanner::listFound(const QStringList &paths)
{
    const QStringList hidden = QSettings().value(kHiddenKey).toStringList();
    QStringList listed;
    for (const QString &p : paths) {
        if (!hidden.contains(p))
            listed.append(p);
    }
    // Newest first, as recents are.
    std::stable_sort(listed.begin(), listed.end(), [this](const QString &a, const QString &b) {
        return m_cache.value(a).modified > m_cache.value(b).modified;
    });
    if (listed == m_found)
        return;
    m_found = listed;
    emit projectsChanged();
}

void ProjectScanner::applyInfos(const QList<Info> &infos)
{
    bool changed = false;
    for (const Info &info : infos) {
        const auto it = m_cache.constFind(info.path);
        if (it != m_cache.constEnd() && it->inspected == info.inspected && it->modified == info.modified
            && it->sizeBytes == info.sizeBytes && it->durationUs == info.durationUs && it->cover == info.cover)
            continue;
        // A stub never replaces a reading of the same version of the file.
        if (!info.inspected && it != m_cache.constEnd() && it->inspected && it->modified == info.modified
            && it->sizeBytes == info.sizeBytes)
            continue;
        m_cache.insert(info.path, info);
        changed = true;
    }
    if (!changed)
        return;
    emit projectsChanged();
    emit infoChanged();
}

void ProjectScanner::finishScan()
{
    setScanning(false);
    saveCache();
    if (m_rescanQueued)
        rescan();
}

QVariantList ProjectScanner::projects() const
{
    QVariantList out;
    out.reserve(m_found.size());
    for (const QString &path : m_found) {
        const Info info = m_cache.value(path);
        out.append(QVariantMap{
            {QStringLiteral("path"), path},
            {QStringLiteral("name"), QFileInfo(path).fileName()},
            {QStringLiteral("kind"), info.kind.isEmpty() ? kindFor(path) : info.kind},
            {QStringLiteral("modified"), info.modified},
            {QStringLiteral("sizeBytes"), info.sizeBytes},
            {QStringLiteral("durationUs"), double(info.durationUs)},
            {QStringLiteral("thumbnail"),
             info.cover.isEmpty() ? QString() : QUrl::fromLocalFile(info.cover).toString()},
            {QStringLiteral("exists"), true},
        });
    }
    return out;
}
