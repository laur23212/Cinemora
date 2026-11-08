#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>
#include <QVariantList>

class QProcess;

namespace dafin::assets {

// Renders an After Effects Motion Graphics Template (.mogrt) with its animation, through the free
// MOGRT Converter (github.com/nielsfranke/MOGRT-Converter, AGPL-3.0) when it is installed.
//
// Dafin itself cannot draw a template's animation: a .mogrt carries an After Effects project, and
// the converter is the only open renderer for those. It is a separate program, run on the command
// line (`mogrt render … --json`), so none of its code becomes part of Dafin. It writes a ProRes 4444
// file with alpha, which Drift's decoder plays with its transparency, and the clip goes on the
// timeline above the video like a title.
//
// What the converter can do is its own business and is reported back as it says: it renders
// templates made in After Effects (not Premiere Pro), skips effects it does not know with a
// warning, and does not recreate third-party plugins or true 3D.
class MogrtRenderer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY availabilityChanged)
    // The converter program in use, or empty.
    Q_PROPERTY(QString executable READ executable NOTIFY availabilityChanged)
    // A program the user pointed at in Settings; empty to look in the usual places.
    Q_PROPERTY(QString customPath READ customPath WRITE setCustomPath NOTIFY availabilityChanged)
    Q_PROPERTY(QString downloadUrl READ downloadUrl CONSTANT)
    // Bumped when any render starts, moves on or ends.
    Q_PROPERTY(int revision READ revision NOTIFY jobsChanged)

public:
    explicit MogrtRenderer(QObject *parent = nullptr);
    ~MogrtRenderer() override;

    bool available() const { return !m_executable.isEmpty(); }
    QString executable() const { return m_executable; }
    QString customPath() const;
    void setCustomPath(const QString &path);
    QString downloadUrl() const;
    int revision() const { return m_revision; }

    // File URL ⇄ local path, for QML.
    Q_INVOKABLE QString pathFor(const QUrl &url) const { return url.isLocalFile() ? url.toLocalFile() : url.toString(); }
    Q_INVOKABLE QUrl urlFor(const QString &path) const { return QUrl::fromLocalFile(path); }

    // Looks for the converter again (after installing it while Dafin runs).
    Q_INVOKABLE void refresh();
    // A file URL or path the user picked; true when it is a program that can be run.
    Q_INVOKABLE bool useProgram(const QString &pathOrUrl);

    // Starts rendering `mogrtPath`. `fields` is the template's text fields as the Titles panel
    // lists them ([{id, label, text}]); `durationSeconds` 0 keeps the template's own length.
    // Returns the job key, empty when it could not start (the reason goes out as failed()).
    Q_INVOKABLE QString render(const QString &mogrtPath, const QVariantList &fields, double durationSeconds,
                               const QString &title);
    Q_INVOKABLE void cancel(const QString &key);
    // "rendering", "done", "failed" or "" for a key it does not know.
    Q_INVOKABLE QString state(const QString &key) const;
    // 0..1 from the converter's frame count.
    Q_INVOKABLE double progress(const QString &key) const;
    Q_INVOKABLE QString message(const QString &key) const;

signals:
    void availabilityChanged();
    void jobsChanged();
    // The video is written; `warnings` are the converter's own notes (effects it skipped …).
    void rendered(const QString &key, const QString &outputPath, const QString &title, const QStringList &warnings);
    void failed(const QString &key, const QString &message);

private:
    struct Job
    {
        QPointer<QProcess> process;
        QString state;
        QString message;
        QString output;
        QString title;
        QByteArray stdoutData;
        QByteArray stderrTail;
        double progress = 0;
    };

    QString findExecutable() const;
    void finish(const QString &key, int exitCode, bool crashed);
    void bump();

    QString m_executable;
    QHash<QString, Job> m_jobs;
    int m_revision = 0;
    int m_counter = 0;
};

} // namespace dafin::assets
