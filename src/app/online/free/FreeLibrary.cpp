#include "app/online/free/FreeLibrary.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>
#include <QMimeDatabase>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>

#include <memory>

namespace {

constexpr auto kBuiltinCatalog = ":/free-library/catalog.json";
constexpr auto kSessionPrefix = "session-";
constexpr auto kLockName = ".lock";
constexpr qint64 kCacheSeconds = 24 * 60 * 60;
constexpr qint64 kStaleSessionSeconds = 24 * 60 * 60;
constexpr int kTimeoutMs = 30000;

QString stockBaseDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/drift-stock-media");
}

// The current session: folder plus the lock that marks it as in use.
struct Session
{
    QString dir;
    std::unique_ptr<QLockFile> lock;
};

Session &session()
{
    static Session s;
    return s;
}

QString safeName(QString name)
{
    name.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._ -]+")), QStringLiteral("_"));
    name = name.simplified();
    if (name.size() > 80)
        name = name.left(80);
    return name.isEmpty() ? QStringLiteral("item") : name;
}

QString extensionFor(const QString &url, const QString &kind, const QByteArray &contentType = {})
{
    const QString path = QUrl(url).path();
    const QString suffix = QFileInfo(path).suffix().toLower();
    static const QStringList known = {"mp4", "mov", "webm", "mkv", "m4v", "mp3", "wav", "ogg", "flac", "m4a",
                                      "aac", "json", "lottie", "png", "jpg", "jpeg", "webp", "gif", "svg"};
    if (known.contains(suffix))
        return suffix;
    if (!contentType.isEmpty()) {
        const QMimeType mime = QMimeDatabase().mimeTypeForName(QString::fromLatin1(contentType).section(';', 0, 0).trimmed());
        if (mime.isValid() && !mime.preferredSuffix().isEmpty())
            return mime.preferredSuffix();
    }
    if (kind == QLatin1String("video")) return QStringLiteral("mp4");
    if (kind == QLatin1String("audio")) return QStringLiteral("mp3");
    if (kind == QLatin1String("lottie")) return QStringLiteral("json");
    return QStringLiteral("bin");
}

bool looksLikeLottieUrl(const QUrl &url)
{
    const QString path = url.path().toLower();
    return path.endsWith(QLatin1String(".json")) || path.endsWith(QLatin1String(".lottie"))
        || url.host().endsWith(QLatin1String("lottie.host"));
}

bool isLottieDocument(const QByteArray &bytes)
{
    if (bytes.startsWith("PK"))
        return true; // .lottie bundle (zip); the bin importer unpacks it
    const QJsonDocument doc = QJsonDocument::fromJson(bytes);
    const QJsonObject o = doc.object();
    return o.contains(QStringLiteral("layers")) && (o.contains(QStringLiteral("v")) || o.contains(QStringLiteral("fr")));
}

// "https://www.pexels.com/video/a-dog-on-a-beach-123456/" -> "A dog on a beach"
QString prettyTitle(QString title)
{
    if (title.startsWith(QLatin1String("http"))) {
        const QStringList parts = QUrl(title).path().split('/', Qt::SkipEmptyParts);
        title = parts.isEmpty() ? QString() : parts.last();
        title.remove(QRegularExpression(QStringLiteral("-?\\d+$")));
        title.replace('-', ' ');
    }
    // Pixabay titles are tag lists: keep the first three.
    if (title.count(',') >= 2)
        title = title.section(',', 0, 2);
    title = title.simplified();
    if (!title.isEmpty())
        title[0] = title.at(0).toUpper();
    return title;
}

QVariant toVariant(const QJsonValue &v)
{
    if (v.isDouble())
        return v.toDouble();
    if (v.isString())
        return v.toString();
    if (v.isBool())
        return v.toBool();
    return {};
}

} // namespace

// ─── Session folder ──────────────────────────────────────────────────────────────────────────

QString FreeLibrary::sessionRoot()
{
    Session &s = session();
    if (!s.dir.isEmpty())
        return s.dir;
    const QString dir = stockBaseDir() + '/' + QLatin1String(kSessionPrefix)
                      + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    QDir().mkpath(dir);
    s.lock = std::make_unique<QLockFile>(dir + '/' + QLatin1String(kLockName));
    // Held for the whole run: age must never make it look abandoned, only a dead process.
    s.lock->setStaleLockTime(0);
    s.lock->tryLock(0);
    s.dir = dir;
    return s.dir;
}

bool FreeLibrary::isSessionFile(const QString &path)
{
    if (path.isEmpty())
        return false;
    const QString base = QDir::cleanPath(stockBaseDir()) + '/';
    const QString clean = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
#ifdef Q_OS_WIN
    return clean.startsWith(base, Qt::CaseInsensitive);
#else
    return clean.startsWith(base);
#endif
}

void FreeLibrary::removeSession()
{
    Session &s = session();
    if (s.dir.isEmpty())
        return;
    if (s.lock)
        s.lock->unlock();
    s.lock.reset();
    QDir(s.dir).removeRecursively();
    s.dir.clear();
}

void FreeLibrary::sweepStaleSessions()
{
    const QDir base(stockBaseDir());
    if (!base.exists())
        return;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QFileInfoList dirs = base.entryInfoList({QLatin1String(kSessionPrefix) + '*'}, QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &info : dirs) {
        if (info.absoluteFilePath() == session().dir)
            continue;
        QLockFile lock(info.absoluteFilePath() + '/' + QLatin1String(kLockName));
        lock.setStaleLockTime(0);
        // A live Drift holds its lock; a dead one's lock is removed by tryLock as stale.
        if (!lock.tryLock(0))
            continue;
        lock.unlock();
        if (info.lastModified().toUTC().secsTo(now) < kStaleSessionSeconds)
            continue; // probably a crash: leave it for the recovery dialog for a day
        QDir(info.absoluteFilePath()).removeRecursively();
    }
}

// ─── Catalog ─────────────────────────────────────────────────────────────────────────────────

FreeLibrary::FreeLibrary(QObject *parent)
    : QObject(parent)
    , m_net(new QNetworkAccessManager(this))
{
    m_net->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
    m_net->setTransferTimeout(kTimeoutMs);
    sweepStaleSessions();
    reloadCatalog();
    if (auto *app = QCoreApplication::instance())
        connect(app, &QCoreApplication::aboutToQuit, this, [this]() {
            for (const QPointer<QNetworkReply> &reply : std::as_const(m_active)) {
                if (reply)
                    reply->abort();
            }
            removeSession();
        });
}

FreeLibrary::~FreeLibrary()
{
    removeSession();
}

QString FreeLibrary::userCatalogPath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/free-library/catalog.json");
}

QString FreeLibrary::catalogPath() const
{
    return userCatalogPath();
}

void FreeLibrary::reloadCatalog()
{
    m_catalogError.clear();

    QFile builtinFile{QString::fromLatin1(kBuiltinCatalog)};
    QJsonArray builtin;
    if (builtinFile.open(QIODevice::ReadOnly))
        builtin = QJsonDocument::fromJson(builtinFile.readAll()).object().value(QStringLiteral("sources")).toArray();

    // First run: give the user an editable copy.
    const QString userPath = userCatalogPath();
    if (!QFileInfo::exists(userPath)) {
        QDir().mkpath(QFileInfo(userPath).absolutePath());
        QFile::copy(QLatin1String(kBuiltinCatalog), userPath);
        QFile::setPermissions(userPath, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadUser | QFile::WriteUser);
    }

    QJsonArray sources;
    QFile userFile(userPath);
    if (userFile.open(QIODevice::ReadOnly)) {
        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(userFile.readAll(), &error);
        if (error.error != QJsonParseError::NoError) {
            m_catalogError = tr("Your catalog file has an error at character %1: %2. Using the built-in catalog.")
                                 .arg(error.offset).arg(error.errorString());
            sources = builtin;
        } else {
            sources = doc.object().value(QStringLiteral("sources")).toArray();
            // Sources added to the built-in catalog by an update appear without touching edits.
            QSet<QString> ids;
            for (const QJsonValue &v : std::as_const(sources))
                ids.insert(v.toObject().value(QStringLiteral("id")).toString());
            for (const QJsonValue &v : std::as_const(builtin)) {
                if (!ids.contains(v.toObject().value(QStringLiteral("id")).toString()))
                    sources.append(v);
            }
        }
    } else {
        sources = builtin;
    }

    m_sources = sources;
    m_responseCache.clear();
    emit catalogChanged();
}

void FreeLibrary::openCatalogFile()
{
    if (!QFileInfo::exists(userCatalogPath()))
        reloadCatalog();
    QDesktopServices::openUrl(QUrl::fromLocalFile(userCatalogPath()));
}

QJsonObject FreeLibrary::sourceObject(const QString &sourceId) const
{
    for (const QJsonValue &v : m_sources) {
        const QJsonObject o = v.toObject();
        if (o.value(QStringLiteral("id")).toString() == sourceId)
            return o;
    }
    return {};
}

QVariantMap FreeLibrary::source(const QString &sourceId) const
{
    QVariantMap out = sourceObject(sourceId).toVariantMap();
    out.remove(QStringLiteral("items"));
    out.insert(QStringLiteral("hasKey"), hasKey(sourceId));
    return out;
}

QVariantList FreeLibrary::sources() const
{
    QVariantList out;
    for (const QJsonValue &v : m_sources) {
        const QJsonObject o = v.toObject();
        const QString id = o.value(QStringLiteral("id")).toString();
        if (id.isEmpty())
            continue;
        out.append(source(id));
    }
    return out;
}

QString FreeLibrary::apiKey(const QString &sourceId) const
{
    const QString saved = QSettings().value(QStringLiteral("FreeLibrary/keys/") + sourceId).toString().trimmed();
    if (!saved.isEmpty())
        return saved;
    return sourceObject(sourceId).value(QStringLiteral("apiKey")).toString().trimmed();
}

void FreeLibrary::setApiKey(const QString &sourceId, const QString &key)
{
    QSettings().setValue(QStringLiteral("FreeLibrary/keys/") + sourceId, key.trimmed());
    m_responseCache.clear();
    emit catalogChanged();
}

bool FreeLibrary::hasKey(const QString &sourceId) const
{
    return !apiKey(sourceId).isEmpty();
}

// ─── Search ──────────────────────────────────────────────────────────────────────────────────

QJsonValue FreeLibrary::lookup(const QJsonValue &root, const QString &path)
{
    if (path.startsWith('\'') && path.endsWith('\'') && path.size() >= 2)
        return QJsonValue(path.mid(1, path.size() - 2));

    static const QRegularExpression pick(QStringLiteral("^(.*)\\{\\s*([A-Za-z0-9_]+)\\s*(<=|>=)\\s*([0-9.]+)\\s*\\}$"));
    QJsonValue cur = root;
    for (const QString &rawPart : path.split('.', Qt::SkipEmptyParts)) {
        QString part = rawPart;
        const QRegularExpressionMatch m = pick.match(part);
        QString field;
        QString op;
        double limit = 0;
        if (m.hasMatch()) {
            part = m.captured(1);
            field = m.captured(2);
            op = m.captured(3);
            limit = m.captured(4).toDouble();
        }
        if (!part.isEmpty()) {
            if (cur.isArray()) {
                bool isIndex = false;
                const int index = part.toInt(&isIndex);
                cur = isIndex ? cur.toArray().at(index) : QJsonValue();
            } else {
                cur = cur.toObject().value(part);
            }
        }
        if (!field.isEmpty()) {
            const QJsonArray array = cur.toArray();
            const bool atMost = op == QLatin1String("<=");
            QJsonValue best;
            QJsonValue fallback;
            bool haveBest = false;
            bool haveFallback = false;
            double bestValue = 0;
            double fallbackValue = 0;
            for (const QJsonValue &element : array) {
                const double value = element.toObject().value(field).toDouble();
                const bool inRange = atMost ? value <= limit : value >= limit;
                if (inRange && (!haveBest || (atMost ? value > bestValue : value < bestValue))) {
                    best = element;
                    bestValue = value;
                    haveBest = true;
                }
                // Nothing in range: take the one closest to the limit from the other side.
                if (!haveFallback || (atMost ? value < fallbackValue : value > fallbackValue)) {
                    fallback = element;
                    fallbackValue = value;
                    haveFallback = true;
                }
            }
            cur = haveBest ? best : fallback;
        }
        if (cur.isUndefined() || cur.isNull())
            return {};
    }
    return cur;
}

QString FreeLibrary::expandTemplate(const QJsonObject &src, const QString &tmpl, const QString &query, int page) const
{
    QString out = tmpl;
    const QString id = src.value(QStringLiteral("id")).toString();
    out.replace(QStringLiteral("{query}"), QString::fromUtf8(QUrl::toPercentEncoding(query)));
    out.replace(QStringLiteral("{page}"), QString::number(page));
    out.replace(QStringLiteral("{perPage}"), QString::number(src.value(QStringLiteral("perPage")).toInt(30)));
    out.replace(QStringLiteral("{apiKey}"), apiKey(id));
    return out;
}

QVariantMap FreeLibrary::normalise(const QJsonObject &src, QVariantMap item) const
{
    const QString sourceId = src.value(QStringLiteral("id")).toString();
    const QString kind = src.value(QStringLiteral("kind")).toString();
    QString id = item.value(QStringLiteral("id")).toString();
    const QString download = item.value(QStringLiteral("download")).toString();
    if (id.isEmpty())
        id = QString::fromLatin1(QCryptographicHash::hash(download.toUtf8(), QCryptographicHash::Sha1).toHex().left(12));
    item.insert(QStringLiteral("id"), id);
    item.insert(QStringLiteral("sourceId"), sourceId);
    item.insert(QStringLiteral("kind"), item.value(QStringLiteral("kind")).toString().isEmpty() ? kind : item.value(QStringLiteral("kind")));
    item.insert(QStringLiteral("key"), sourceId + ':' + id);
    item.insert(QStringLiteral("title"), prettyTitle(item.value(QStringLiteral("title")).toString()));
    if (item.value(QStringLiteral("license")).toString().isEmpty())
        item.insert(QStringLiteral("license"), src.value(QStringLiteral("license")).toString());
    return item;
}

QVariantList FreeLibrary::staticResults(const QJsonObject &src, const QString &query) const
{
    QVariantList out;
    const QStringList words = query.toLower().split(' ', Qt::SkipEmptyParts);
    for (const QJsonValue &v : src.value(QStringLiteral("items")).toArray()) {
        const QJsonObject o = v.toObject();
        const QString haystack = (o.value(QStringLiteral("title")).toString() + ' '
                                  + o.value(QStringLiteral("tags")).toString() + ' '
                                  + o.value(QStringLiteral("author")).toString()).toLower();
        bool match = true;
        for (const QString &word : words)
            match = match && haystack.contains(word);
        if (!match)
            continue;
        QVariantMap item = o.toVariantMap();
        item.insert(QStringLiteral("download"), o.value(QStringLiteral("url")).toString());
        out.append(normalise(src, item));
    }
    return out;
}

QVariantList FreeLibrary::mapApiResults(const QJsonObject &src, const QJsonDocument &doc) const
{
    QVariantList out;
    const QJsonValue root = doc.isArray() ? QJsonValue(doc.array()) : QJsonValue(doc.object());
    const QJsonArray items = lookup(root, src.value(QStringLiteral("items")).toString()).toArray();
    const QJsonObject map = src.value(QStringLiteral("map")).toObject();
    for (const QJsonValue &entry : items) {
        QVariantMap item;
        for (auto it = map.constBegin(); it != map.constEnd(); ++it)
            item.insert(it.key(), toVariant(lookup(entry, it.value().toString())));
        if (item.value(QStringLiteral("download")).toString().isEmpty())
            continue;
        out.append(normalise(src, item));
    }
    return out;
}

int FreeLibrary::search(const QString &sourceId, const QString &query, int page)
{
    const int token = m_nextToken++;
    const QJsonObject src = sourceObject(sourceId);
    const QString trimmed = query.trimmed();

    auto answer = [this, token, sourceId](const QVariantList &items, bool hasMore, const QString &error) {
        QMetaObject::invokeMethod(this, [=, this]() { emit results(token, sourceId, items, hasMore, error); },
                                  Qt::QueuedConnection);
    };

    if (src.isEmpty()) {
        answer({}, false, tr("Unknown source"));
        return token;
    }
    const QString type = src.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("static")) {
        answer(page <= 1 ? staticResults(src, trimmed) : QVariantList(), false, {});
        return token;
    }
    if (src.value(QStringLiteral("needsKey")).toBool() && !hasKey(sourceId)) {
        answer({}, false, QStringLiteral("needs-key"));
        return token;
    }

    const QString tmpl = src.value(trimmed.isEmpty() ? QStringLiteral("browse") : QStringLiteral("search")).toString();
    if (tmpl.isEmpty()) {
        answer({}, false, tr("This source has no %1 URL in the catalog").arg(trimmed.isEmpty() ? "browse" : "search"));
        return token;
    }
    const QString url = expandTemplate(src, tmpl, trimmed, qMax(1, page));
    const int perPage = src.value(QStringLiteral("perPage")).toInt(30);

    const auto cached = m_responseCache.constFind(url);
    if (cached != m_responseCache.constEnd() && cached->at.secsTo(QDateTime::currentDateTimeUtc()) < kCacheSeconds) {
        const QVariantList items = mapApiResults(src, QJsonDocument::fromJson(cached->body));
        answer(items, items.size() >= perPage, {});
        return token;
    }

    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Drift-StockMedia/1.0"));
    const QJsonObject headers = src.value(QStringLiteral("headers")).toObject();
    for (auto it = headers.constBegin(); it != headers.constEnd(); ++it)
        request.setRawHeader(it.key().toUtf8(), expandTemplate(src, it.value().toString(), trimmed, page).toUtf8());

    QNetworkReply *reply = m_net->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, src, url, perPage, answer]() {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError) {
            QString message = reply->errorString();
            if (status == 401 || status == 403)
                message = QStringLiteral("bad-key");
            else if (status == 429)
                message = tr("Too many requests — wait a minute and try again.");
            answer({}, false, message);
            return;
        }
        const QByteArray body = reply->readAll();
        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(body, &error);
        if (error.error != QJsonParseError::NoError) {
            answer({}, false, tr("The source sent something that is not JSON."));
            return;
        }
        m_responseCache.insert(url, {QDateTime::currentDateTimeUtc(), body});
        const QVariantList items = mapApiResults(src, doc);
        answer(items, items.size() >= perPage, {});
    });
    return token;
}

// ─── Downloads ───────────────────────────────────────────────────────────────────────────────

void FreeLibrary::setState(const QString &key, const QString &state, double progress)
{
    m_states.insert(key, state);
    m_progress.insert(key, progress);
    ++m_revision;
    emit revisionChanged();
}

QString FreeLibrary::state(const QString &key) const
{
    return m_states.value(key, QStringLiteral("none"));
}

double FreeLibrary::progress(const QString &key) const
{
    return m_progress.value(key, 0.0);
}

QString FreeLibrary::localPath(const QString &key) const
{
    const QString path = m_paths.value(key);
    return QFileInfo::exists(path) ? path : QString();
}

void FreeLibrary::fetchToFile(const QString &key, const QString &url, const QString &destPath,
                              std::function<void(bool, const QString &)> done)
{
    QDir().mkpath(QFileInfo(destPath).absolutePath());
    auto file = std::make_shared<QSaveFile>(destPath);
    if (!file->open(QIODevice::WriteOnly)) {
        done(false, tr("Cannot write to the temporary folder: %1").arg(file->errorString()));
        return;
    }
    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Drift-StockMedia/1.0"));
    QNetworkReply *reply = m_net->get(request);
    m_active.insert(key, reply);

    connect(reply, &QNetworkReply::readyRead, this, [reply, file]() { file->write(reply->readAll()); });
    connect(reply, &QNetworkReply::downloadProgress, this, [this, key](qint64 got, qint64 total) {
        if (total > 0 && state(key) == QLatin1String("downloading")) {
            m_progress.insert(key, double(got) / double(total));
            ++m_revision;
            emit revisionChanged();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, key, reply, file, done]() {
        reply->deleteLater();
        m_active.remove(key);
        if (reply->error() != QNetworkReply::NoError) {
            file->cancelWriting();
            done(false, reply->error() == QNetworkReply::OperationCanceledError ? tr("Cancelled") : reply->errorString());
            return;
        }
        file->write(reply->readAll());
        if (!file->commit()) {
            done(false, tr("Could not save the file: %1").arg(file->errorString()));
            return;
        }
        done(true, {});
    });
}

void FreeLibrary::download(const QVariantMap &item)
{
    const QString key = item.value(QStringLiteral("key")).toString();
    const QString url = item.value(QStringLiteral("download")).toString();
    const QString kind = item.value(QStringLiteral("kind")).toString();
    if (key.isEmpty() || url.isEmpty())
        return;
    if (state(key) == QLatin1String("downloading"))
        return;

    // Already downloaded this session: just add it to the bin again.
    if (!localPath(key).isEmpty()) {
        const QStringList ids = m_importer ? m_importer({localPath(key)}) : QStringList();
        emit downloaded(key, ids.value(0));
        return;
    }

    const QString sourceId = item.value(QStringLiteral("sourceId")).toString();
    const QString name = safeName(item.value(QStringLiteral("title")).toString()) + QStringLiteral(" [")
                       + safeName(item.value(QStringLiteral("id")).toString()) + QStringLiteral("].")
                       + extensionFor(url, kind);
    const QString dest = sessionRoot() + '/' + safeName(sourceId) + '/' + name;

    setState(key, QStringLiteral("downloading"));
    fetchToFile(key, url, dest, [this, key, dest, kind](bool ok, const QString &error) {
        if (ok && kind == QLatin1String("lottie")) {
            QFile f(dest);
            if (!f.open(QIODevice::ReadOnly) || !isLottieDocument(f.read(8 * 1024 * 1024))) {
                f.close();
                QFile::remove(dest);
                setState(key, QStringLiteral("failed"));
                emit failed(key, tr("That link is not a Lottie animation. On LottieFiles, copy the "
                                    "'Lottie JSON' URL, not the page address."));
                return;
            }
        }
        if (!ok) {
            setState(key, QStringLiteral("failed"));
            emit failed(key, error);
            return;
        }
        m_paths.insert(key, dest);
        setState(key, QStringLiteral("ready"), 1.0);
        const QStringList ids = m_importer ? m_importer({dest}) : QStringList();
        if (ids.isEmpty()) {
            emit failed(key, tr("Downloaded, but the media bin could not import it."));
            return;
        }
        emit downloaded(key, ids.value(0));
    });
}

void FreeLibrary::rememberLottieLink(const QString &url)
{
    const QString path = userCatalogPath();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject())
        return; // never overwrite a catalog the user is in the middle of fixing
    QJsonObject root = doc.object();
    QJsonArray sources = root.value(QStringLiteral("sources")).toArray();
    int index = -1;
    for (int i = 0; i < sources.size(); ++i) {
        if (sources.at(i).toObject().value(QStringLiteral("id")).toString() == QLatin1String("my-lotties"))
            index = i;
    }
    if (index < 0) {
        sources.append(QJsonObject{{"id", "my-lotties"}, {"name", "My Lotties"}, {"kind", "lottie"},
                                   {"type", "static"}, {"items", QJsonArray()}});
        index = sources.size() - 1;
    }
    QJsonObject mine = sources.at(index).toObject();
    QJsonArray items = mine.value(QStringLiteral("items")).toArray();
    for (const QJsonValue &v : std::as_const(items)) {
        if (v.toObject().value(QStringLiteral("url")).toString() == url)
            return;
    }
    QString title = QFileInfo(QUrl(url).path()).completeBaseName();
    items.append(QJsonObject{{"title", title.isEmpty() ? QStringLiteral("Lottie") : title}, {"url", url}});
    mine.insert(QStringLiteral("items"), items);
    sources.replace(index, mine);
    root.insert(QStringLiteral("sources"), sources);

    QSaveFile out(path);
    if (out.open(QIODevice::WriteOnly)) {
        out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        out.commit();
    }
    reloadCatalog();
}

bool FreeLibrary::addFromUrl(const QString &text)
{
    const QUrl url = QUrl::fromUserInput(text.trimmed());
    if (!url.isValid() || (url.scheme() != QLatin1String("https") && url.scheme() != QLatin1String("http"))) {
        emit failed(QString(), tr("Paste a full link that starts with https://"));
        return false;
    }
    const bool lottie = looksLikeLottieUrl(url);
    if (lottie)
        rememberLottieLink(url.toString());

    QString kind = QStringLiteral("video");
    const QString suffix = QFileInfo(url.path()).suffix().toLower();
    if (lottie)
        kind = QStringLiteral("lottie");
    else if (QStringList{"mp3", "wav", "ogg", "flac", "m4a", "aac"}.contains(suffix))
        kind = QStringLiteral("audio");
    else if (QStringList{"png", "jpg", "jpeg", "webp", "gif", "svg"}.contains(suffix))
        kind = QStringLiteral("image");

    const QString id = QString::fromLatin1(QCryptographicHash::hash(url.toString().toUtf8(), QCryptographicHash::Sha1).toHex().left(12));
    const QString sourceId = lottie ? QStringLiteral("my-lotties") : QStringLiteral("links");
    QString title = QFileInfo(url.path()).completeBaseName();
    download(QVariantMap{{"key", sourceId + ':' + id}, {"sourceId", sourceId}, {"id", id}, {"kind", kind},
                         {"title", title.isEmpty() ? QStringLiteral("Link") : title}, {"download", url.toString()}});
    return true;
}

// ─── Lottie previews ─────────────────────────────────────────────────────────────────────────

QString FreeLibrary::previewPath(const QString &key) const
{
    const QString path = m_previews.value(key);
    return QFileInfo::exists(path) ? path : QString();
}

void FreeLibrary::preparePreview(const QVariantMap &item)
{
    const QString key = item.value(QStringLiteral("key")).toString();
    const QString url = item.value(QStringLiteral("download")).toString();
    if (key.isEmpty() || url.isEmpty() || item.value(QStringLiteral("kind")).toString() != QLatin1String("lottie"))
        return;
    if (!previewPath(key).isEmpty()) {
        emit previewReady(key, previewPath(key));
        return;
    }
    if (!localPath(key).isEmpty()) {
        m_previews.insert(key, localPath(key));
        emit previewReady(key, localPath(key));
        return;
    }
    const QString previewKey = QStringLiteral("preview|") + key;
    if (m_active.contains(previewKey))
        return;
    const QString dest = sessionRoot() + QStringLiteral("/previews/")
                       + QString::fromLatin1(QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha1).toHex().left(16))
                       + '.' + extensionFor(url, QStringLiteral("lottie"));
    fetchToFile(previewKey, url, dest, [this, key, dest](bool ok, const QString &) {
        if (!ok)
            return;
        m_previews.insert(key, dest);
        emit previewReady(key, dest);
    });
}
