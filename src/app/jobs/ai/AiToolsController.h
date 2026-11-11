#pragma once

#include "core/base/Time.h"
#include "core/project/Transcript.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class AppController;
class QTimer;

// Dafin: the back end of the assets panel's AI category, reached from QML as EditorState.ai.
// Every tool there runs on this computer; its models are add-ons downloaded on first use.
//
// Auto captions: the media behind each selected clip is transcribed once, word by word (Whisper,
// the CTC aligner when one is installed for the language, Silero VAD when installed). The
// transcript is kept with the project and the caption clip is built from it, so the cues land on
// the words and captioning the same media again is instant. The transcript of the clip in focus
// is listed for reading and correcting; a correction is saved with the project and rewrites the
// captions made from those words in one undo step. (Transcripts sit outside undo, as everywhere in
// Drift, so a line entered again brings undone captions back in line with it.)
class AiToolsController : public QObject
{
    Q_OBJECT

    // A selected video or audio clip with sound, so Create captions has something to work on.
    Q_PROPERTY(bool canCaption READ canCaption NOTIFY canCaptionChanged)
    Q_PROPERTY(bool captioning READ captioning NOTIFY captioningChanged)
    Q_PROPERTY(double captionProgress READ captionProgress NOTIFY captionProgressChanged)
    Q_PROPERTY(QString captionStatus READ captionStatus NOTIFY captionStatusChanged)
    // The media clip whose transcript is listed: the selected video/audio clip, else the one the
    // last captions were made from. transcriptInfo: clipId, clipName, hasTranscript, language,
    // aligned (measured word times), words. transcriptLines: text, start, end (timeline seconds),
    // time (a label), speaker.
    Q_PROPERTY(QString transcriptClipId READ transcriptClipId NOTIFY transcriptChanged)
    Q_PROPERTY(QVariantMap transcriptInfo READ transcriptInfo NOTIFY transcriptChanged)
    Q_PROPERTY(QVariantList transcriptLines READ transcriptLines NOTIFY transcriptChanged)
    // Set by the panel while it shows the transcript; the lines are only worked out then.
    Q_PROPERTY(bool transcriptVisible READ transcriptVisible WRITE setTranscriptVisible
                   NOTIFY transcriptVisibleChanged)

public:
    explicit AiToolsController(AppController *app, QObject *parent = nullptr);

    bool transcriptVisible() const { return m_transcriptVisible; }
    void setTranscriptVisible(bool visible);
    bool canCaption() const { return m_canCaption; }
    bool captioning() const { return m_captioning; }
    double captionProgress() const { return m_captionProgress; }
    QString captionStatus() const { return m_captionStatus; }
    QString transcriptClipId() const { return m_targetClipId; }
    QVariantMap transcriptInfo() const { return m_info; }
    QVariantList transcriptLines() const { return m_lineRows; }

    // Transcribes whatever the selected video/audio clips still need, then adds one caption clip
    // for them. language: a Whisper code, empty to detect it; a code other than the one a stored
    // transcript was made in transcribes again. maxWordsPerCue: 0 packs by line width. False, with
    // a message, when the selection cannot be captioned.
    Q_INVOKABLE bool createCaptions(const QString &language, int maxWordsPerCue);
    Q_INVOKABLE void cancelCaptions();

    // Replaces the words of transcript line `line` with `text`; an empty text removes them. The
    // captions made from those words are rewritten to match. False when nothing changed.
    Q_INVOKABLE bool setLineText(int line, const QString &text);
    // Puts the playhead where the line starts in the clip.
    Q_INVOKABLE void seekToLine(int line);

    // Frees the speech models a caption run loaded (about 1 GB of memory), so Settings can delete
    // their files: Windows will not remove a file a loaded model still holds. False, and nothing
    // freed, while anything is still listening.
    Q_INVOKABLE bool releaseSpeechModels();

signals:
    void canCaptionChanged();
    void captioningChanged();
    void captionProgressChanged();
    void captionStatusChanged();
    void transcriptChanged();
    void transcriptVisibleChanged();
    // A caption run ended: ok when the caption clip was added.
    void captionsFinished(bool ok, const QString &message);

private:
    struct Line
    {
        int firstWord = -1;
        int lastWord = -1;
        drift::TimeUs sourceStartUs = 0;
        drift::TimeUs sourceEndUs = 0;
        drift::TimeUs timelineStartUs = 0;
        // As listed, to notice the transcript changed under the line before an edit lands.
        QString text;
    };

    void scheduleRefresh();
    void refresh();
    void setCaptioning(bool captioning);
    void setCaptionProgress(double progress);
    void setCaptionStatus(const QString &status);
    void reportJobProgress(int index, double fraction, const QString &status);
    void jobDone(const QString &jobId, bool ok, const QString &code, const QString &message);
    void finishCaptions();
    void endCaptionRun(bool ok, const QString &message);
    void storeTranscript(const QString &assetId, std::shared_ptr<drift::Transcript> transcript);
    // Rewrites, from the transcript, the text of every caption tied to `sourceKey` whose words lie
    // over the source span [startUs, endUs). True when a caption changed (one undo step).
    bool syncCaptions(const QString &sourceKey, const drift::Transcript &transcript,
                      drift::TimeUs startUs, drift::TimeUs endUs);

    AppController *m_app = nullptr;
    QTimer *m_refreshTimer = nullptr;
    bool m_transcriptVisible = false;

    bool m_canCaption = false;
    bool m_captioning = false;
    double m_captionProgress = 0.0;
    QString m_captionStatus;

    // The run in progress: its jobs, the clips to caption when they are done, and the settings.
    QStringList m_jobIds;
    int m_jobCount = 0;
    int m_jobsDone = 0;
    QString m_runError;
    bool m_runCancelled = false;
    QStringList m_runClipIds;
    QString m_runLanguage;
    int m_runWordsPerCue = 0;

    QString m_lastCaptionedClipId;
    QString m_targetClipId;
    QString m_targetAssetId;
    QString m_targetSourceKey;
    QList<Line> m_lines;
    QVariantMap m_info;
    QVariantList m_lineRows;
};
