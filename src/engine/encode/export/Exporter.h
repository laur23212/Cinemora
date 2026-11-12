#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

#include "core/base/Time.h"

namespace drift {
class Project;
}

// Named scale targets used by the simple downscale chips. Resolution is derived
// from the project's aspect ratio so export never distorts.
struct ExportScalePreset
{
    QString id;
    QString label;
    int targetHeight = 0; // 0 = keep project height
    int videoBitrateKbps = 12000;
};

// Upper bound for a hand-typed export frame rate; anything above is clamped.
inline constexpr int kMaxExportFps = 480;

// Full encode settings passed from the export dialog.
struct ExportSettings
{
    int targetHeight = 0; // 0 = keep project height
    // Output frame rate. 0 = follow the project fps. Rational so NTSC rates
    // (24000/1001, 30000/1001, 60000/1001) round-trip exactly.
    int fpsNum = 0;
    int fpsDen = 1;
    QString videoCodecId = QStringLiteral("h264");
    QString rateControl = QStringLiteral("crf"); // "crf" | "bitrate"
    int crf = 18;
    int videoBitrateKbps = 12000;
    QString videoPreset = QStringLiteral("medium");
    QString audioCodecId = QStringLiteral("aac");
    int audioBitrateKbps = 192;
    bool audioOnly = false;
    bool gifExport = false;
    QString metadataTitle;
    QString metadataArtist;
    QString metadataAlbum;
    QString metadataComment;
    // Optional export slice on the timeline. Both zero = encode the full project.
    drift::TimeUs startUs = 0;
    drift::TimeUs endUs = 0;
    // Dafin: one segment of a resumable export (SegmentedExport). Frames count from the start of
    // the resolved range above, and each frame is rendered at the same timeline time a single-pass
    // export gives it, so a transition across a segment boundary renders identically. The file's
    // own timestamps start at zero. 0 frames = the whole range (a normal export).
    int64_t segmentFirstFrame = 0;
    int64_t segmentFrameCount = 0;
};

// WYSIWYG exporter: encodes frames straight from FrameCompositor and audio from
// AudioMixer, so the exported file matches the preview exactly (single compositor).
class Exporter
{
public:
    // Called with progress in [0,1]; return false to cancel the export.
    using ProgressFn = std::function<bool(double)>;

    static const QList<ExportScalePreset> &scalePresets();
    static const ExportScalePreset *scalePresetById(const QString &id);

    // HandBrake-like catalogs; `available` is libav encoder presence, and for hardware
    // entries also a successful device probe. Hardware backends that cannot exist on
    // this OS are omitted from the list; videoCodecById still returns them.
    static QVariantList videoCodecs();
    static QVariantList audioCodecs();
    static QVariantMap videoCodecById(const QString &id);
    static QVariantMap audioCodecById(const QString &id);

    // Preferred container extension for a video+audio pair (mp4 / webm / mkv).
    static QString preferredContainer(const QString &videoCodecId, const QString &audioCodecId);
    // Standalone audio muxer (m4a / mp3 / opus / ac3 / flac).
    static QString preferredAudioOnlyContainer(const QString &audioCodecId);
    static QStringList saveFilters(const QString &container, bool audioOnly = false);
    static QString defaultSuffix(const QString &container, bool audioOnly = false);

    // Downscale chip options for the current project size (no upscale).
    static QVariantList scaleOptions(int projectWidth, int projectHeight);

    // Frame rate choices for the export dialog; the first entry follows `projectFps`.
    static QVariantList frameRateOptions(int projectFps);

    static bool gifAvailable();

    // Dafin: opens `videoCodecId` the way an export would (same device, pixel format and frames
    // context) at a tiny size, encodes a few grey frames and checks a packet comes out. A device
    // that opens is not an encoder that works — old drivers, a laptop on its integrated GPU, all
    // NVENC sessions in use — so EncoderProbe runs this once per GPU/driver and caches it.
    static bool testEncode(const QString &videoCodecId, QString *reasonOut = nullptr);

    // Dafin: how many video frames a single-pass export of `settings` encodes, and at what rate.
    // 0 with *errorOut set when the range is empty or the project has no size.
    static int64_t frameCount(const drift::Project &project, const ExportSettings &settings,
                              int *fpsNum, int *fpsDen, QString *errorOut = nullptr);

    static ExportSettings defaultSettings();
    static ExportSettings settingsFromMap(const QVariantMap &map);

    // Holds the process in the foreground for as long as it exists: an Android foreground service
    // with a progress notification, plus FLAG_KEEP_SCREEN_ON. Without one, a backgrounded process
    // is frozen and then killed, and a multi-minute render or ML pass is gone with nothing to
    // resume from — so any job of that length wants one, not just the encode. Refcounted, so an
    // outer hold spanning the copy into the user's document is not dropped when Exporter::run's own
    // inner hold ends; the outermost hold's `title` is the one the notification shows. Constructing
    // it on desktop does nothing at all.
    class BackgroundHold
    {
    public:
        // `cancellable` puts a Cancel action on the notification. Only the outermost hold's value
        // counts, and only a job that actually polls cancelRequested() should pass true — the
        // action stops nothing on its own.
        explicit BackgroundHold(const QString &title, bool cancellable = false);
        ~BackgroundHold();
        BackgroundHold(const BackgroundHold &) = delete;
        BackgroundHold &operator=(const BackgroundHold &) = delete;

        // Progress in whole percent, clamped to 0..100. Static because the code that knows the
        // percentage is rarely the code holding the outermost hold; repeats are dropped rather
        // than re-posting the same notification once per frame.
        static void setPercent(int percent);

        // True once the notification's Cancel action has been tapped. Cleared when the outermost
        // hold is taken, so it never carries into the next job. Always false off Android.
        static bool cancelRequested();
    };

    // `outputPath` is a filesystem path, or — on Android — the fully encoded content:// URI of a
    // document the save picker created (AndroidUri::filePath of what FileDialogs returned).
    //
    // Dafin: on desktop the file is rendered to a hidden sibling (stagingPathFor) and moved onto
    // `outputPath` only when it is complete, so a failed, cancelled or crashed export never leaves
    // a broken file under the final name, and an existing file there survives until the new one is
    // ready. Where no sibling can be created (a document-portal file) it renders in place as before.
    // `warningsOut` lists what was patched over to keep going — a clip that would not decode at a
    // given time (black or the previous frame instead) — one line each, also logged.
    static bool run(const drift::Project &project, const ExportSettings &settings, const QString &outputPath,
                    QString *errorOut, const ProgressFn &onProgress = {}, QStringList *warningsOut = nullptr);

    // ".<name>.dafin-part.<ext>" next to `outputPath` (the extension stays last so the muxer is
    // still guessed from it). Empty for an empty path.
    static QString stagingPathFor(const QString &outputPath);
    // Moves a finished staging file onto `finalPath`, replacing what is there in one step where the
    // filesystem allows it, else copying the bytes over. The staging file is gone afterwards.
    static bool commitStaged(const QString &stagingPath, const QString &finalPath, QString *errorOut = nullptr);

    // Rough size of the file this export will write: bitrate (or a per-codec bits-per-pixel guess
    // for CRF/lossless) × duration, plus audio, plus 10 %. -1 when it can't be told (GIF).
    static qint64 estimateOutputBytes(const drift::Project &project, const ExportSettings &settings);

    // Copies a finished export into the shared Movies (or Music) collection and returns the
    // MediaStore URI it now lives at, so the gallery and the share sheet can see it: a file left
    // in app storage is reachable from neither. Empty with *errorOut set on failure, and always
    // empty on desktop, where an export already lands wherever the user pointed it.
    static QUrl publishToGallery(const QUrl &source, const QString &displayName,
                                 QString *errorOut = nullptr);

private:
    static bool runDirect(const drift::Project &project, const ExportSettings &settings,
                          const QString &outputPath, QString *errorOut, const ProgressFn &onProgress,
                          QStringList *warningsOut);
};
