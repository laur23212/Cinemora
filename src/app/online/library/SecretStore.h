#pragma once

#include <QByteArray>
#include <QString>

#include <memory>
#include <optional>

namespace cadra::assets {

// Where provider credentials are kept. The best available backend is chosen at startup:
//   Windows  — DPAPI (CryptProtectData, bound to the Windows user account), files in AppData
//   Linux    — the Secret Service keyring (GNOME Keyring, KWallet ≥ 5.97…) via libsecret,
//              loaded at runtime so Cadra builds and runs without it
//   fallback — a JSON file readable only by the user (0600). Not encrypted; the Settings page
//              says so. Used on macOS for now and on Linux without a running keyring.
class SecretStore
{
public:
    virtual ~SecretStore() = default;

    virtual bool write(const QString &key, const QByteArray &secret) = 0;
    virtual std::optional<QByteArray> read(const QString &key) = 0;
    virtual bool remove(const QString &key) = 0;

    virtual QString backendName() const = 0;
    virtual bool isEncrypted() const = 0;

    // `directory` holds the DPAPI blobs / fallback file.
    static std::unique_ptr<SecretStore> createBest(const QString &directory);
    static std::unique_ptr<SecretStore> createFileStore(const QString &directory);
};

} // namespace cadra::assets
