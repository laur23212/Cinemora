#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QString>
#include <QUrl>
#include <QVariantList>



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
// Every run goes through submit(); MogrtTitleController turns template values into runs and keeps
// the results. What the converter can do is its own business and is reported back as it says: it renders
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

    // One run of the converter. `verb` is "info", "still" or "render". `values` holds the controls'
    // values by control id, typed as the converter reads them (text, number, bool, [r,g,b,a] 0..1,
    // [x,y]); controls left out keep the template's default. A still or render is written to a
    // temporary name beside `output` and renamed onto it only once complete, so a reader never sees
    // half a file. `durationSeconds` 0 keeps the template's own length.
    struct Request
    {
        QString verb;
        QString templatePath;
        QJsonObject values;
        QString output;
        double durationSeconds = 0;
        double timeSeconds = -1;  // still: -1 for the middle
        double scale = 1.0;
        int priority = 0;         // higher starts first
    };
    // Queues a run and returns its key. At most two runs at a time: each is a Python process
    // drawing every frame on the CPU.
    QString submit(const Request &request);
    // Stops a queued or running job; it ends with jobFinished(…, false, …, "Cancelled.").
    Q_INVOKABLE void cancel(const QString &key);
    bool isActive(const QString &key) const;
    // 0..1 from the converter's frame count.
    Q_INVOKABLE double progress(const QString &key) const;
    // Identifies the converter build in use (path, size, date), empty without one.
    QString signature() const;

    static MogrtRenderer *instance();

signals:
    void availabilityChanged();
    void jobsChanged();
    // `result` is the converter's own JSON answer (info: name, width, height, fps, duration,
    // fonts, controls; still/render: output, duration, warnings). `error` is set when !ok.
    void jobFinished(const QString &key, bool ok, const QJsonObject &result, const QString &error);

private:
    struct Job
    {
        Request request;
        QPointer<QProcess> process;
        QString partial;     // the file the converter writes, renamed onto request.output
        QString valuesFile;  // --values JSON
        QByteArray stdoutData;
        QByteArray stderrTail;
        double progress = 0;
        qint64 order = 0;
    };

    QString findExecutable() const;
    void pump();
    void start(const QString &key);
    void finish(const QString &key, int exitCode, bool crashed);
    void fail(const QString &key, const QString &message);
    void cleanup(Job &job);
    void bump();

    QString m_executable;
    QHash<QString, Job> m_queued;
    QHash<QString, Job> m_running;
    QHash<QString, double> m_progress;
    int m_revision = 0;
    qint64 m_counter = 0;
};

} // namespace dafin::assets
