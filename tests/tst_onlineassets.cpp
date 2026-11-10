// Online asset system: provider parsers, credential storage, auth states, the search model,
// downloads and the local asset database. No network: providers are fed canned JSON or faked,
// downloads read local files through file:// URLs.

#include "app/online/library/AssetSearchService.h"
#include "app/online/library/AuthManager.h"
#include "app/online/library/DownloadManager.h"
#include "app/online/library/LocalAssetDatabase.h"
#include "app/online/library/ProviderRegistry.h"
#include "app/online/library/SecretStore.h"
#include "app/online/library/SessionFiles.h"
#include "app/online/library/providers/DafinLibraryProvider.h"
#include "app/online/library/providers/CatalogProvider.h"
#include "app/online/library/providers/FreesoundProvider.h"
#include "app/online/library/providers/JamendoProvider.h"
#include "app/online/library/providers/OpenverseProvider.h"
#include "app/online/library/providers/PexelsProvider.h"
#include "app/online/library/providers/PixabayProvider.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include "tst_onlineassets_fixtures.h"

using namespace dafin::assets;
using namespace fixtures;

namespace {

QJsonDocument json(const char *text)
{
    QJsonParseError e;
    QJsonDocument d = QJsonDocument::fromJson(QByteArray(text), &e);
    if (e.error != QJsonParseError::NoError)
        qFatal("bad test JSON: %s", qPrintable(e.errorString()));
    return d;
}

// A provider whose answers the test scripts.
class FakeProvider : public AssetProvider
{
public:
    FakeProvider(ProviderContext *ctx, QString id, QList<AssetKind> kinds, bool requiresAccount = false)
        : AssetProvider(ctx), m_id(std::move(id)), m_kinds(std::move(kinds)), m_requires(requiresAccount) {}

    ProviderInfo info() const override
    {
        ProviderInfo i;
        i.id = m_id;
        i.name = m_id.toUpper();
        i.kinds = m_kinds;
        i.requiresAccount = m_requires;
        if (m_requires)
            i.fields = {{QStringLiteral("apiKey"), QStringLiteral("API key"), true, false, {}}};
        i.responseCacheSeconds = 600;
        return i;
    }
    RequestHandle search(const SearchRequest &r, SearchCallback cb) override
    {
        ++calls;
        if (m_requires && credentials().value(QStringLiteral("apiKey")).toString() != QLatin1String("good")) {
            cb({}, ProviderError::make(credentials().isEmpty() ? ProviderError::Code::AuthRequired
                                                               : ProviderError::Code::AuthExpired, QStringLiteral("bad key"), 401));
            return {};
        }
        if (nextError.code != ProviderError::Code::None) {
            const ProviderError e = nextError;
            nextError = {};
            cb({}, e);
            return {};
        }
        SearchPage p;
        for (int i = 0; i < r.pageSize && (r.page - 1) * r.pageSize + i < total; ++i) {
            AssetItem a;
            a.providerId = m_id;
            a.id = QString::number((r.page - 1) * r.pageSize + i);
            a.kind = r.kind;
            a.title = m_id + a.id;
            a.downloadUrl = downloadUrl;
            p.items.append(a);
        }
        p.hasMore = r.page * r.pageSize < total;
        cb(p, ProviderError::none());
        return {};
    }

    int total = 5;
    int calls = 0;
    QUrl downloadUrl;
    ProviderError nextError;

private:
    QString m_id;
    QList<AssetKind> m_kinds;
    bool m_requires;
};

// A provider that answers only when the test says so, like one still waiting on the network.
class SlowProvider : public AssetProvider
{
public:
    explicit SlowProvider(ProviderContext *ctx) : AssetProvider(ctx) {}

    ProviderInfo info() const override
    {
        ProviderInfo i;
        i.id = QStringLiteral("slow");
        i.name = QStringLiteral("Slow");
        i.kinds = {AssetKind::Sticker};
        return i;
    }
    RequestHandle search(const SearchRequest &, SearchCallback cb) override
    {
        RequestHandle handle;
        waiting.append({handle.flag(), std::move(cb)});
        return handle;
    }
    void answerAll()
    {
        const auto pending = std::exchange(waiting, {});
        for (const Waiting &w : pending) {
            if (!*w.cancelled)
                w.callback({}, ProviderError::none());
        }
    }

    struct Waiting
    {
        std::shared_ptr<bool> cancelled;
        SearchCallback callback;
    };
    QList<Waiting> waiting;
};

} // namespace

class TstOnlineAssets : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("DafinTest"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_onlineassets"));
    }

    // ─── parsers ─────────────────────────────────────────────────────────────────────────────

    void pexelsPicksRenditionsAndCredits()
    {
        const SearchPage p = PexelsProvider::parse(json(kPexelsPage), 2);
        QCOMPARE(p.items.size(), 1);
        const AssetItem &a = p.items.first();
        QCOMPARE(a.key(), QStringLiteral("pexels:7"));
        QCOMPARE(a.title, QStringLiteral("A dog on a beach"));
        QCOMPARE(a.downloadUrl.toString(), QStringLiteral("https://v/hd.mp4"));
        QCOMPARE(a.previewUrl.toString(), QStringLiteral("https://v/sd.mp4"));
        QCOMPARE(a.height, 1080);
        QCOMPARE(a.attribution, QStringLiteral("Video by Ann on Pexels"));
        QVERIFY(p.hasMore);
    }

    void pixabayFallsBackToMediumAndPages()
    {
        const SearchPage p = PixabayProvider::parse(json(kPixabayPage), 1, 2);
        QCOMPARE(p.items.size(), 1);
        QCOMPARE(p.items.first().downloadUrl.toString(), QStringLiteral("https://p/m.mp4"));
        QCOMPARE(p.items.first().title, QStringLiteral("Sea, waves, beach"));
        QCOMPARE(p.items.first().thumbnailUrl.toString(), QStringLiteral("https://p/t.jpg"));
        QVERIFY(p.hasMore); // 1*2 < 3
        QVERIFY(!PixabayProvider::parse(json(kPixabayEmpty), 1, 2).hasMore);
    }

    void freesoundLicencesAndNames()
    {
        QCOMPARE(FreesoundProvider::licenseName(QStringLiteral("http://creativecommons.org/publicdomain/zero/1.0/")), QStringLiteral("CC0 1.0"));
        QCOMPARE(FreesoundProvider::licenseName(QStringLiteral("https://creativecommons.org/licenses/by/4.0/")), QStringLiteral("CC BY 4.0"));
        QCOMPARE(FreesoundProvider::licenseName(QStringLiteral("https://creativecommons.org/licenses/by-nc/4.0/")), QStringLiteral("CC BY-NC 4.0"));
        const SearchPage p = FreesoundProvider::parse(json(kFreesoundPage));
        QCOMPARE(p.items.size(), 1);
        QCOMPARE(p.items.first().title, QStringLiteral("door slam"));
        QCOMPARE(p.items.first().downloadUrl.toString(), QStringLiteral("https://f/hq.mp3"));
        QCOMPARE(p.items.first().thumbnailUrl.toString(), QStringLiteral("https://f/w.png"));
        QVERIFY(p.hasMore);
    }

    void jamendoRespectsDownloadPermission()
    {
        ProviderError error;
        const SearchPage p = JamendoProvider::parse(json(kJamendoPage), 2, &error);
        QVERIFY(error.ok());
        QCOMPARE(p.items.size(), 2);
        QVERIFY(p.items.at(0).downloadable);
        QCOMPARE(p.items.at(0).license, QStringLiteral("CC BY-SA"));
        QVERIFY(!p.items.at(1).downloadable);
        QVERIFY(!p.items.at(1).downloadBlockedReason.isEmpty());
        QCOMPARE(p.items.at(1).durationSeconds, 90.0);

        JamendoProvider::parse(json(kJamendoFailed), 2, &error);
        QCOMPARE(error.code, ProviderError::Code::AuthExpired);
    }

    void openverseConvertsDurationAndLicence()
    {
        const SearchPage p = OpenverseProvider::parse(json(kOpenversePage),
            AssetKind::SoundEffect);
        QCOMPARE(p.items.size(), 1);
        QCOMPARE(p.items.first().durationSeconds, 61.5);
        QCOMPARE(p.items.first().license, QStringLiteral("CC BY 4.0"));
        QVERIFY(p.items.first().attribution.contains(QLatin1String("Kim")));
        QVERIFY(p.hasMore);
    }

    void dafinLibraryIndexResolvesAndFilters()
    {
        const QList<AssetItem> items = DafinLibraryProvider::parseIndex(json(kDafinIndex), QUrl(QStringLiteral("qrc:/asset-library/index.json")));
        QCOMPARE(items.size(), 3);
        QCOMPARE(items.at(0).downloadUrl.toString(), QStringLiteral("qrc:/asset-library/stickers/heart.json"));
        QCOMPARE(items.at(2).downloadUrl.toString(), QStringLiteral("https://raw.example/wave.json"));
        QVERIFY(items.at(1).extra.value(QStringLiteral("editableText")).toBool());
        QVERIFY(items.at(0).attribution.isEmpty());       // CC0 needs no credit
        QVERIFY(!items.at(2).attribution.isEmpty());      // Apache-2.0 keeps one

        SearchRequest r;
        r.kind = AssetKind::Sticker;
        r.pageSize = 1;
        SearchPage page = DafinLibraryProvider::filter(items, r);
        QCOMPARE(page.items.size(), 1);
        QVERIFY(page.hasMore);
        r.query = QStringLiteral("love");
        page = DafinLibraryProvider::filter(items, r);
        QCOMPARE(page.items.size(), 1);
        QCOMPARE(page.items.first().id, QStringLiteral("heart"));
    }

    void bundledLibraryIsValid()
    {
        // The generated library shipped in the app: every entry parses and points at a real file.
        QFile f(QStringLiteral(DAFIN_SOURCE_DIR "/asset-library/index.json"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        const QList<AssetItem> items = DafinLibraryProvider::parseIndex(doc, QUrl::fromLocalFile(QStringLiteral(DAFIN_SOURCE_DIR "/asset-library/index.json")));
        int local = 0;
        for (const AssetItem &a : items) {
            if (!a.downloadUrl.isLocalFile())
                continue;
            ++local;
            QFile lottie(a.downloadUrl.toLocalFile());
            QVERIFY2(lottie.open(QIODevice::ReadOnly), qPrintable(a.id));
            QVERIFY2(DownloadManager::looksLikeLottie(lottie.readAll()), qPrintable(a.id));
        }
        QVERIFY(local >= 10);
    }

    void catalogLookupPicksRenditions()
    {
        const QJsonValue v = json(kCatalogItem).object();
        QCOMPARE(CatalogProvider::lookup(v, QStringLiteral("files{width<=1920}.link")).toString(), QStringLiteral("hd"));
        QCOMPARE(CatalogProvider::lookup(v, QStringLiteral("files{width>=700}.link")).toString(), QStringLiteral("hd"));
        QCOMPARE(CatalogProvider::lookup(v, QStringLiteral("files.0.link")).toString(), QStringLiteral("4k"));
        QCOMPARE(CatalogProvider::lookup(v, QStringLiteral("user.name")).toString(), QStringLiteral("Ann"));
        QVERIFY(CatalogProvider::lookup(v, QStringLiteral("nope.x")).isNull());
    }

    void prettyTitles()
    {
        QCOMPARE(util::prettyTitle(QStringLiteral("https://www.pexels.com/video/city-at-night-99/")), QStringLiteral("City at night"));
        QCOMPARE(util::safeFileName(QStringLiteral("../../etc/passwd")), QStringLiteral("_etc_passwd"));
    }

    // ─── credentials & auth ──────────────────────────────────────────────────────────────────

    void fileSecretStoreRoundTrip()
    {
        QTemporaryDir dir;
        auto store = SecretStore::createFileStore(dir.path());
        QVERIFY(store->write(QStringLiteral("a"), QByteArray("s3cret\0bin", 10)));
        QCOMPARE(store->read(QStringLiteral("a")).value(), QByteArray("s3cret\0bin", 10));
        QVERIFY(!store->read(QStringLiteral("missing")).has_value());
        QVERIFY(store->remove(QStringLiteral("a")));
        QVERIFY(!store->read(QStringLiteral("a")).has_value());
#ifndef Q_OS_WIN
        const auto perms = QFile::permissions(dir.path() + QStringLiteral("/credentials.json"));
        QVERIFY(!(perms & (QFile::ReadGroup | QFile::ReadOther)));
#endif
    }

    void authConnectExpireLogoutRestore()
    {
        QTemporaryDir dir;
        QNetworkAccessManager net;
        {
            AuthManager auth(SecretStore::createFileStore(dir.path()));
            ProviderRegistry registry(&net, &auth);
            auth.setRegistry(&registry);
            registry.add(std::make_unique<FakeProvider>(&registry, QStringLiteral("stock"), QList<AssetKind>{AssetKind::Video}, true));
            auth.restore();
            QCOMPARE(auth.state(QStringLiteral("stock")), ConnectionState::NotConnected);

            QSignalSpy finished(&auth, &AuthManager::authFinished);
            auth.connectProvider(QStringLiteral("stock"), {{QStringLiteral("apiKey"), QStringLiteral("wrong")}});
            QCOMPARE(finished.size(), 1);
            QCOMPARE(finished.last().at(1).toBool(), false);
            QVERIFY(auth.credentials(QStringLiteral("stock")).isEmpty()); // a rejected key is not kept
            QCOMPARE(auth.state(QStringLiteral("stock")), ConnectionState::NotConnected);

            auth.connectProvider(QStringLiteral("stock"), {{QStringLiteral("apiKey"), QStringLiteral("good")}});
            QCOMPARE(finished.last().at(1).toBool(), true);
            QCOMPARE(auth.state(QStringLiteral("stock")), ConnectionState::Connected);
            QCOMPARE(auth.storedFieldIds(QStringLiteral("stock")), QStringList{QStringLiteral("apiKey")});

            auth.markExpired(QStringLiteral("stock"), QStringLiteral("rejected"));
            QCOMPARE(auth.state(QStringLiteral("stock")), ConnectionState::Expired);
            auth.reconnect(QStringLiteral("stock"));
            QCOMPARE(auth.state(QStringLiteral("stock")), ConnectionState::Connected);
        }
        {
            // "Restart": a new manager over the same store restores the session silently.
            AuthManager auth(SecretStore::createFileStore(dir.path()));
            ProviderRegistry registry(&net, &auth);
            auth.setRegistry(&registry);
            registry.add(std::make_unique<FakeProvider>(&registry, QStringLiteral("stock"), QList<AssetKind>{AssetKind::Video}, true));
            auth.restore();
            QCOMPARE(auth.state(QStringLiteral("stock")), ConnectionState::Connected);
            auth.logout(QStringLiteral("stock"));
            QCOMPARE(auth.state(QStringLiteral("stock")), ConnectionState::NotConnected);
        }
        {
            AuthManager auth(SecretStore::createFileStore(dir.path()));
            QVERIFY(auth.credentials(QStringLiteral("stock")).isEmpty()); // logout removed it from disk
        }
    }

    void legacyKeysMoveToSecureStore()
    {
        QTemporaryDir dir;
        QSettings().setValue(QStringLiteral("FreeLibrary/keys/pexels-video"), QStringLiteral("k1"));
        QSettings().setValue(QStringLiteral("FreeLibrary/keys/custom"), QStringLiteral("k2"));
        AuthManager auth(SecretStore::createFileStore(dir.path()));
        auth.migrateLegacyKeys();
        QCOMPARE(auth.credentials(QStringLiteral("pexels")).value(QStringLiteral("apiKey")).toString(), QStringLiteral("k1"));
        QCOMPARE(auth.credentials(QStringLiteral("catalog-custom")).value(QStringLiteral("apiKey")).toString(), QStringLiteral("k2"));
        QVERIFY(!QSettings().contains(QStringLiteral("FreeLibrary/keys/pexels-video")));
    }

    // ─── search model ────────────────────────────────────────────────────────────────────────

    void modelInterleavesPagesAndListsLockedProviders()
    {
        QTemporaryDir dir;
        QNetworkAccessManager net;
        AuthManager auth(SecretStore::createFileStore(dir.path()));
        ProviderRegistry registry(&net, &auth);
        auth.setRegistry(&registry);
        auto a = std::make_unique<FakeProvider>(&registry, QStringLiteral("a"), QList<AssetKind>{AssetKind::Video});
        auto b = std::make_unique<FakeProvider>(&registry, QStringLiteral("b"), QList<AssetKind>{AssetKind::Video});
        a->total = 3;
        b->total = 10;
        FakeProvider *bp = b.get();
        registry.add(std::move(a));
        registry.add(std::move(b));
        registry.add(std::make_unique<FakeProvider>(&registry, QStringLiteral("locked"), QList<AssetKind>{AssetKind::Video}, true));
        auth.restore();
        AssetSearchService service(&registry, &auth);
        AssetSearchModel::setService(&service);

        AssetSearchModel model;
        model.setKinds({QStringLiteral("video")});
        model.setPageSize(6);
        model.refresh();
        QCOMPARE(model.rowCount(), 9); // a: 3, b: 6
        QCOMPARE(model.get(0).value(QStringLiteral("providerId")).toString(), QStringLiteral("a"));
        QCOMPARE(model.get(1).value(QStringLiteral("providerId")).toString(), QStringLiteral("b"));
        const QVariantList problems = model.problems();
        QCOMPARE(problems.size(), 1);
        QCOMPARE(problems.first().toMap().value(QStringLiteral("code")).toString(), QStringLiteral("authRequired"));

        QVERIFY(model.canFetchMore({}));
        model.fetchMore({});
        QCOMPARE(model.rowCount(), 13); // b's remaining 4
        QVERIFY(!model.canFetchMore({}));

        // One provider only: page 1 was fetched above, so it comes from the response cache.
        const int before = bp->calls;
        model.setProviderId(QStringLiteral("b"));
        model.refresh();
        QCOMPARE(model.rowCount(), 6);
        QCOMPARE(bp->calls, before);
        model.setQuery(QStringLiteral("new words"));
        model.refresh();
        QCOMPARE(bp->calls, before + 1);
        AssetSearchModel::setService(nullptr);
    }

    void rateLimitedProviderIsSkippedUntilRetryAfter()
    {
        QTemporaryDir dir;
        QNetworkAccessManager net;
        AuthManager auth(SecretStore::createFileStore(dir.path()));
        ProviderRegistry registry(&net, &auth);
        auth.setRegistry(&registry);
        auto p = std::make_unique<FakeProvider>(&registry, QStringLiteral("busy"), QList<AssetKind>{AssetKind::Video});
        FakeProvider *fp = p.get();
        registry.add(std::move(p));
        AssetSearchService service(&registry, &auth);

        fp->nextError = ProviderError::make(ProviderError::Code::RateLimited, QStringLiteral("slow down"), 429, 120);
        ProviderError seen;
        SearchRequest r;
        r.query = QStringLiteral("x");
        service.search(QStringLiteral("busy"), r, [&](const SearchPage &, const ProviderError &e) { seen = e; });
        QCOMPARE(seen.code, ProviderError::Code::RateLimited);
        QVERIFY(service.blockedSeconds(QStringLiteral("busy")) >= 100);
        const int calls = fp->calls;
        r.query = QStringLiteral("y");
        service.search(QStringLiteral("busy"), r, [&](const SearchPage &, const ProviderError &e) { seen = e; });
        QCOMPARE(seen.code, ProviderError::Code::RateLimited);
        QCOMPARE(fp->calls, calls); // not asked again while blocked
    }

    // A tab's grid (its model) goes away when another tab opens, often while a provider is still
    // waiting on the network. Its searches are cancelled then, so the late answer is dropped instead
    // of landing in a deleted model (the Windows crash of 2026-11-10, build 32).
    void modelCancelsItsSearchesWhenDestroyed()
    {
        QTemporaryDir dir;
        QNetworkAccessManager net;
        AuthManager auth(SecretStore::createFileStore(dir.path()));
        ProviderRegistry registry(&net, &auth);
        auth.setRegistry(&registry);
        auto p = std::make_unique<SlowProvider>(&registry);
        SlowProvider *slow = p.get();
        registry.add(std::move(p));
        auth.restore();
        AssetSearchService service(&registry, &auth);
        AssetSearchModel::setService(&service);

        auto model = std::make_unique<AssetSearchModel>();
        model->setKinds({QStringLiteral("sticker")});
        model->refresh();
        QVERIFY(model->loading());
        QCOMPARE(slow->waiting.size(), qsizetype(1));

        model.reset();
        QVERIFY(*slow->waiting.first().cancelled);
        slow->answerAll(); // what every provider does with a cancelled request: nothing
        AssetSearchModel::setService(nullptr);
    }

    // The same with the real Dafin library, whose index comes from the network: the tab closes
    // before the index arrives.
    void libraryIndexArrivingAfterTheTabClosed()
    {
        QTemporaryDir dir;
        const QString indexPath = dir.path() + QStringLiteral("/index.json");
        {
            QFile f(indexPath);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(kDafinIndex);
        }
        QNetworkAccessManager net;
        AuthManager auth(SecretStore::createFileStore(dir.path()));
        ProviderRegistry registry(&net, &auth);
        auth.setRegistry(&registry);
        auto p = std::make_unique<DafinLibraryProvider>(&registry, QUrl::fromLocalFile(indexPath));
        DafinLibraryProvider *library = p.get();
        registry.add(std::move(p));
        auth.restore();
        AssetSearchService service(&registry, &auth);
        AssetSearchModel::setService(&service);

        auto model = std::make_unique<AssetSearchModel>();
        model->setKinds({QStringLiteral("sticker")});
        model->refresh();
        QVERIFY(model->loading());
        model.reset();

        // Once the index is in, the library answers at once; by then the late answer for the
        // closed tab has been delivered (and dropped).
        bool answered = false;
        QTRY_VERIFY_WITH_TIMEOUT(([&] {
                                     SearchRequest r;
                                     r.kind = AssetKind::Sticker;
                                     answered = false;
                                     library->search(r, [&](const SearchPage &, const ProviderError &) { answered = true; });
                                     return answered;
                                 }()),
                                 5000);
        AssetSearchModel::setService(nullptr);
    }

    // ─── downloads & database ────────────────────────────────────────────────────────────────

    void downloadRecordsImportsAndReuses()
    {
        QTemporaryDir dir;
        QTemporaryDir src;
        const QString lottiePath = src.path() + QStringLiteral("/heart.json");
        {
            QFile f(lottiePath);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(kMiniLottie);
        }
        QNetworkAccessManager net;
        AuthManager auth(SecretStore::createFileStore(dir.path()));
        ProviderRegistry registry(&net, &auth);
        auth.setRegistry(&registry);
        auto p = std::make_unique<FakeProvider>(&registry, QStringLiteral("lib"), QList<AssetKind>{AssetKind::Sticker});
        registry.add(std::move(p));
        LocalAssetDatabase db(dir.path() + QStringLiteral("/assets.json"));
        DownloadManager downloads(&net, &registry, &db);
        QStringList imported;
        downloads.setImporter([&](const QStringList &paths) { imported << paths; return QStringList{QStringLiteral("bin-1")}; });

        AssetItem item;
        item.providerId = QStringLiteral("lib");
        item.id = QStringLiteral("heart");
        item.kind = AssetKind::Sticker;
        item.title = QStringLiteral("Heart");
        item.license = QStringLiteral("CC0 1.0");
        item.downloadUrl = QUrl::fromLocalFile(lottiePath);

        QSignalSpy done(&downloads, &DownloadManager::downloaded);
        downloads.enqueue(item);
        QVERIFY(done.wait(5000));
        QCOMPARE(done.first().at(1).toString(), QStringLiteral("bin-1"));
        const QString saved = done.first().at(2).toString();
        QVERIFY(session::isSessionFile(saved));
        QCOMPARE(downloads.state(item.key()), DownloadManager::State::Done);
        const auto record = db.find(item.key());
        QVERIFY(record.has_value());
        QCOMPARE(record->license, QStringLiteral("CC0 1.0"));
        QCOMPARE(record->storage, QStringLiteral("session"));

        // Again: no new transfer, just a fresh import.
        downloads.enqueue(item);
        QCOMPARE(done.size(), 2);
        QCOMPARE(imported.size(), 2);

        // Persisted, then reconciled away once the file is gone.
        db.flush();
        LocalAssetDatabase reread(dir.path() + QStringLiteral("/assets.json"));
        reread.load();
        QVERIFY(reread.find(item.key()).has_value());
        QFile::remove(saved);
        QCOMPARE(reread.reconcile(), 1);
        QVERIFY(!reread.find(item.key()).has_value());
    }

    void stickerThatIsNotLottieIsRejected()
    {
        QTemporaryDir dir;
        QTemporaryDir src;
        const QString path = src.path() + QStringLiteral("/page.json");
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(kNotLottie);
        }
        QNetworkAccessManager net;
        AuthManager auth(SecretStore::createFileStore(dir.path()));
        ProviderRegistry registry(&net, &auth);
        auth.setRegistry(&registry);
        registry.add(std::make_unique<FakeProvider>(&registry, QStringLiteral("lib"), QList<AssetKind>{AssetKind::Sticker}));
        LocalAssetDatabase db(dir.path() + QStringLiteral("/assets.json"));
        DownloadManager downloads(&net, &registry, &db);
        AssetItem item;
        item.providerId = QStringLiteral("lib");
        item.id = QStringLiteral("page");
        item.kind = AssetKind::Sticker;
        item.downloadUrl = QUrl::fromLocalFile(path);
        QSignalSpy failed(&downloads, &DownloadManager::failed);
        downloads.enqueue(item);
        QVERIFY(failed.wait(5000));
        QCOMPARE(downloads.state(item.key()), DownloadManager::State::Failed);
        QVERIFY(!db.find(item.key()).has_value());
    }

    void databaseIgnoresNewerSchema()
    {
        QTemporaryDir dir;
        const QString path = dir.path() + QStringLiteral("/assets.json");
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(kNewerSchema);
        f.close();
        LocalAssetDatabase db(path);
        db.load();
        QVERIFY(db.all().isEmpty());
    }

    void cleanupTestCase()
    {
        session::remove();
    }
};

QTEST_MAIN(TstOnlineAssets)
#include "tst_onlineassets.moc"
