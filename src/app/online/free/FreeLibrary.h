#pragma once

#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <memory>

class QLockFile;
class QNetworkAccessManager;
class QNetworkReply;

// The "Stock Media" tab's back end: free stock video, sound effects and Lottie animations from
// sources described in a user-editable catalog JSON (resources/free-library/catalog.json is the
// built-in copy; see its _help block for the format).
//
// A source is either a static list of files or any JSON search API (Pexels, Pixabay, Freesound
// ship in the catalog; more can be added by editing it). Results are mapped into one flat item
// shape so QML draws every source the same way:
//   { key, sourceId, id, kind, title, author, thumb, preview, download, width, height,
//     duration, license, page }
//
// Downloads land in a per-session temporary folder (sessionRoot) and are imported into the
// media bin. The folder is deleted when Drift quits; one left behind by a crash is swept on a
// later start. Files a saved project uses survive anyway: ProjectFileController embeds every
// session file into the .drift bundle on save (isSessionFile), so a reopened project unpacks
// them from the bundle instead of looking for the deleted download.
class FreeLibrary : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList sources READ sources NOTIFY catalogChanged)
    Q_PROPERTY(QString catalogPath READ catalogPath NOTIFY catalogChanged)
    Q_PROPERTY(QString catalogError READ catalogError NOTIFY catalogChanged)
    // Bumped whenever a download's state or progress changes; QML bindings on state() read it.
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)

public:
    using Importer = std::function<QStringList(const QStringList &)>;

    explicit FreeLibrary(QObject *parent = nullptr);
    ~FreeLibrary() override;

    void setImporter(Importer importer) { m_importer = std::move(importer); }

    // The temporary folder this process downloads into. Created (and locked) on first use.
    static QString sessionRoot();
    // True for a file inside any stock-media session folder — the save path embeds these.
    static bool isSessionFile(const QString &path);
    // Deletes this process's session folder. Safe to call more than once.
    static void removeSession();
    // Deletes folders left by earlier runs that are no longer running (crash, kill). Folders
    // younger than a day are kept so crash recovery can still find its media.
    static void sweepStaleSessions();

    QVariantList sources() const;
    QString catalogPath() const;
    QString catalogError() const { return m_catalogError; }
    int revision() const { return m_revision; }

    Q_INVOKABLE void reloadCatalog();
    // Opens the user's catalog JSON in the system editor (creating it first if needed).
    Q_INVOKABLE void openCatalogFile();
    Q_INVOKABLE QVariantMap source(const QString &sourceId) const;

    // API keys are kept in the app settings, not in the catalog, so the catalog can be shared.
    // A key written in the catalog's "apiKey" field is used when none is saved.
    Q_INVOKABLE QString apiKey(const QString &sourceId) const;
    Q_INVOKABLE void setApiKey(const QString &sourceId, const QString &key);
    Q_INVOKABLE bool hasKey(const QString &sourceId) const;

    // Starts a search (empty query = browse/popular). Returns a token; results(token, …) answers
    // it. Older tokens for the same source can be ignored by the caller.
    Q_INVOKABLE int search(const QString &sourceId, const QString &query, int page = 1);

    // none | downloading | ready | failed
    Q_INVOKABLE QString state(const QString &key) const;
    Q_INVOKABLE double progress(const QString &key) const;
    Q_INVOKABLE QString localPath(const QString &key) const;
    // Downloads the item into the session folder and imports it into the media bin.
    Q_INVOKABLE void download(const QVariantMap &item);
    // A pasted link: a Lottie (.json/.lottie/lottie.host) is remembered in "My Lotties"; any
    // link is downloaded and imported. Returns false (with failed()) when it is not a usable URL.
    Q_INVOKABLE bool addFromUrl(const QString &url);

    // Lottie hover previews: fetches the (small) JSON into the session folder and fires
    // previewReady(key, path); QML then draws frames through image://lottieframe/.
    Q_INVOKABLE void preparePreview(const QVariantMap &item);
    Q_INVOKABLE QString previewPath(const QString &key) const;

    // Field lookup used by the API mapper; public so it can be unit-tested.
    static QJsonValue lookup(const QJsonValue &root, const QString &path);

signals:
    void catalogChanged();
    void revisionChanged();
    void results(int token, const QString &sourceId, const QVariantList &items, bool hasMore,
                 const QString &error);
    void downloaded(const QString &key, const QString &binAssetId);
    void failed(const QString &key, const QString &message);
    void previewReady(const QString &key, const QString &path);

private:
    struct Download;

    QString userCatalogPath() const;
    QJsonObject sourceObject(const QString &sourceId) const;
    QVariantList staticResults(const QJsonObject &src, const QString &query) const;
    QVariantList mapApiResults(const QJsonObject &src, const QJsonDocument &doc) const;
    QVariantMap normalise(const QJsonObject &src, QVariantMap item) const;
    QString expandTemplate(const QJsonObject &src, const QString &tmpl, const QString &query, int page) const;
    void fetchToFile(const QString &key, const QString &url, const QString &destPath,
                     std::function<void(bool ok, const QString &error)> done);
    void setState(const QString &key, const QString &state, double progress = 0.0);
    void rememberLottieLink(const QString &url);

    QNetworkAccessManager *m_net = nullptr;
    Importer m_importer;
    QJsonArray m_sources;
    QString m_catalogError;
    QHash<QString, QString> m_states;
    QHash<QString, double> m_progress;
    QHash<QString, QString> m_paths;
    QHash<QString, QString> m_previews;
    QHash<QString, QPointer<QNetworkReply>> m_active;
    // Pixabay asks for search responses to be cached for 24 hours; done for every API source.
    struct CachedResponse { QDateTime at; QByteArray body; };
    QHash<QString, CachedResponse> m_responseCache;
    int m_nextToken = 1;
    int m_revision = 0;
};
