#include "app/online/library/SecretStore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibrary>
#include <QSaveFile>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dpapi.h>
#endif

namespace cadra::assets {

namespace {

[[maybe_unused]] QString hashedName(const QString &key)
{
    return QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256).toHex().left(32));
}

// ─── Fallback: a 0600 JSON file ──────────────────────────────────────────────────────────────
class FileSecretStore : public SecretStore
{
public:
    explicit FileSecretStore(const QString &dir)
        : m_path(dir + QStringLiteral("/credentials.json"))
    {
        QDir().mkpath(dir);
    }

    bool write(const QString &key, const QByteArray &secret) override
    {
        QJsonObject all = load();
        all.insert(key, QString::fromLatin1(secret.toBase64()));
        return save(all);
    }
    std::optional<QByteArray> read(const QString &key) override
    {
        const QJsonObject all = load();
        if (!all.contains(key))
            return std::nullopt;
        return QByteArray::fromBase64(all.value(key).toString().toLatin1());
    }
    bool remove(const QString &key) override
    {
        QJsonObject all = load();
        all.remove(key);
        return save(all);
    }
    QString backendName() const override { return QObject::tr("a private file (no system keyring found)"); }
    bool isEncrypted() const override { return false; }

private:
    QJsonObject load() const
    {
        QFile f(m_path);
        if (!f.open(QIODevice::ReadOnly))
            return {};
        return QJsonDocument::fromJson(f.readAll()).object();
    }
    bool save(const QJsonObject &all) const
    {
        QSaveFile f(m_path);
        if (!f.open(QIODevice::WriteOnly))
            return false;
        f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        f.write(QJsonDocument(all).toJson(QJsonDocument::Compact));
        if (!f.commit())
            return false;
        QFile::setPermissions(m_path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        return true;
    }
    QString m_path;
};

#ifdef Q_OS_WIN
// ─── Windows: DPAPI ──────────────────────────────────────────────────────────────────────────
class DpapiSecretStore : public SecretStore
{
public:
    explicit DpapiSecretStore(const QString &dir) : m_dir(dir) { QDir().mkpath(dir); }

    bool write(const QString &key, const QByteArray &secret) override
    {
        DATA_BLOB in{DWORD(secret.size()), reinterpret_cast<BYTE *>(const_cast<char *>(secret.constData()))};
        DATA_BLOB entropy = entropyBlob();
        DATA_BLOB out{};
        if (!CryptProtectData(&in, L"Cadra asset provider", &entropy, nullptr, nullptr,
                              CRYPTPROTECT_UI_FORBIDDEN, &out))
            return false;
        const QByteArray blob(reinterpret_cast<const char *>(out.pbData), int(out.cbData));
        LocalFree(out.pbData);
        QSaveFile f(path(key));
        if (!f.open(QIODevice::WriteOnly))
            return false;
        f.write(blob);
        return f.commit();
    }
    std::optional<QByteArray> read(const QString &key) override
    {
        QFile f(path(key));
        if (!f.open(QIODevice::ReadOnly))
            return std::nullopt;
        QByteArray blob = f.readAll();
        DATA_BLOB in{DWORD(blob.size()), reinterpret_cast<BYTE *>(blob.data())};
        DATA_BLOB entropy = entropyBlob();
        DATA_BLOB out{};
        if (!CryptUnprotectData(&in, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
            return std::nullopt;
        QByteArray plain(reinterpret_cast<const char *>(out.pbData), int(out.cbData));
        SecureZeroMemory(out.pbData, out.cbData);
        LocalFree(out.pbData);
        return plain;
    }
    bool remove(const QString &key) override { return QFile::remove(path(key)) || !QFile::exists(path(key)); }
    QString backendName() const override { return QObject::tr("Windows Data Protection (DPAPI)"); }
    bool isEncrypted() const override { return true; }

private:
    QString path(const QString &key) const { return m_dir + QLatin1Char('/') + hashedName(key) + QStringLiteral(".dpapi"); }
    static DATA_BLOB entropyBlob()
    {
        static QByteArray e("cadra/asset-providers/v1");
        return DATA_BLOB{DWORD(e.size()), reinterpret_cast<BYTE *>(e.data())};
    }
    QString m_dir;
};
#endif

#if defined(Q_OS_LINUX) || defined(Q_OS_FREEBSD)
// ─── Linux: Secret Service via libsecret, resolved at runtime ────────────────────────────────
// Mirrors libsecret's public C types (stable ABI since libsecret 0.7).
struct SecretSchemaAttribute { const char *name; int type; };
struct SecretSchema
{
    const char *name;
    int flags;
    SecretSchemaAttribute attributes[32];
    int reserved;
    void *reserved1, *reserved2, *reserved3, *reserved4, *reserved5, *reserved6, *reserved7;
};
struct GErrorLite { quint32 domain; int code; char *message; };

class LibsecretSecretStore : public SecretStore
{
public:
    using StoreFn = int (*)(const SecretSchema *, const char *, const char *, const char *, void *, GErrorLite **, ...);
    using LookupFn = char *(*)(const SecretSchema *, void *, GErrorLite **, ...);
    using ClearFn = int (*)(const SecretSchema *, void *, GErrorLite **, ...);
    using FreeFn = void (*)(char *);
    using ErrorFreeFn = void (*)(GErrorLite *);

    static std::unique_ptr<LibsecretSecretStore> open()
    {
        auto s = std::unique_ptr<LibsecretSecretStore>(new LibsecretSecretStore);
        s->m_lib.setFileNameAndVersion(QStringLiteral("secret-1"), 0);
        if (!s->m_lib.load())
            return nullptr;
        s->m_store = reinterpret_cast<StoreFn>(s->m_lib.resolve("secret_password_store_sync"));
        s->m_lookup = reinterpret_cast<LookupFn>(s->m_lib.resolve("secret_password_lookup_sync"));
        s->m_clear = reinterpret_cast<ClearFn>(s->m_lib.resolve("secret_password_clear_sync"));
        s->m_free = reinterpret_cast<FreeFn>(s->m_lib.resolve("secret_password_free"));
        QLibrary glib;
        glib.setFileNameAndVersion(QStringLiteral("glib-2.0"), 0);
        if (glib.load())
            s->m_errorFree = reinterpret_cast<ErrorFreeFn>(glib.resolve("g_error_free"));
        if (!s->m_store || !s->m_lookup || !s->m_clear || !s->m_free)
            return nullptr;
        // Probe: a lookup that fails with an error means no Secret Service is running.
        GErrorLite *error = nullptr;
        char *probe = s->m_lookup(&s->schema(), nullptr, &error, "key", "cadra-probe", nullptr);
        if (probe)
            s->m_free(probe);
        if (error) {
            s->freeError(error);
            return nullptr;
        }
        return s;
    }

    bool write(const QString &key, const QByteArray &secret) override
    {
        GErrorLite *error = nullptr;
        const QByteArray k = key.toUtf8();
        const QByteArray label = QByteArrayLiteral("Cadra: ") + k;
        const QByteArray value = secret.toBase64();
        const int ok = m_store(&schema(), nullptr, label.constData(), value.constData(), nullptr, &error,
                               "key", k.constData(), nullptr);
        if (error) {
            freeError(error);
            return false;
        }
        return ok != 0;
    }
    std::optional<QByteArray> read(const QString &key) override
    {
        GErrorLite *error = nullptr;
        const QByteArray k = key.toUtf8();
        char *value = m_lookup(&schema(), nullptr, &error, "key", k.constData(), nullptr);
        if (error) {
            freeError(error);
            return std::nullopt;
        }
        if (!value)
            return std::nullopt;
        const QByteArray out = QByteArray::fromBase64(QByteArray(value));
        m_free(value);
        return out;
    }
    bool remove(const QString &key) override
    {
        GErrorLite *error = nullptr;
        const QByteArray k = key.toUtf8();
        m_clear(&schema(), nullptr, &error, "key", k.constData(), nullptr);
        if (error) {
            freeError(error);
            return false;
        }
        return true;
    }
    QString backendName() const override { return QObject::tr("the system keyring (Secret Service)"); }
    bool isEncrypted() const override { return true; }

private:
    LibsecretSecretStore() = default;
    static const SecretSchema &schema()
    {
        static const SecretSchema s{"io.github.cadra.AssetProvider", 0, {{"key", 0}, {nullptr, 0}}, 0,
                                    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
        return s;
    }
    void freeError(GErrorLite *e) const
    {
        if (m_errorFree)
            m_errorFree(e);
    }
    QLibrary m_lib;
    StoreFn m_store = nullptr;
    LookupFn m_lookup = nullptr;
    ClearFn m_clear = nullptr;
    FreeFn m_free = nullptr;
    ErrorFreeFn m_errorFree = nullptr;
};
#endif

} // namespace

std::unique_ptr<SecretStore> SecretStore::createFileStore(const QString &directory)
{
    return std::make_unique<FileSecretStore>(directory);
}

std::unique_ptr<SecretStore> SecretStore::createBest(const QString &directory)
{
#ifdef Q_OS_WIN
    return std::make_unique<DpapiSecretStore>(directory);
#elif defined(Q_OS_LINUX) || defined(Q_OS_FREEBSD)
    if (qEnvironmentVariableIsEmpty("CADRA_PLAIN_SECRETS")) {
        if (auto store = LibsecretSecretStore::open())
            return store;
    }
    return createFileStore(directory);
#else
    return createFileStore(directory);
#endif
}

} // namespace cadra::assets
