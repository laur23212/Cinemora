#include <QtTest>

#include <QJsonArray>
#include <QJsonObject>

#include "app/online/updates/UpdateAsset.h"

class UpdateAssetTest : public QObject
{
    Q_OBJECT

private slots:
    void versionText();
    void windowsInstallerIgnoresPortableZip();
    void dafinWindowsInstaller();
    void macDiskImage();
    void linuxAppImageIgnoresZsync();
    void checksumFromSums();
    void rejectsNonGitHubUrl();
    void releaseUrlsFollowTheVersion();
    void installerFileName();
};

void UpdateAssetTest::versionText()
{
    QCOMPARE(drift::parseVersionText("0.7.5"), QStringLiteral("0.7.5"));
    QCOMPARE(drift::parseVersionText("\"0.7.5\""), QStringLiteral("0.7.5"));
    QCOMPARE(drift::parseVersionText("v0.7.5"), QStringLiteral("0.7.5"));
    QCOMPARE(drift::parseVersionText("  V1.2.3\n"), QStringLiteral("1.2.3"));
    QVERIFY(drift::parseVersionText("latest").isEmpty());
    QVERIFY(drift::parseVersionText("0.7.5-rc1").isEmpty());
}

void UpdateAssetTest::windowsInstallerIgnoresPortableZip()
{
    const QJsonArray assets{
            QJsonObject{{QStringLiteral("name"), QStringLiteral("Drift-Portable-1.2.0-x64.zip")},
                        {QStringLiteral("browser_download_url"),
                         QStringLiteral("https://github.com/CutWire-Studios/Drift/releases/download/v1.2.0/Drift-Portable-1.2.0-x64.zip")}},
            QJsonObject{{QStringLiteral("name"), QStringLiteral("Drift-Setup-1.2.0-x64.exe")},
                        {QStringLiteral("browser_download_url"),
                         QStringLiteral("https://github.com/CutWire-Studios/Drift/releases/download/v1.2.0/Drift-Setup-1.2.0-x64.exe")},
                        {QStringLiteral("digest"),
                         QStringLiteral("sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef")},
                        {QStringLiteral("size"), 42}},
            QJsonObject{{QStringLiteral("name"), QStringLiteral("Drift-1.2.0-arm64.dmg")},
                        {QStringLiteral("browser_download_url"),
                         QStringLiteral("https://github.com/CutWire-Studios/Drift/releases/download/v1.2.0/Drift-1.2.0-arm64.dmg")}}};

    const drift::ReleaseAsset asset = drift::selectReleaseAsset(assets, QStringLiteral("windows"),
                                                                QStringLiteral("x86_64"));
    QCOMPARE(asset.name, QStringLiteral("Drift-Setup-1.2.0-x64.exe"));
    QCOMPARE(asset.sha256,
             QStringLiteral("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));
    QCOMPARE(asset.size, qint64(42));
    // An x64 installer is not offered to an arm64 machine.
    QVERIFY(drift::selectReleaseAsset(assets, QStringLiteral("windows"), QStringLiteral("arm64")).url.isEmpty());

    QJsonArray withArm = assets;
    withArm.append(QJsonObject{{QStringLiteral("name"), QStringLiteral("Drift-Setup-1.2.0-arm64.exe")},
                               {QStringLiteral("browser_download_url"),
                                QStringLiteral("https://github.com/CutWire-Studios/Drift/releases/download/v1.2.0/Drift-Setup-1.2.0-arm64.exe")}});
    QCOMPARE(drift::selectReleaseAsset(withArm, QStringLiteral("windows"), QStringLiteral("arm64")).name,
             QStringLiteral("Drift-Setup-1.2.0-arm64.exe"));
    QCOMPARE(drift::selectReleaseAsset(withArm, QStringLiteral("windows"), QStringLiteral("x86_64")).name,
             QStringLiteral("Drift-Setup-1.2.0-x64.exe"));
}

// Dafin's release workflow uploads Dafin-Setup-<version>-x64.exe beside the portable zip.
void UpdateAssetTest::dafinWindowsInstaller()
{
    const QJsonArray assets{
            QJsonObject{{QStringLiteral("name"), QStringLiteral("Dafin-Portable-1.0.1-x64.zip")},
                        {QStringLiteral("browser_download_url"),
                         QStringLiteral("https://github.com/laur23212/Dafin/releases/download/v1.0.1/Dafin-Portable-1.0.1-x64.zip")}},
            QJsonObject{{QStringLiteral("name"), QStringLiteral("SHA256SUMS")},
                        {QStringLiteral("browser_download_url"),
                         QStringLiteral("https://github.com/laur23212/Dafin/releases/download/v1.0.1/SHA256SUMS")}},
            QJsonObject{{QStringLiteral("name"), QStringLiteral("Dafin-Setup-1.0.1-x64.exe")},
                        {QStringLiteral("browser_download_url"),
                         QStringLiteral("https://github.com/laur23212/Dafin/releases/download/v1.0.1/Dafin-Setup-1.0.1-x64.exe")},
                        {QStringLiteral("size"), 7}}};
    const drift::ReleaseAsset asset = drift::selectReleaseAsset(assets, QStringLiteral("windows"),
                                                                QStringLiteral("x86_64"));
    QCOMPARE(asset.name, QStringLiteral("Dafin-Setup-1.0.1-x64.exe"));
    QCOMPARE(asset.size, qint64(7));
    // No digest in the listing: the checker asks SHA256SUMS for it.
    QVERIFY(asset.sha256.isEmpty());
    const QString feed = QStringLiteral("https://api.github.com/repos/laur23212/Dafin/releases/latest");
    QCOMPARE(drift::releaseDownloadUrl(feed, QStringLiteral("1.0.1"), QStringLiteral("SHA256SUMS")),
             QStringLiteral("https://github.com/laur23212/Dafin/releases/download/v1.0.1/SHA256SUMS"));
    QCOMPARE(drift::releasePageUrl(feed, QStringLiteral("1.0.1")),
             QStringLiteral("https://github.com/laur23212/Dafin/releases/tag/v1.0.1"));
}

void UpdateAssetTest::macDiskImage()
{
    const QJsonArray assets{
            QJsonObject{{QStringLiteral("name"), QStringLiteral("Drift-Setup-1.2.0-x64.exe")},
                        {QStringLiteral("browser_download_url"),
                         QStringLiteral("https://github.com/CutWire-Studios/Drift/releases/download/v1.2.0/Drift-Setup-1.2.0-x64.exe")}},
            QJsonObject{{QStringLiteral("name"), QStringLiteral("Drift-1.2.0-arm64.dmg")},
                        {QStringLiteral("browser_download_url"),
                         QStringLiteral("https://github.com/CutWire-Studios/Drift/releases/download/v1.2.0/Drift-1.2.0-arm64.dmg")}}};

    const drift::ReleaseAsset asset = drift::selectReleaseAsset(assets, QStringLiteral("macos"),
                                                                QStringLiteral("arm64"));
    QCOMPARE(asset.name, QStringLiteral("Drift-1.2.0-arm64.dmg"));
}

void UpdateAssetTest::linuxAppImageIgnoresZsync()
{
    const QJsonArray assets{
            QJsonObject{{QStringLiteral("name"), QStringLiteral("Drift-1.2.0-x86_64.AppImage.zsync")},
                        {QStringLiteral("browser_download_url"),
                         QStringLiteral("https://github.com/CutWire-Studios/Drift/releases/download/v1.2.0/Drift-1.2.0-x86_64.AppImage.zsync")}},
            QJsonObject{{QStringLiteral("name"), QStringLiteral("Drift-1.2.0-x86_64.AppImage")},
                        {QStringLiteral("browser_download_url"),
                         QStringLiteral("https://github.com/CutWire-Studios/Drift/releases/download/v1.2.0/Drift-1.2.0-x86_64.AppImage")}}};

    const drift::ReleaseAsset asset = drift::selectReleaseAsset(assets, QStringLiteral("linux"),
                                                                QStringLiteral("x86_64"));
    QCOMPARE(asset.name, QStringLiteral("Drift-1.2.0-x86_64.AppImage"));
}

void UpdateAssetTest::checksumFromSums()
{
    const QByteArray sums =
            "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef  Drift-1.2.0-arm64.dmg\n"
            "ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789 *Drift-Setup-1.2.0-x64.exe\r\n";
    QCOMPARE(drift::sha256FromSums(sums, QStringLiteral("Drift-1.2.0-arm64.dmg")),
             QStringLiteral("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));
    QCOMPARE(drift::sha256FromSums(sums, QStringLiteral("Drift-Setup-1.2.0-x64.exe")),
             QStringLiteral("abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789"));
    QVERIFY(drift::sha256FromSums(sums, QStringLiteral("Drift-1.2.0-x86_64.AppImage")).isEmpty());
    QVERIFY(drift::sha256FromSums("not a listing", QStringLiteral("Drift-1.2.0-arm64.dmg")).isEmpty());
}

void UpdateAssetTest::rejectsNonGitHubUrl()
{
    const QJsonArray assets{
            QJsonObject{{QStringLiteral("name"), QStringLiteral("Drift-Setup-1.2.0-x64.exe")},
                        {QStringLiteral("browser_download_url"),
                         QStringLiteral("http://github.com/CutWire-Studios/Drift/releases/download/v1.2.0/Drift-Setup-1.2.0-x64.exe")}},
            QJsonObject{{QStringLiteral("name"), QStringLiteral("Drift-Setup-1.2.0-x64.exe")},
                        {QStringLiteral("browser_download_url"),
                         QStringLiteral("https://example.com/Drift-Setup-1.2.0-x64.exe")}}};
    QVERIFY(drift::selectReleaseAsset(assets, QStringLiteral("windows"), QStringLiteral("x86_64")).url.isEmpty());
}

void UpdateAssetTest::releaseUrlsFollowTheVersion()
{
    const QString feed = QStringLiteral(
            "https://api.github.com/repos/CutWire-Studios/Drift/releases/latest");
    QCOMPARE(drift::releaseTagApiUrl(feed, QStringLiteral("0.7.5")),
             QStringLiteral("https://api.github.com/repos/CutWire-Studios/Drift/releases/tags/v0.7.5"));
    QCOMPARE(drift::releasePageUrl(feed, QStringLiteral("0.7.5")),
             QStringLiteral("https://github.com/CutWire-Studios/Drift/releases/tag/v0.7.5"));
    QCOMPARE(drift::releaseDownloadUrl(feed, QStringLiteral("0.7.5"),
                                       QStringLiteral("Drift-Setup-0.7.5-x64.exe")),
             QStringLiteral("https://github.com/CutWire-Studios/Drift/releases/download/v0.7.5/Drift-Setup-0.7.5-x64.exe"));
}

void UpdateAssetTest::installerFileName()
{
    QCOMPARE(drift::installerFileName(QStringLiteral("windows"), QStringLiteral("x86_64"),
                                      QStringLiteral("0.7.5")),
             QStringLiteral("Dafin-Setup-0.7.5-x64.exe"));
    QCOMPARE(drift::installerFileName(QStringLiteral("macos"), QStringLiteral("arm64"),
                                      QStringLiteral("0.7.5")),
             QStringLiteral("Drift-0.7.5-arm64.dmg"));
    QCOMPARE(drift::installerFileName(QStringLiteral("linux"), QStringLiteral("x86_64"),
                                      QStringLiteral("0.7.5")),
             QStringLiteral("Drift-0.7.5-x86_64.AppImage"));
    QCOMPARE(drift::installerFileName(QStringLiteral("linux"), QStringLiteral("arm64"),
                                      QStringLiteral("0.7.5")),
             QStringLiteral("Drift-0.7.5-aarch64.AppImage"));
    QCOMPARE(drift::installerFileName(QStringLiteral("windows"), QStringLiteral("arm64"),
                                      QStringLiteral("0.7.5")),
             QStringLiteral("Dafin-Setup-0.7.5-arm64.exe"));
    QCOMPARE(drift::installerFileName(QStringLiteral("macos"), QStringLiteral("x86_64"),
                                      QStringLiteral("0.7.5")),
             QStringLiteral("Drift-0.7.5-x86_64.dmg"));
}

QTEST_GUILESS_MAIN(UpdateAssetTest)
#include "tst_updateasset.moc"
