#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariant>
#include <QVariantMap>

#include <cstdint>
#include <functional>

class AppController;
class QTimer;

namespace drift {
struct Clip;
}

// Dafin: titles made from Adobe Motion Graphics Templates (.mogrt), kept editable.
//
// A .mogrt title is a Video clip that carries a MogrtSource: the template (by content hash), its
// Essential Graphics controls and the values the user gave them. The picture is a render of that
// state by MOGRT Converter, a separate program (MogrtRenderer). Renders are immutable files named
// by a key of everything that shapes them (template, values, length, size), so any project state
// — after an undo, a duplicate, a reopen — finds its render again, or has one made.
//
// After every timeline change a short pass looks at each .mogrt clip: it keeps the clip's source
// range equal to the render (no speed, no slip), swaps in a cached render of the clip's current
// state when one exists (full quality before a preview), and otherwise asks for a half-size
// preview, leaving the last good render on screen until the new one is in. Export waits for full
// quality renders of every .mogrt clip (finalizeForExport).
//
// Reached from QML as EditorState.mogrt.
class MogrtTitleController : public QObject
{
    Q_OBJECT
    // Bumped whenever a template's check, a render or the converter's availability changes.
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(bool converterAvailable READ converterAvailable NOTIFY changed)
    // Renders are being made for the open project.
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    // Export is waiting for full-quality renders.
    Q_PROPERTY(bool finalizing READ finalizing NOTIFY changed)
    Q_PROPERTY(double finalizeProgress READ finalizeProgress NOTIFY changed)

public:
    explicit MogrtTitleController(AppController *app, QObject *parent = nullptr);
    ~MogrtTitleController() override;

    int revision() const { return m_revision; }
    bool converterAvailable() const;
    bool busy() const;
    bool finalizing() const { return m_finalizing; }
    double finalizeProgress() const;

    // ---- Titles grid -------------------------------------------------------------------------

    // How well a template will work, for the card's badge. Starts a check (one test frame drawn
    // by the converter) the first time it is asked about a template. `templateId` is the
    // template's content id when the caller knows it (the Titles grid does), else "".
    //   state: "checking" | "ok" | "warnings" | "unsupported" | "failed" | "needsConverter"
    //   warnings: what the converter reported it could not draw; message: the reason for the rest
    Q_INVOKABLE QVariantMap templateStatus(const QString &templateId, const QString &pathOrUrl);
    // Adds the template as an editable .mogrt title. At the playhead when atSeconds < 0, on
    // trackIndex when it takes video. Returns {ok, fallback, message}: when the template cannot
    // be animated here (made in Premiere Pro, no converter), it is added as an ordinary Dafin
    // title instead and `fallback` is true.
    Q_INVOKABLE QVariantMap addTemplate(const QString &pathOrUrl, double atSeconds = -1, int trackIndex = -1,
                                        const QString &label = QString());
    // The template's own length in seconds (5 when it does not say), cached; for drop previews.
    double templateDurationSeconds(const QString &pathOrUrl);

    // ---- Inspector (the Template tab) ---------------------------------------------------------

    // {name, status, message, warnings, controls:[{id,type,label,value,default,min,max,editable}],
    //  rendering, progress, quality ("preview"/"full"/""), templateMissing, converterAvailable,
    //  authorApp}
    Q_INVOKABLE QVariantMap clipState(int track, int clip) const;
    // One undo step. `value` as QML has it: text, number, bool, color, or a list of numbers.
    Q_INVOKABLE void setValue(int track, int clip, const QString &controlId, const QVariant &value);
    Q_INVOKABLE void resetValues(int track, int clip);
    Q_INVOKABLE void renderFullQuality(int track, int clip);
    // Throws the clip's renders away and makes them again (after installing a missing font, say).
    Q_INVOKABLE void renderAgain(int track, int clip);
    // Points every clip of a missing template at `url`, when that file is the same template.
    Q_INVOKABLE bool locateTemplate(int track, int clip, const QUrl &url);

    // ---- Export -------------------------------------------------------------------------------

    // Starts the full-quality renders the timeline still lacks and calls `ready` once every .mogrt
    // clip shows one, or at once when none is needed; `failed` with the reason otherwise.
    void finalizeForExport(std::function<void()> ready, std::function<void(const QString &)> failed);
    Q_INVOKABLE void cancelFinalize();

    // ---- Storage ------------------------------------------------------------------------------

    // <app data>/mogrt: templates/<id>.mogrt, cache/<key>-<quality>.mov
    static QString storeRoot();
    static QString contentId(const QString &path);
    Q_INVOKABLE double cacheMegabytes() const;
    // Deletes renders the open project does not use. Saved projects keep copies of their own.
    Q_INVOKABLE int clearUnusedRenders();

signals:
    void changed();

private:
    struct Template
    {
        QString path;          // in the store when it could be copied there
        bool parsed = false;
        bool readable = false;
        QString title;
        QString authorApp;
        QJsonArray controls;
        int width = 0;
        int height = 0;
        double fps = 0;
        double durationSeconds = 0;
        QString posterPath;
        QString readError;
        // the converter's check
        QString checkState;    // "", "checking", "ok", "warnings", "failed", "unsupported"
        QStringList warnings;
        QString checkMessage;
        QString checkJob;
    };
    struct Render
    {
        QString key;
        QString quality;
        QString templateId;
        QString output;
    };

    Template *templateFor(const QString &id, const QString &path);
    void parse(Template &t);
    QString importToStore(const QString &path, QString *error);
    void startCheck(const QString &id, Template &t);
    void loadCheck(const QString &id, Template &t) const;
    void saveCheck(const QString &id, const Template &t) const;

    drift::Clip *clipAt(int track, int clip) const;
    // The render key of a clip's current state, and the files for it.
    static QString renderKey(const drift::Clip &clip);
    static QString renderPath(const QString &key, const QString &quality);
    static int64_t frameRoundedDurationUs(const drift::Clip &clip);
    QString resolveTemplatePath(const drift::Clip &clip) const;
    QString wantKey(const QString &key, const QString &quality) const { return key + QLatin1Char('|') + quality; }
    void request(const drift::Clip &clip, const QString &key, const QString &quality, int priority);

    void scheduleReconcile();
    // Brings every .mogrt clip's render in line with its state. True when it changed the project.
    bool reconcile();
    void onJobFinished(const QString &job, bool ok, const QJsonObject &result, const QString &error);
    void checkFinalize();
    void bump();

    AppController *m_app = nullptr;
    QTimer *m_reconcileTimer = nullptr;
    QHash<QString, Template> m_templates;        // by content id
    QHash<QString, QString> m_idForFile;         // "path|size|mtime" → content id
    QHash<QString, Render> m_jobs;               // by renderer job key
    QHash<QString, QString> m_jobForWant;        // wantKey → renderer job key
    QHash<QString, QString> m_failed;            // wantKey → error
    QHash<QString, QStringList> m_renderWarnings;  // render key → converter warnings
    bool m_finalizing = false;
    int m_finalizeTotal = 0;
    QSet<QString> m_finalizeWants;
    std::function<void()> m_finalizeReady;
    std::function<void(const QString &)> m_finalizeFailed;
    int m_revision = 0;
};
