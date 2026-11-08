#pragma once

#include <QAtomicInt>
#include <QFuture>
#include <QList>
#include <QObject>
#include <QPair>
#include <QString>

class AppController;

// Silence removal and audio-based clip syncing, reached from QML as EditorState.audioAlign.
// Both decode the clips' audio off the GUI thread, then land one undo step back on it.
class AudioAlignController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)

public:
    explicit AudioAlignController(AppController *app, QObject *parent = nullptr);
    ~AudioAlignController() override;

    bool running() const { return m_running; }
    double progress() const { return m_progress; }
    QString status() const { return m_status; }

    // True for a clip that has sound of its own, or a separated video whose audio partner does.
    Q_INVOKABLE bool canRemoveSilence(int trackIndex, int clipIndex) const;
    // Cuts the quiet stretches out of the clip (and its linked partners) and closes the gaps
    // within its span. RMS 0.02, at least 0.5 s quiet, 0.08 s of sound kept either side.
    Q_INVOKABLE void removeSilence(int trackIndex, int clipIndex);

    // True when the selection holds exactly two audio-bearing items (a linked video and its audio
    // count as one).
    Q_INVOKABLE bool canSyncByAudio() const;
    // The first selected item is the anchor and stays put; the other, with its linked partners,
    // moves so its sound lines up with the anchor's.
    Q_INVOKABLE void syncByAudio();

    // Dafin's Sync audio (the timeline toolbar's button): lines up any number of clips recorded
    // together — a camera and separate microphones — by their sound. The video's own audio is the
    // reference and stays put (else the longest recording); every other clip, with its linked
    // partners, moves to where its sound matches. Select the clips; with just one (or nothing:
    // the video under the playhead), every other clip with sound near it is tried, and only the
    // ones that really share its sound move. Offsets of any size are found. One undo step.
    Q_INVOKABLE bool canSyncSimilar() const;
    Q_INVOKABLE void syncSimilarAudio();

    Q_INVOKABLE void cancel();

signals:
    void runningChanged();
    void progressChanged();
    void statusChanged();

private:
    struct SyncUnit;
    struct SimilarUnit;
    struct SimilarMatch;
    bool begin(const QString &message);
    void end();
    void reportProgress(double fraction, const QString &status);
    void applySilence(const QString &clipId, qint64 startUs, qint64 durationUs,
                      const QList<QPair<double, double>> &silences);
    void applySync(const QString &anchorId, const QString &otherId, qint64 anchorStartUs,
                   qint64 otherStartUs, double lagSeconds);
    QList<SimilarUnit> selectedSimilarUnits() const;
    void applySimilar(const SimilarUnit &anchor, const QList<SimilarMatch> &matches, bool gathered);

    AppController *m_app = nullptr;
    bool m_running = false;
    double m_progress = 0.0;
    QString m_status;
    QAtomicInt m_cancel = 0;
    // The running worker, waited for on destruction.
    QFuture<void> m_future;
};
