#pragma once

#include <QDateTime>
#include <QFuture>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <memory>

// Dafin: finds video projects on this computer for the start screen — Dafin's own .drift, Filmora
// .wfp and Premiere Pro .prproj (both of which Dafin opens) — and reads what a card shows: the
// length of the edit and a picture.
//
// The walk and the reading run off the GUI thread. The file list comes first, so cards appear at
// once with name and date; length and picture follow in small batches. What was read is kept in
// <AppData>/project-scan.json by path, size and modification time, so the next start only reads
// files that changed.
//
// Where it looks: Documents, Videos, Desktop and Downloads (Filmora's and Premiere's default
// project folders are inside Documents), plus folders added in Settings › General, each up to
// kMaxDepth folders deep. Hidden folders, Premiere's Auto-Save folders and the recycle bin are
// skipped. Reached from QML as EditorState.projectFile.scanner.
class ProjectScanner : public QObject
{
    Q_OBJECT

    // One map per project, newest first: path, name, kind ("dafin", "filmora", "premiere"),
    // modified, sizeBytes, durationUs (0 = not known yet), thumbnail (file URL or ""), exists.
    Q_PROPERTY(QVariantList projects READ projects NOTIFY projectsChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    // Folders the user added (Settings › General › Project folders).
    Q_PROPERTY(QStringList folders READ folders NOTIFY foldersChanged)
    // The standard folders always searched, for the settings page to name.
    Q_PROPERTY(QStringList defaultFolders READ defaultFolders CONSTANT)
    // The start screen's last choice of "grid" or "list", kept across starts.
    Q_PROPERTY(QString startView READ startView WRITE setStartView NOTIFY startViewChanged)

public:
    static constexpr int kMaxDepth = 6;
    static constexpr int kMaxFiles = 500;
    // A walk stops after this many folders, so a huge tree can't keep it going for minutes.
    static constexpr int kMaxFolders = 20000;

    struct Info
    {
        QString path;
        QString kind;
        QDateTime modified;
        qint64 sizeBytes = 0;
        qint64 durationUs = 0;
        QString cover;      // a local image file, or empty
        bool inspected = false;
    };

    explicit ProjectScanner(QObject *parent = nullptr);
    ~ProjectScanner() override;

    QVariantList projects() const;
    bool scanning() const { return m_scanning; }
    bool enabled() const;
    void setEnabled(bool enabled);
    QStringList folders() const;
    QStringList defaultFolders() const;
    QString startView() const;
    void setStartView(const QString &view);

    // Looks again (the start screen calls this when it opens and from its Refresh button). A
    // call while a scan runs makes that scan start over once it ends.
    Q_INVOKABLE void rescan();
    Q_INVOKABLE bool addFolder(const QUrl &folder);
    Q_INVOKABLE void removeFolder(const QString &folder);
    // "Remove from list" on a found project: it is not shown again (the file is left alone).
    Q_INVOKABLE void hide(const QString &path);
    Q_INVOKABLE void unhideAll();
    // Opens the folder holding `path` in the system file manager (Explorer selects the file).
    Q_INVOKABLE void revealInFolder(const QString &path) const;

    // What is known about a file (also files outside the search folders, such as recents, which
    // setExtraFiles() asks to be read). Empty Info when nothing is.
    Info info(const QString &path) const;
    void setExtraFiles(const QStringList &paths);

    // True for a file whose contents are in the cloud only (OneDrive "files on demand" and the
    // like, on Windows): reading it would download it, so such files are listed, never read.
    static bool isCloudOnly(const QString &path);
    // "dafin", "filmora", "premiere" or "" by the file's extension.
    static QString kindFor(const QString &path);
    // Every project file under `roots`, at most `maxDepth` folders down and `maxFiles` in all.
    static QStringList findProjectFiles(const QStringList &roots, int maxDepth = kMaxDepth,
                                        int maxFiles = kMaxFiles,
                                        const std::atomic_bool *cancel = nullptr);
    // Reads a project's length and picks its picture. `thumbDir` is where Dafin keeps the
    // thumbnail it writes when a .drift is saved (<AppData>/thumbs/<projectId>.jpg).
    static Info inspect(const QString &path, const QString &thumbDir);

signals:
    void projectsChanged();
    void scanningChanged();
    void enabledChanged();
    void foldersChanged();
    void startViewChanged();
    // Something about a file changed (length, picture): recents read it too.
    void infoChanged();

private:
    QStringList searchRoots() const;
    QString cacheFile() const;
    QString thumbDir() const;
    void loadCache();
    void saveCache() const;
    void setScanning(bool scanning);
    void listFound(const QStringList &paths);
    void applyInfos(const QList<Info> &infos);
    void finishScan();

    QHash<QString, Info> m_cache;
    QStringList m_found;            // paths, as found
    QStringList m_extraFiles;
    bool m_scanning = false;
    bool m_rescanQueued = false;
    std::shared_ptr<std::atomic_bool> m_cancel;
    QFuture<void> m_scan;
};
