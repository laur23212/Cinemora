#include "app/editor/ProjectSafety.h"
#include "app/platform/DiagnosticsLog.h"
#include "app/platform/FileDialogs.h"
#include "engine/platform/AndroidUri.h"
#include "app/editor/appcontroller/AppControllerInternal.h"
#include "app/editor/sessions/MogrtTitleController.h"
#include "app/editor/sessions/CurveEditorController.h"
#include "app/jobs/vision/FaceTrackingController.h"
#include "app/jobs/vision/SegmentationController.h"
#include "app/bin/MediaBinController.h"
#include "app/jobs/vision/StabilizeController.h"
#include "app/editor/sessions/SpeedCurveController.h"
#include "app/editor/ProjectFileController.h"
#include "ui/preview/PreviewController.h"

#include "engine/decode/readers/ClipReaderPool.h"
#include "engine/diagnostics/DebugReport.h"
#include "engine/project/ProjectDependencies.h"
#include "engine/EngineTuning.h"
#include "engine/encode/export/Exporter.h"
#include "engine/encode/export/EncoderProbe.h"
#include "engine/encode/export/SegmentedExport.h"
#include "engine/encode/export/HwEncodeFallback.h"
#include "engine/packages/assets/EmojiCatalog.h"
// Engine-internal by its own header comment, and included here only for releaseCaches(): the GPU
// caches have no other owner outside src/engine to ask. A one-line forwarder on GpuCompositor
// would restore the boundary.
#include "engine/gpu/GlRuntime.h"
#include "playback/stats/PlaybackDiagnostics.h"
#include "engine/packages/assets/StickerCatalog.h"
#ifdef DRIFT_WITH_SKIA
#include "engine/render2d/text/SkiaTextPainter.h"
#endif

#include <QClipboard>
#include <QDesktopServices>
#include <QGuiApplication>

#include <QFileInfo>
#include <QLocale>
#include <QStorageInfo>
#include <QMimeDatabase>
#include <QPointer>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>

#include <QCryptographicHash>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QDateTime>
#include <QRegularExpression>

#if !defined(Q_OS_ANDROID) && !defined(Q_OS_WASM)
#include "app/workers/WorkerSupervisor.h"

#include <QCoreApplication>
#include <QFile>
#include <QSaveFile>
#include <QUuid>
#endif
using namespace drift::appdetail;
using drift::mcpdetail::findClipById;
using drift::mcpdetail::runBeatAnalysis;
using namespace drift::appcontroller_detail;


QByteArray AppController::serializeProjectJson() const
{
    QJsonObject root = m_project.toJson();
    const QJsonObject session = sessionJson();
    for (auto it = session.constBegin(); it != session.constEnd(); ++it)
        root.insert(it.key(), it.value());
    if (const QJsonObject history = historyJson(drift::captureHistory(m_undoStack)); !history.isEmpty())
        root.insert(QStringLiteral("undoHistory"), history);
    dafin::project::stampVersion(root);
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

// What a saved project records beside the project itself: view and editing state, read on the GUI
// thread so the project half can be serialized anywhere.
QJsonObject AppController::sessionJson() const
{
    QJsonObject root;
    root.insert(QStringLiteral("playheadUs"), static_cast<double>(m_playheadUs));
    root.insert(QStringLiteral("snapEnabled"), m_snapEnabled);
    root.insert(QStringLiteral("rippleEnabled"), m_rippleEnabled);
    root.insert(QStringLiteral("allowClipOverlap"), m_allowClipOverlap);
    root.insert(QStringLiteral("mediaViewMode"), m_mediaViewMode);
    // Still written so a project saved here opens with the right view in builds that
    // predate the tree mode.
    root.insert(QStringLiteral("mediaGridMode"), mediaGridMode());
    root.insert(QStringLiteral("loopWorkArea"), m_loopWorkAreaEnabled);
    QJsonArray guideSets;
    for (const QString &id : m_activeGuideSets) {
        const drift::GuideSet *set = findGuideSet(id);
        if (set && !set->builtIn)
            guideSets.append(drift::guideSetToJson(*set));
    }
    root.insert(QStringLiteral("guides"), QJsonObject{
        {QStringLiteral("enabled"), m_preview->guidesEnabled()},
        {QStringLiteral("active"), QJsonArray::fromStringList(m_activeGuideSets)},
        {QStringLiteral("sets"), guideSets},
    });
    return root;
}

void AppController::resetSessionState()
{
    // These end up editing the project, so they have to run while it is still the one they were
    // opened against.
    m_speedCurve->end();
    m_curves->endFade();
    m_segmentation->endSession();
    endMulticamSession();

    m_clipboard.clear();
    // Keyed by timeline position rather than by source, so entries from the old project would be
    // served for the same span in the new one.
    m_subtitleWaveformCache.clear();
    m_subtitleWaveformPending.clear();
    // Beats are timeline-absolute for the same reason.
    clearBeatAnalysis();
    // Also drops the failed-source blacklist, so media that was missing gets another chance.
    m_filmstripTiles.clear();
    // Blocks are keyed by source path, so they would not go stale across documents — this is
    // about memory. Nothing else evicts them beyond the size budget, so a session that opens
    // several projects in turn would otherwise hold the peaks of every source in all of them.
    m_waveformBlocks.clear();

    m_bin->m_replacingAssetId.clear();
    m_sequencePlayheads.clear();
    m_pendingEffectTemplate.reset();
    m_previewDragActive = false;
    m_previewDragAuto = false;
    m_previewDragDirty = false;
    m_previewAutoCommit->stop();
    m_keyframeGraphHiddenProperties.clear();
    m_preview->setCanvasCropMode(false);
    m_preview->setGuideEditSetId(QString());
    setSubtitleEditing(false);
    setSelectedSubtitleCue(-1);

    emit projectReset();
}

bool AppController::applyProjectJson(const QByteArray &data, QString *error)
{
    std::optional<ParsedProject> parsed = parseProjectJson(data, error);
    if (!parsed)
        return false;
    applyParsedProject(std::move(*parsed));
    return true;
}

std::optional<AppController::ParsedProject> AppController::parseProjectJson(const QByteArray &data,
                                                                             QString *error)
{
    const QJsonDocument document = QJsonDocument::fromJson(data);
    if (!document.isObject()) {
        if (error)
            *error = QStringLiteral("Invalid project file");
        return std::nullopt;
    }
    return parseProjectJson(document.object(), error);
}

std::optional<AppController::ParsedProject> AppController::parseProjectJson(const QJsonObject &fileRoot,
                                                                             QString *error)
{
    // Dafin: older files are brought up to this build's project version first (ProjectSafety.h). A
    // file from a newer build is still tried — a newer build mostly only adds fields.
    QJsonObject root = fileRoot;
    bool fromNewer = false;
    dafin::project::migrate(root, &fromNewer);
    if (fromNewer) {
        qWarning("Project file is from a newer Dafin (projectVersion %d, this build writes %d)",
                 dafin::project::versionOf(root), dafin::project::kProjectVersion);
    }
    // Parse into a local and check before touching anything: a project we end up rejecting must
    // leave the open one exactly as it was, session state included.
    QString parseError;
    drift::Project project = drift::Project::fromJson(root, &parseError);
    if (!parseError.isEmpty()) {
        if (error)
            *error = parseError;
        return std::nullopt;
    }
    ParsedProject parsed{std::move(project), root, std::nullopt};
    // A history that fails to decode is dropped; the project itself still opens.
    if (root.contains(QStringLiteral("undoHistory")))
        parsed.history = drift::historyFromJson(root.value(QStringLiteral("undoHistory")).toObject());
    return parsed;
}

void AppController::applyParsedProject(ParsedProject parsed)
{
    const QJsonObject &root = parsed.root;

    resetSessionState();
    m_project = std::move(parsed.project);

    // Stickers moved out of the QRC and into an addon, so projects saved before that store paths
    // like ":/qt/qml/Drift/resources/stickers/grinning.png" that no longer resolve. Repoint them
    // at the installed pack; a sticker with no installed pack keeps its old path and simply fails
    // to load, which is the same outcome as a missing media file.
    //
    // Media that travelled inside the bundle now lives in this machine's extraction directory, so
    // the saved absolute paths have to be repointed before anything probes them. Same class of
    // fix-up as the two migrations around it.
    //
    // Emoji clips point at a raster in this machine's app data cache, which a project opened
    // elsewhere will not have. The glyph sequence is what was saved, so re-derive the path — the
    // render is cached, and without the font addon it comes back empty and the clip fails to load
    // like any other missing file.
    //
    // Undo history states are full projects and get the same treatment, once per distinct state.
    const QHash<QString, QString> pathRemap = m_projectFile->pendingPathRemap();
    m_projectFile->pendingPathRemap().clear();
    const auto migrate = [&pathRemap](drift::Project &project) {
        for (drift::MediaAsset &asset : project.assets()) {
            const QString migrated = resolveLegacyStickerPath(asset.path);
            if (!migrated.isEmpty())
                asset.path = migrated;
        }
        ProjectFileController::remapPaths(project, pathRemap);
        project.forEachTrackList([&](QList<drift::Track> &tracks) {
            for (drift::Track &track : tracks) {
                for (drift::Clip &clip : track.clips) {
                    if (clip.emoji.isEmpty())
                        continue;
                    const QString path = emojiImagePath(clip.emoji);
                    if (path.isEmpty())
                        continue;
                    clip.path = path;
                    clip.thumbnailPath = path;
                    clip.filmstripPath = path;
                }
            }
        });
    };
    if (!pathRemap.isEmpty())
        m_filmstripTiles.clear();
    migrate(m_project);
    if (parsed.history) {
        for (drift::Project &state : parsed.history->states)
            migrate(state);
    }

    m_stabilize->migrateLegacyClips();

    if (m_assetLibrary)
        m_assetLibrary->setProject(&m_project, true);
    m_bin->folderModel()->setProject(&m_project);
    m_bin->setCurrentFolderId(QString());

    m_projectFile->rehydrateMissingSources();

    m_projectFile->reportMissingCatalogEntries();

    setPlaying(false);
    m_snapEnabled = root.value(QStringLiteral("snapEnabled")).toBool(true);
    m_rippleEnabled = root.value(QStringLiteral("rippleEnabled")).toBool(false);
    m_allowClipOverlap = root.value(QStringLiteral("allowClipOverlap")).toBool(false);

    if (root.contains(QStringLiteral("mediaViewMode"))) {
        setMediaViewMode(root.value(QStringLiteral("mediaViewMode")).toString());
    } else if (root.contains(QStringLiteral("mediaGridMode"))) {
        setMediaGridMode(root.value(QStringLiteral("mediaGridMode")).toBool(true));
    }

    if (root.contains(QStringLiteral("loopWorkArea"))) {
        setLoopWorkAreaEnabled(root.value(QStringLiteral("loopWorkArea")).toBool(false));
    } else {
        m_playback.setLoopWorkArea(m_loopWorkAreaEnabled);
    }

    // Projects from before guides were saved keep whatever the session had.
    if (root.contains(QStringLiteral("guides"))) {
        const QJsonObject guides = root.value(QStringLiteral("guides")).toObject();
        m_preview->restoreGuidesEnabled(guides.value(QStringLiteral("enabled")).toBool(false));
        m_projectGuideSets.clear();
        for (const QJsonValue &value : guides.value(QStringLiteral("sets")).toArray()) {
            drift::GuideSet set = drift::guideSetFromJson(value.toObject());
            if (!set.id.isEmpty())
                m_projectGuideSets.append(set);
        }
        m_activeGuideSets.clear();
        for (const QJsonValue &value : guides.value(QStringLiteral("active")).toArray()) {
            const QString id = value.toString();
            if (findGuideSet(id) && !m_activeGuideSets.contains(id))
                m_activeGuideSets.append(id);
        }
    }

    if (root.contains(QStringLiteral("playheadUs"))) {
        setPlayheadUs(static_cast<drift::TimeUs>(root.value(QStringLiteral("playheadUs")).toDouble()));
    } else {
        setPlayheadSeconds(root.value(QStringLiteral("playheadSeconds")).toDouble());
    }

    restoreFilmstripsAfterLoad();
    // Face Swap landmarks are derived from the photo and deliberately not bundled, so a project
    // opened anywhere but where it was made has none. Re-derive them in the background; the
    // effect renders pass-through until each lands.
    m_faces->ingestSwapSourcesInProject();
    m_playback.setProject(&m_project);
    m_undoStack.clear();
    if (parsed.history) {
        const drift::Project loaded = m_project;
        QList<drift::HistoryStatePtr> states;
        states.reserve(parsed.history->states.size());
        for (drift::Project &state : parsed.history->states)
            states.append(std::make_shared<const drift::HistoryState>(std::move(state)));
        {
            const QSignalBlocker blocker(&m_undoStack);
            for (const drift::HistoryStep &step : std::as_const(parsed.history->steps)) {
                auto *command = new drift::ProjectSnapshotCommand(
                    &m_project, states.at(step.before), states.at(step.after), step.text);
                command->adoptApplied();
                m_undoStack.push(command);
            }
            m_undoStack.setIndex(parsed.history->index);
        }
        m_project = loaded;
    }
    emit undoHistoryChanged();
    emit undoStackChanged();
    clearSelection();
    m_projectFile->setDirty(false);
    m_projectFile->sweepBrushMattes();
    emit snapEnabledChanged();
    emit rippleEnabledChanged();
    emit allowClipOverlapChanged();
    notifyTracksChanged();
    emit sequenceTabsChanged();
    emit bookmarksChanged();
    emit workAreaChanged();
    emit m_projectFile->projectNameChanged();
    emit m_projectFile->projectMetadataChanged();
    emit m_projectFile->backgroundChanged();
    emit guidesChanged();
}


void AppController::releaseTransientCaches()
{
    ClipReaderPool::instance().releaseAll();
    FrameCompositor::clearStillImageCache();
    drift::text::clearLayoutCache();
#ifdef DRIFT_WITH_SKIA
    drift::skia::clearTextGeometryCache();
#endif
    // Uploaded textures and the FBO pool, without tearing the context down. Runs on the GL thread
    // and blocks, which is what makes it safe from here; it returns without creating a context if
    // GL was never brought up at all.
    drift::gl::runtime().releaseCaches();
}


QVariantList AppController::exportPresets() const
{
    QVariantList out;
    for (const ExportScalePreset &preset : Exporter::scalePresets()) {
        out.append(QVariantMap{
            {QStringLiteral("id"), preset.id},
            {QStringLiteral("label"), preset.label},
        });
    }
    return out;
}

QVariantList AppController::exportScaleOptions() const
{
    return Exporter::scaleOptions(m_project.width(), m_project.height());
}

QVariantList AppController::exportFrameRateOptions() const
{
    return Exporter::frameRateOptions(m_project.fps());
}

QVariantList AppController::exportVideoCodecs() const
{
    return Exporter::videoCodecs();
}

QVariantList AppController::exportAudioCodecs() const
{
    return Exporter::audioCodecs();
}

bool AppController::exportGifAvailable() const
{
    return Exporter::gifAvailable();
}

QVariantMap AppController::exportDefaultSettings() const
{
    const ExportSettings s = Exporter::defaultSettings();
    return QVariantMap{
        {QStringLiteral("targetHeight"), s.targetHeight},
        {QStringLiteral("fpsNum"), s.fpsNum},
        {QStringLiteral("fpsDen"), s.fpsDen},
        {QStringLiteral("videoCodecId"), s.videoCodecId},
        {QStringLiteral("rateControl"), s.rateControl},
        {QStringLiteral("crf"), s.crf},
        {QStringLiteral("videoBitrateKbps"), s.videoBitrateKbps},
        {QStringLiteral("videoPreset"), s.videoPreset},
        {QStringLiteral("audioCodecId"), s.audioCodecId},
        {QStringLiteral("audioBitrateKbps"), s.audioBitrateKbps},
        {QStringLiteral("audioOnly"), s.audioOnly},
        {QStringLiteral("gifExport"), s.gifExport},
    };
}

QVariantMap AppController::lastExportSettings() const
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("export"));
    if (!settings.contains(QStringLiteral("videoCodecId"))
        && !settings.contains(QStringLiteral("audioCodecId"))
        && !settings.contains(QStringLiteral("scaleId"))) {
        return {};
    }

    // A relaunch re-parses the INI and hands every value back as a QString, so the
    // types have to be restored here. QML sees these directly, and in JavaScript the
    // string "false" is truthy — an untyped audioOnly would put the export dialog in
    // audio mode on every launch after the first.
    QVariantMap out;
    const auto takeString = [&](const QString &key) {
        if (settings.contains(key))
            out.insert(key, settings.value(key).toString());
    };
    const auto takeInt = [&](const QString &key) {
        if (settings.contains(key))
            out.insert(key, settings.value(key).toInt());
    };
    const auto takeBool = [&](const QString &key) {
        if (settings.contains(key))
            out.insert(key, settings.value(key).toBool());
    };
    takeString(QStringLiteral("scaleId"));
    takeInt(QStringLiteral("targetHeight"));
    takeInt(QStringLiteral("fpsNum"));
    takeInt(QStringLiteral("fpsDen"));
    takeString(QStringLiteral("videoCodecId"));
    takeString(QStringLiteral("rateControl"));
    takeInt(QStringLiteral("crf"));
    takeInt(QStringLiteral("videoBitrateKbps"));
    takeString(QStringLiteral("videoPreset"));
    takeString(QStringLiteral("audioCodecId"));
    takeInt(QStringLiteral("audioBitrateKbps"));
    takeBool(QStringLiteral("audioOnly"));
    takeBool(QStringLiteral("gifExport"));
    takeBool(QStringLiteral("exportWorkAreaOnly"));
    return out;
}

QString AppController::lastExportFolder() const
{
    const QString folder = QSettings().value(QStringLiteral("export/lastFolder")).toString();
    if (folder.isEmpty() || !QDir(folder).exists())
        return {};
    return folder;
}

void AppController::rememberExportChoice(const QString &outputPath, const QVariantMap &settings)
{
    QSettings store;
    if (!outputPath.isEmpty()) {
        const QFileInfo info(outputPath);
        const QString folder = info.absolutePath();
        if (!folder.isEmpty() && QDir(folder).exists())
            store.setValue(QStringLiteral("export/lastFolder"), folder);
    }

    store.beginGroup(QStringLiteral("export"));
    const auto put = [&](const QString &key) {
        if (settings.contains(key))
            store.setValue(key, settings.value(key));
    };
    put(QStringLiteral("scaleId"));
    put(QStringLiteral("targetHeight"));
    put(QStringLiteral("fpsNum"));
    put(QStringLiteral("fpsDen"));
    put(QStringLiteral("videoCodecId"));
    put(QStringLiteral("rateControl"));
    put(QStringLiteral("crf"));
    put(QStringLiteral("videoBitrateKbps"));
    put(QStringLiteral("videoPreset"));
    put(QStringLiteral("audioCodecId"));
    put(QStringLiteral("audioBitrateKbps"));
    put(QStringLiteral("audioOnly"));
    put(QStringLiteral("gifExport"));
    put(QStringLiteral("exportWorkAreaOnly"));
    store.endGroup();
}

QString AppController::exportPreferredAudioOnlyContainer(const QString &audioCodecId) const
{
    return Exporter::preferredAudioOnlyContainer(audioCodecId);
}

QString AppController::exportPreferredContainer(const QString &videoCodecId,
                                                const QString &audioCodecId) const
{
    return Exporter::preferredContainer(videoCodecId, audioCodecId);
}

QStringList AppController::exportSaveFilters(const QString &container, bool audioOnly) const
{
    return Exporter::saveFilters(container, audioOnly);
}

QString AppController::exportDefaultSuffix(const QString &container, bool audioOnly) const
{
    return Exporter::defaultSuffix(container, audioOnly);
}

double AppController::exportProgress() const
{
    return m_exportProgress;
}

void AppController::cancelExport()
{
    if (m_exportInProgress)
        m_exportCancel.storeRelaxed(1);
}

QString AppController::defaultScreenshotFolder() const
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (dir.isEmpty())
        dir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    return QDir(dir).filePath(QStringLiteral("Dafin Screenshots"));
}

QString AppController::screenshotFolder() const
{
    const QString saved = QSettings().value(QStringLiteral("preview/screenshotFolder")).toString().trimmed();
    return saved.isEmpty() ? defaultScreenshotFolder() : QDir::fromNativeSeparators(saved);
}

void AppController::setScreenshotFolder(const QString &folder)
{
    QString clean = QDir::fromNativeSeparators(folder.trimmed());
    // A file:// URL from the folder picker, or the default again, both mean a plain path / reset.
    if (clean.startsWith(QStringLiteral("file:")))
        clean = QUrl(clean).toLocalFile();
    if (clean == defaultScreenshotFolder())
        clean.clear();
    QSettings settings;
    if (settings.value(QStringLiteral("preview/screenshotFolder")).toString() == clean)
        return;
    if (clean.isEmpty())
        settings.remove(QStringLiteral("preview/screenshotFolder"));
    else
        settings.setValue(QStringLiteral("preview/screenshotFolder"), clean);
    emit screenshotFolderChanged();
}

void AppController::savePreviewScreenshot()
{
    if (m_project.width() <= 0 || m_project.height() <= 0) {
        setLastMessage(tr("Nothing to capture yet."), QStringLiteral("warning"));
        return;
    }
    const drift::Project snapshot = m_project;
    const drift::TimeUs at = m_playheadUs;
    const QString dir = screenshotFolder();
    QString base = snapshot.name().trimmed();
    base.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
    if (base.isEmpty())
        base = QStringLiteral("Dafin");
    const QString file = QStringLiteral("%1 %2.png")
                             .arg(base, QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH.mm.ss.zzz")));
    const QString path = QDir(dir).filePath(file);
    QPointer<AppController> self(this);
    // Off the GUI thread: compositing a 4K frame takes a moment and must not stall playback.
    (void)QtConcurrent::run([self, snapshot, at, dir, path]() {
        FrameCompositor compositor;
        compositor.setProject(&snapshot);
        // Default options: scale 1 = the project's own pixels, and no proxies, so the picture is
        // what an export of this frame would contain.
        const QImage frame = compositor.compositeAt(at);
        QString message;
        QString severity = QStringLiteral("info");
        if (frame.isNull()) {
            message = QCoreApplication::translate("AppController", "Could not capture the preview.");
            severity = QStringLiteral("error");
        } else if (!QDir().mkpath(dir) || !frame.save(path, "PNG")) {
            message = QCoreApplication::translate("AppController", "Could not save the screenshot to %1.").arg(dir);
            severity = QStringLiteral("error");
        } else {
            message = QCoreApplication::translate("AppController", "Screenshot saved: %1 (%2×%3)")
                          .arg(QDir::toNativeSeparators(path)).arg(frame.width()).arg(frame.height());
        }
        if (self)
            QMetaObject::invokeMethod(self.data(), [self, message, severity]() {
                if (self)
                    self->setLastMessage(message, severity);
            }, Qt::QueuedConnection);
    });
}

#if !defined(Q_OS_ANDROID) && !defined(Q_OS_WASM)
namespace {

// Dafin: the job `dafin --export-worker` reads (ExportWorker.h).
QString writeExportJob(const drift::Project &project, const QVariantMap &settings,
                       const QString &outputPath, QString *errorOut)
{
    const QJsonObject job{
        {QStringLiteral("project"), project.toJson(/*includeTranscripts=*/false)},
        {QStringLiteral("settings"), QJsonObject::fromVariantMap(settings)},
        {QStringLiteral("output"), outputPath},
    };
    const QString path = QDir(QDir::tempPath())
                             .filePath(QStringLiteral("dafin-export-%1.json")
                                           .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)
        || file.write(QJsonDocument(job).toJson(QJsonDocument::Compact)) < 0 || !file.commit()) {
        if (errorOut)
            *errorOut = file.errorString();
        return {};
    }
    return path;
}

struct WorkerExport
{
    bool used = false;  // false: run the export in this process instead
    bool ok = false;
    bool broke = false; // the worker crashed or hung, also after the watchdog's restart
    QString error;
};

// Dafin: renders the export in a separate process (WorkerSupervisor.h), so a crash or a hang in
// a decoder, the GPU driver or an encoder ends that process and not the editor. A worker that
// crashes or sends no heartbeat for 15 s is killed and started once more; if that fails too the
// caller reports it with a Retry. When no worker can be started at all (or
// DAFIN_EXPORT_IN_PROCESS is set) the export runs here as before.
WorkerExport exportInWorkerProcess(const drift::Project &project, const QVariantMap &settings,
                                   const QString &outputPath, const std::function<void(double)> &progress,
                                   const std::function<bool()> &cancelled,
                                   const std::function<void()> &restarting)
{
    using namespace drift::worker;
    WorkerExport out;
    if (qEnvironmentVariableIsSet("DAFIN_EXPORT_IN_PROCESS"))
        return out;

    QString jobError;
    const QString jobPath = writeExportJob(project, settings, outputPath, &jobError);
    if (jobPath.isEmpty()) {
        qWarning("Export worker: could not write the job (%s); exporting in the editor",
                 qPrintable(jobError));
        return out;
    }

    WorkerSpec spec;
    spec.program = QCoreApplication::applicationFilePath();
    spec.arguments = {QStringLiteral("--export-worker"), jobPath};
    WorkerCallbacks callbacks;
    callbacks.progress = progress;
    callbacks.cancelled = cancelled;
    callbacks.restarting = [&](Outcome why, int attempt) {
        qWarning("Export worker %s; restarting it (attempt %d)",
                 why == Outcome::Hung ? "stopped responding" : "crashed", attempt);
        restarting();
    };
    const WorkerResult result = runWithWatchdog(spec, /*maxRestarts=*/1, callbacks);
    QFile::remove(jobPath);

    switch (result.outcome) {
    case Outcome::FailedToStart:
        qWarning("Export worker could not start (%s); exporting in the editor",
                 qPrintable(result.message));
        return out;
    case Outcome::Finished:
        out.used = true;
        out.ok = true;
        return out;
    case Outcome::Cancelled:
        out.used = true;
        out.error = QStringLiteral("Export cancelled");
        return out;
    case Outcome::Failed:
        out.used = true;
        out.error = result.message;
        return out;
    case Outcome::Crashed:
    case Outcome::Hung:
        qWarning("Export worker gave up: %s", qPrintable(result.message));
        out.used = true;
        out.broke = true;
        out.error = result.outcome == Outcome::Hung
                        ? QCoreApplication::translate(
                              "AppController",
                              "The export stopped responding, also after Dafin started it again. "
                              "The editor and your project are fine. Press Retry to try once more.")
                        : QCoreApplication::translate(
                              "AppController",
                              "The export crashed, also after Dafin started it again. The editor "
                              "and your project are fine. Press Retry to try once more.");
        return out;
    }
    return out;
}

} // namespace
#endif

void AppController::retryExport()
{
    if (m_exportInProgress || !m_retryExportUrl.isValid())
        return;
    // The dialog's choice was remembered the first time.
    exportWithSettings(m_retryExportUrl, m_retryExportSettings, /*rememberChoice=*/false);
}

void AppController::refreshUnfinishedExport()
{
    QVariantMap info;
#ifndef Q_OS_ANDROID
    const QList<SegmentedExport::PendingJob> jobs = SegmentedExport::pendingJobs();
    if (!jobs.isEmpty()) {
        const SegmentedExport::PendingJob &job = jobs.first(); // newest
        const QUrl url(job.outputUrl);
        info = QVariantMap{
            {QStringLiteral("dir"), job.dir},
            {QStringLiteral("name"), url.isValid() && !job.outputUrl.isEmpty()
                                         ? exportDisplayName(url)
                                         : QFileInfo(job.outputPath).fileName()},
            {QStringLiteral("path"), job.outputPath},
            {QStringLiteral("segmentsDone"), job.segmentsDone},
            {QStringLiteral("segmentsTotal"), job.segmentsTotal},
            {QStringLiteral("percent"),
             job.segmentsTotal > 0 ? (100 * job.segmentsDone) / job.segmentsTotal : 0},
            {QStringLiteral("updated"), QLocale().toString(job.updated, QLocale::ShortFormat)},
        };
    }
#endif
    if (info == m_unfinishedExport)
        return;
    m_unfinishedExport = info;
    emit unfinishedExportChanged();
}

void AppController::resumeUnfinishedExport()
{
    const QString dir = m_unfinishedExport.value(QStringLiteral("dir")).toString();
    if (dir.isEmpty())
        return;
    if (m_exportInProgress) {
        setLastMessage(tr("Export already in progress"), QStringLiteral("warning"));
        return;
    }
    drift::Project snapshot;
    QVariantMap settings;
    QString outputPath;
    QString outputUrlText;
    QString error;
    if (!SegmentedExport::loadJob(dir, &snapshot, &settings, &outputPath, &outputUrlText, &error)) {
        setLastMessage(error, QStringLiteral("error"));
        return;
    }
    QUrl outputUrl(outputUrlText);
    if (outputUrlText.isEmpty() || !outputUrl.isValid())
        outputUrl = QUrl::fromLocalFile(outputPath);
    startExportJob(snapshot, settings, outputPath, outputUrl, /*resume=*/true);
}

void AppController::discardUnfinishedExport()
{
    SegmentedExport::discardJob(m_unfinishedExport.value(QStringLiteral("dir")).toString());
    m_unfinishedExport.clear();
    emit unfinishedExportChanged();
    refreshUnfinishedExport(); // an older one may still be there
}

void AppController::exportProject(const QUrl &outputUrl)
{
    exportWithSettings(outputUrl, exportDefaultSettings());
}

void AppController::exportWithPreset(const QUrl &outputUrl, const QString &presetId)
{
    QVariantMap map = exportDefaultSettings();
    if (const ExportScalePreset *preset = Exporter::scalePresetById(presetId)) {
        map.insert(QStringLiteral("scaleId"), preset->id);
        map.insert(QStringLiteral("targetHeight"), preset->targetHeight);
        map.insert(QStringLiteral("videoBitrateKbps"), preset->videoBitrateKbps);
    }
    exportWithSettings(outputUrl, map);
}

void AppController::exportWithSettings(const QUrl &outputUrl, const QVariantMap &settings,
                                       bool rememberChoice)
{
    const ExportSettings exportSettings = Exporter::settingsFromMap(settings);

    // The muxer is chosen from the output file's extension, and a created document's URI has
    // none — so the staging file the encoder writes is given the one the settings imply.
    const QString container =
        exportSettings.gifExport
            ? QStringLiteral("gif")
            : exportSettings.audioOnly
                  ? Exporter::preferredAudioOnlyContainer(exportSettings.audioCodecId)
                  : Exporter::preferredContainer(exportSettings.videoCodecId, exportSettings.audioCodecId);
    const QString outputPath =
        writeTargetPath(outputUrl, Exporter::defaultSuffix(container, exportSettings.audioOnly));
    if (outputPath.isEmpty()) {
        const QString problem = exportTargetProblem(outputUrl, outputPath);
        setLastMessage(problem.isEmpty() ? tr("That save location isn’t valid") : problem,
                       QStringLiteral("error"));
        emit exportFinished(false);
        return;
    }

    if (m_exportInProgress) {
        setLastMessage(tr("Export already in progress"), QStringLiteral("warning"));
        return;
    }

    // Dafin: template titles play half-size previews while editing; the export waits for their
    // full-quality renders, then comes back here.
    if (!m_mogrtExportReady) {
        if (m_mogrt->finalizing()) {
            setLastMessage(tr("Template titles are still rendering for the export"), QStringLiteral("warning"));
            return;
        }
        QPointer<AppController> self(this);
        m_mogrt->finalizeForExport(
            [self, outputUrl, settings, rememberChoice] {
                if (!self)
                    return;
                self->m_mogrtExportReady = true;
                self->exportWithSettings(outputUrl, settings, rememberChoice);
                self->m_mogrtExportReady = false;
            },
            [self](const QString &message) {
                if (!self)
                    return;
                self->setLastMessage(message, QStringLiteral("error"));
                emit self->exportFinished(false);
            });
        return;
    }

    // The chosen location, not the staging file: remembering the latter would point the next
    // export dialog at this app's cache.
    if (rememberChoice) {
#ifdef Q_OS_ANDROID
        rememberExportChoice(AndroidUri::filePath(outputUrl), settings);
#else
        rememberExportChoice(outputPath, settings);
#endif
    }

    m_retryExportUrl = outputUrl;
    m_retryExportSettings = settings;

    // Snapshot the project so edits during export can't race the encoder. Always the main
    // timeline, even while a composite's tab is open.
    startExportJob(m_project.sequenceView({}), settings, outputPath, outputUrl, /*resume=*/false);
}

void AppController::startExportJob(const drift::Project &snapshot, const QVariantMap &settings,
                                   const QString &outputPath, const QUrl &outputUrl, bool resume)
{
    const ExportSettings exportSettings = Exporter::settingsFromMap(settings);

    // Stop playback so the decode pool isn't driven from two threads at once.
    setPlaying(false);

    m_exportCancel.storeRelaxed(0);
    m_exportProgress = 0.0;
    emit exportProgressChanged();
    m_exportInProgress = true;
    m_exportByAgent = m_exportRequestedByAgent;
    emit exportInProgressChanged();
    setLastMessage(tr("Exporting…"));

    const bool disposable = writeTargetIsDisposable(outputUrl);
    // Dafin: a long export renders in segments a crash cannot take away (SegmentedExport); the
    // job keeps the dialog's settings and the chosen target so the next launch can resume it.
    SegmentedExport::Options segmentOptions;
    segmentOptions.settingsMap = settings;
    segmentOptions.outputUrl = outputUrl.toString();
    segmentOptions.resume = resume;
    if (resume && !m_unfinishedExport.isEmpty()) {
        m_unfinishedExport.clear();
        emit unfinishedExportChanged();
    }

    m_exportFuture = QtConcurrent::run([this, snapshot, settings, exportSettings, outputPath, outputUrl, disposable,
                                        segmentOptions]() {
        // Spans the encode *and* the copy into the user's document. Exporter::run takes its own
        // hold, but that one ends when run() returns — and the commit below is the phase that
        // would otherwise lose a multi-minute render to a screen that idled out mid-copy. The
        // holds are refcounted, so the inner one nests without restarting the service.
        Exporter::BackgroundHold hold(QStringLiteral("Exporting"), /*cancellable=*/true);
        QString error;
        const auto report = [this](double fraction) {
            QMetaObject::invokeMethod(
                this,
                [this, fraction]() {
                    m_exportProgress = fraction;
                    emit exportProgressChanged();
                },
                Qt::QueuedConnection);
            // Either route into the same stop: the in-app Cancel button sets m_exportCancel, the
            // notification's action sets the service flag.
            return m_exportCancel.loadRelaxed() == 0 && !Exporter::BackgroundHold::cancelRequested();
        };
        // On the worker: probing a disconnected network share can block for many seconds.
        error = exportTargetProblem(outputUrl, outputPath);
        const bool targetOk = error.isEmpty();
        // Dafin: a rough size check before minutes of rendering. Only a warning: CRF sizes depend
        // on the footage, and the export still stops cleanly (no file left behind) if it runs out.
        if (targetOk) {
            const qint64 needed = Exporter::estimateOutputBytes(snapshot, exportSettings);
            const QStorageInfo storage(QFileInfo(outputPath).absolutePath());
            const qint64 available = storage.isValid() && storage.isReady() ? storage.bytesAvailable() : -1;
            if (needed > 0 && available >= 0 && needed > available) {
                const QLocale locale;
                const QString neededText = locale.formattedDataSize(needed);
                const QString availableText = locale.formattedDataSize(available);
                qWarning("Export: about %s needed, %s free at %s", qUtf8Printable(neededText),
                         qUtf8Printable(availableText), qUtf8Printable(outputPath));
                QMetaObject::invokeMethod(
                    this,
                    [this, neededText, availableText]() {
                        setLastMessage(tr("This export may need about %1, but only %2 is free on that drive. "
                                          "Exporting anyway.")
                                           .arg(neededText, availableText),
                                       QStringLiteral("warning"));
                    },
                    Qt::QueuedConnection);
            }
        }
        QStringList warnings;
        // Dafin: the encoder chain (HwEncodeFallback.h). A GPU pick runs on itself, then on the
        // GPU encoders after it (NVENC, AMF, QuickSync, VAAPI), then on the matching software
        // encoder — skipping any that failed the start-up test encode or an earlier export on
        // this GPU and driver (EncoderProbe.h). A failure part-way restarts the same snapshot on
        // the next one, so the picture is the one the preview shows; the app keeps running.
        // Settings > Performance › "If the GPU encoder fails…" off keeps the single encoder.
        const bool chainEnabled = drift::tuning::cpuFallbackOnGpuEncodeFailure.load(std::memory_order_relaxed);
        const QStringList chain = chainEnabled
                                      ? drift::hwencode::fallbackChain(exportSettings.videoCodecId,
                                                                       drift::encoderprobe::knownBroken())
                                      : QStringList{exportSettings.videoCodecId};
        bool ok = false;
        bool workerBroke = false; // the worker process crashed or hung, also after its restart
        bool finishedOnCpu = false;
        QStringList fallbacks; // "NVENC → AMF" steps, for the notice
        QString usedCodecId;
        QString previousId;
        QString firstError;
        for (const QString &codecId : chain) {
            if (!targetOk)
                break;
            ExportSettings attempt = exportSettings;
            if (codecId != exportSettings.videoCodecId) {
                const QVariantMap codec = Exporter::videoCodecById(codecId);
                if (!codec.value(QStringLiteral("available")).toBool())
                    continue;
                attempt.videoCodecId = codecId;
                // Presets don't carry between vendors (p4, balanced, medium…); CRF/bitrate do.
                const QString preset = codec.value(QStringLiteral("defaultPreset")).toString();
                attempt.videoPreset = preset.isEmpty() ? QStringLiteral("medium") : preset;
            }
            if (!previousId.isEmpty()) {
                qWarning("Export: encoder %s failed (%s); exporting again on %s", qPrintable(previousId),
                         qPrintable(error), qPrintable(codecId));
                fallbacks.append(QStringLiteral("%1 → %2").arg(previousId, codecId));
                const QString from = previousId;
                QMetaObject::invokeMethod(
                    this,
                    [this, from, codecId]() {
                        setLastMessage(tr("The %1 encoder failed; continuing the export on %2.")
                                           .arg(Exporter::videoCodecById(from).value(QStringLiteral("label")).toString(),
                                                Exporter::videoCodecById(codecId).value(QStringLiteral("label")).toString()),
                                       QStringLiteral("warning"));
                    },
                    Qt::QueuedConnection);
                error.clear();
            }
            // Dafin: each attempt renders in a separate process where one can be started (a crash or
            // a hang in a decoder, the GPU driver or an encoder then ends that process, not the
            // editor); otherwise, and on Android, it runs here as before. Frames patched over
            // during the export (warnings) are only collected for an in-editor run.
            bool handled = false;
            // Dafin: a long video export renders in resumable segments, in the editor, so its
            // finished parts survive a crash; shorter ones go to the worker process below.
            const bool segmented = SegmentedExport::supports(attempt, outputPath)
                                   && snapshot.durationUs() >= qint64(segmentOptions.minDurationSeconds * 1000000.0);
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_WASM)
            QVariantMap attemptSettings = settings;
            attemptSettings.insert(QStringLiteral("videoCodecId"), attempt.videoCodecId);
            attemptSettings.insert(QStringLiteral("videoPreset"), attempt.videoPreset);
            const WorkerExport worker = segmented ? WorkerExport{} : exportInWorkerProcess(
                snapshot, attemptSettings, outputPath, [&report](double fraction) { report(fraction); },
                [this] {
                    return m_exportCancel.loadRelaxed() != 0 || Exporter::BackgroundHold::cancelRequested();
                },
                [this] {
                    QMetaObject::invokeMethod(
                        this,
                        [this] {
                            m_exportProgress = 0.0;
                            emit exportProgressChanged();
                            setLastMessage(tr("The export stopped unexpectedly. Starting it again…"),
                                           QStringLiteral("warning"));
                        },
                        Qt::QueuedConnection);
                });
            if (worker.used) {
                handled = true;
                ok = worker.ok;
                workerBroke = worker.broke;
                error = worker.error;
            }
#endif
            if (!handled) {
                if (segmented) {
                    SegmentedExport::Options attemptOptions = segmentOptions;
                    // A second encoder cannot continue parts the first one made.
                    if (!previousId.isEmpty())
                        attemptOptions.resume = false;
                    ok = SegmentedExport::run(snapshot, attempt, outputPath, attemptOptions, &error, report);
                } else {
                    ok = Exporter::run(snapshot, attempt, outputPath, &error, report, &warnings);
                }
            }
            if (workerBroke)
                break; // reported with Retry; another encoder would not fix a crashing worker
            if (ok) {
                usedCodecId = codecId;
                finishedOnCpu = !fallbacks.isEmpty() && !drift::hwencode::isGpuEncoderId(codecId);
                break;
            }
            if (firstError.isEmpty())
                firstError = error;
            // Only an encoder failure moves down the chain: a cancel, a full disk or a refused
            // folder would fail the same way on every encoder.
            if (!drift::hwencode::shouldRetryOnCpu(chainEnabled, codecId, true, error))
                break;
            drift::encoderprobe::markBroken(codecId, error);
            previousId = codecId;
        }
        if (!ok && error.isEmpty())
            error = firstError;
        if (ok && !fallbacks.isEmpty())
            qWarning("Export: finished on %s after %s", qPrintable(usedCodecId),
                     qPrintable(fallbacks.join(QStringLiteral(", "))));
        // Before the commit: on Android the staged file is gone once it has been streamed out.
        const qint64 sizeBytes = ok ? QFileInfo(outputPath).size() : 0;
        // Android: the encoder needs a real file, and the document the user picked is only
        // reachable through the resolver, so the finished encode is streamed into it — the bar
        // runs a second time for that, and cancel still lands.
        if (ok) {
            ok = commitWriteTarget(
                outputPath, outputUrl,
                [&report](qint64 done, qint64 total) {
                    const double fraction = total > 0 ? double(done) / double(total) : 0.0;
                    Exporter::BackgroundHold::setPercent(static_cast<int>(fraction * 100.0));
                    return report(fraction);
                },
                &error);
        }
        // An empty document is one this export created through the save picker: without disposing
        // of it a cancel at 5% leaves a 0-byte file the user's gallery shows as a video. A document
        // that already had bytes is one the user chose to replace, and a cancel never reached it.
        if (!ok)
            discardWriteTarget(outputPath, outputUrl, disposable);

        QString thumbnail;
        if (ok) {
            const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
            const QString hash =
                QString::fromLatin1(QCryptographicHash::hash(outputUrl.toString().toUtf8(),
                                                             QCryptographicHash::Sha1).toHex().left(16));
            const QString thumbPath = QDir(base).filePath(QStringLiteral("thumbs/export-%1.jpg").arg(hash));
            FrameCompositor compositor;
            compositor.setProject(&snapshot);
            QImage frame = compositor.compositeAt(0);
            if (!frame.isNull()) {
                if (frame.width() > 320)
                    frame = frame.scaledToWidth(320, Qt::SmoothTransformation);
                QDir().mkpath(QFileInfo(thumbPath).absolutePath());
                if (frame.convertToFormat(QImage::Format_RGB32).save(thumbPath, "JPEG", 85))
                    thumbnail = thumbPath;
            }
        }
        const double durationSeconds = drift::usToSeconds(snapshot.durationUs());

        QMetaObject::invokeMethod(
            this,
            [this, ok, workerBroke, finishedOnCpu, usedFallback = !fallbacks.isEmpty(), warningCount = int(warnings.size()), error, outputUrl, sizeBytes, thumbnail, durationSeconds]() {
                m_exportInProgress = false;
                m_exportProgress = ok ? 1.0 : 0.0;
                if (ok)
                    recordExport(outputUrl, durationSeconds, sizeBytes, thumbnail);
#ifdef Q_OS_ANDROID
                m_lastExportUrl = ok ? outputUrl : QUrl();
                m_lastExportName = ok ? exportDisplayName(outputUrl) : QString();
                emit canShareExportChanged();
#else
                Q_UNUSED(outputUrl);
#endif
                emit exportProgressChanged();
                emit exportInProgressChanged();
                const bool cancelled = error == QLatin1String("Export cancelled")
                                       || error == QLatin1String("Cancelled");
                const QString severity = ok          ? QStringLiteral("success")
                                         : cancelled ? QStringLiteral("info")
                                                     : QStringLiteral("error");
                // Dafin: frames patched over during the export (Exporter::run), detailed in the log.
                if (ok && warningCount > 0) {
                    setLastMessage(tr("Export finished with %n warning(s). Details are in the log.", "", warningCount),
                                   QStringLiteral("warning"));
                    emit exportFinished(ok);
                    return;
                }
                // Dafin: a broken worker gets its own message with a Retry button (Main.qml),
                // not a second plain error toast. An agent's export reports as before.
                if (workerBroke && !m_exportByAgent) {
                    emit exportWorkerFailed(error);
                } else {
                    setLastMessage(ok ? (finishedOnCpu
                                             ? tr("Export complete. The GPU encoder failed, so it finished on the CPU.")
                                             : usedFallback ? tr("Export complete. The first GPU encoder failed, so another one finished it.")
                                                            : tr("Export complete"))
                                      : error.isEmpty() ? tr("Export failed") : error,
                                   severity);
                }
                // A failed long export keeps its finished parts; offer them again next launch.
                if (!ok && !cancelled)
                    refreshUnfinishedExport();
                emit exportFinished(ok);
            },
            Qt::QueuedConnection);
    });
}

void AppController::recordExport(const QUrl &url, double durationSeconds, qint64 sizeBytes,
                                 const QString &thumbnail)
{
    const QString uri = url.toString();
    const QString name = exportDisplayName(url);
    QSettings settings;
    QVariantList history = settings.value(QStringLiteral("exports/history")).toList();
    for (qsizetype i = history.size() - 1; i >= 0; --i) {
        if (history.at(i).toMap().value(QStringLiteral("uri")).toString() == uri)
            history.removeAt(i);
    }
    history.prepend(QVariantMap{
        {QStringLiteral("uri"), uri},
        {QStringLiteral("name"), name},
        {QStringLiteral("created"), QDateTime::currentDateTime().toString(Qt::ISODate)},
        {QStringLiteral("durationSeconds"), durationSeconds},
        {QStringLiteral("sizeBytes"), sizeBytes},
        {QStringLiteral("mime"),
         QMimeDatabase().mimeTypeForFile(name, QMimeDatabase::MatchExtension).name()},
        {QStringLiteral("thumbnail"), thumbnail.isEmpty() ? QString() : QUrl::fromLocalFile(thumbnail).toString()},
    });
    while (history.size() > 100)
        history.removeLast();
    settings.setValue(QStringLiteral("exports/history"), history);
    emit exportHistoryChanged();
}

QVariantList AppController::exportHistory() const
{
    const QVariantList history = QSettings().value(QStringLiteral("exports/history")).toList();
    QVariantList out;
    for (const QVariant &entry : history) {
        QVariantMap map = entry.toMap();
        const QUrl url(map.value(QStringLiteral("uri")).toString());
        bool exists;
#ifdef Q_OS_ANDROID
        if (AndroidUri::isContentUri(url))
            exists = AndroidUri::openForRead(url) != nullptr;
        else
#endif
            exists = QFileInfo::exists(url.toLocalFile());
        if (!exists)
            continue;
        const QString thumb = QUrl(map.value(QStringLiteral("thumbnail")).toString()).toLocalFile();
        if (!thumb.isEmpty() && !QFileInfo::exists(thumb))
            map.insert(QStringLiteral("thumbnail"), QString());
        out.append(map);
    }
    return out;
}

void AppController::removeExport(const QString &uri)
{
    QSettings settings;
    QVariantList history = settings.value(QStringLiteral("exports/history")).toList();
    bool removed = false;
    for (qsizetype i = history.size() - 1; i >= 0; --i) {
        const QVariantMap map = history.at(i).toMap();
        if (map.value(QStringLiteral("uri")).toString() != uri)
            continue;
        const QString thumb = QUrl(map.value(QStringLiteral("thumbnail")).toString()).toLocalFile();
        if (!thumb.isEmpty())
            QFile::remove(thumb);
        history.removeAt(i);
        removed = true;
    }
    if (!removed)
        return;
    settings.setValue(QStringLiteral("exports/history"), history);
    emit exportHistoryChanged();
}

void AppController::openExportedFile(const QString &uri, bool share)
{
    const QUrl source(uri);
    if (source.isEmpty())
        return;
#ifdef Q_OS_ANDROID
    if (m_sharingExport)
        return;
    m_sharingExport = true;
    emit canShareExportChanged();
    setLastMessage(share ? tr("Getting your video ready to share…") : tr("Opening your video…"));

    const QString name = exportDisplayName(source);
    (void)QtConcurrent::run([this, source, name, share]() {
        Exporter::BackgroundHold hold(QStringLiteral("Preparing to share"));
        QString error;
        const QUrl published = Exporter::publishToGallery(source, name, &error);
        QMetaObject::invokeMethod(
            this,
            [this, published, error, share]() {
                m_sharingExport = false;
                emit canShareExportChanged();
                if (published.isEmpty()) {
                    setLastMessage(error, QStringLiteral("error"));
                    return;
                }
                const bool opened = share ? FileDialogs().shareFile(published) : FileDialogs().viewFile(published);
                if (!opened)
                    setLastMessage(share ? tr("Nothing on this device can share that file")
                                         : tr("Nothing on this device can play that file"),
                                   QStringLiteral("error"));
            },
            Qt::QueuedConnection);
    });
#else
    if (!share)
        QDesktopServices::openUrl(source);
#endif
}

void AppController::shareFile(const QString &uri)
{
    openExportedFile(uri, true);
}

void AppController::viewFile(const QString &uri)
{
    openExportedFile(uri, false);
}

bool AppController::canShareExport() const
{
#ifdef Q_OS_ANDROID
    return !m_lastExportUrl.isEmpty() && !m_sharingExport;
#else
    return false;
#endif
}

void AppController::saveToGallery(const QString &filePath, const QString &displayName)
{
#ifdef Q_OS_ANDROID
    if (filePath.isEmpty())
        return;

    const QString name = displayName.isEmpty() ? QFileInfo(filePath).fileName() : displayName;
    const QUrl source = QUrl::fromLocalFile(filePath);
    const QString mime = QMimeDatabase().mimeTypeForFile(name, QMimeDatabase::MatchExtension).name();
    const QString location = mime.startsWith(QLatin1String("audio/")) ? QStringLiteral("Music/Drift")
                             : mime.startsWith(QLatin1String("image/"))
                                 ? QStringLiteral("Pictures/Drift")
                                 : QStringLiteral("Movies/Drift");

    (void)QtConcurrent::run([this, source, name, location]() {
        Exporter::BackgroundHold hold(QStringLiteral("Saving to gallery"));
        QString error;
        const QUrl published = Exporter::publishToGallery(source, name, &error);
        const bool ok = !published.isEmpty();
        QMetaObject::invokeMethod(
            this,
            [this, name, ok, location, error]() {
                emit savedToGallery(name, ok, ok ? location : QString(), error);
            },
            Qt::QueuedConnection);
    });
#else
    Q_UNUSED(filePath);
    Q_UNUSED(displayName);
#endif
}

void AppController::playLastExport()
{
#ifdef Q_OS_ANDROID
    if (m_lastExportUrl.isEmpty() || m_sharingExport)
        return;

    m_sharingExport = true;
    emit canShareExportChanged();
    setLastMessage(tr("Opening your video…"));

    const QUrl source = m_lastExportUrl;
    const QString name = m_lastExportName;
    (void)QtConcurrent::run([this, source, name]() {
        Exporter::BackgroundHold hold(QStringLiteral("Preparing to play"));
        QString error;
        const QUrl published = Exporter::publishToGallery(source, name, &error);
        QMetaObject::invokeMethod(
            this,
            [this, published, error]() {
                m_sharingExport = false;
                emit canShareExportChanged();
                if (published.isEmpty()) {
                    setLastMessage(error, QStringLiteral("error"));
                    return;
                }
                if (!FileDialogs().viewFile(published))
                    setLastMessage(tr("Nothing on this device can play that file"),
                                   QStringLiteral("error"));
            },
            Qt::QueuedConnection);
    });
#endif
}

void AppController::shareLastExport()
{
#ifdef Q_OS_ANDROID
    if (m_lastExportUrl.isEmpty() || m_sharingExport)
        return;

    // publishToGallery streams every byte of the export into a MediaStore row, reading it back out
    // of the SAF document the user picked — which can be a cloud provider, so the copy is bounded
    // by the network. Inline, as this used to be, that is an ANR on anything longer than a clip.
    // Only the copy moves: shareFile starts an Activity and has to stay on the GUI thread.
    m_sharingExport = true;
    emit canShareExportChanged();
    setLastMessage(tr("Getting your video ready to share…"));

    const QUrl source = m_lastExportUrl;
    const QString name = m_lastExportName;
    (void)QtConcurrent::run([this, source, name]() {
        Exporter::BackgroundHold hold(QStringLiteral("Preparing to share"));
        QString error;
        const QUrl published = Exporter::publishToGallery(source, name, &error);
        QMetaObject::invokeMethod(
            this,
            [this, published, error]() {
                m_sharingExport = false;
                emit canShareExportChanged();
                if (published.isEmpty()) {
                    setLastMessage(error, QStringLiteral("error"));
                    return;
                }
                if (!FileDialogs().shareFile(published))
                    setLastMessage(tr("Nothing on this device can share that file"),
                                   QStringLiteral("error"));
            },
            Qt::QueuedConnection);
    });
#endif
}
namespace {


void copyToClipboard(const QString &text)
{
    if (text.isEmpty())
        return;
    if (QClipboard *clip = QGuiApplication::clipboard())
        clip->setText(text);
}

} // namespace

QVariantMap AppController::debugInfo() const
{
    return DebugReport::collect();
}

QVariantMap AppController::playbackDiagnostics() const
{
    return PlaybackDiagnostics::collect(*m_playback.stats(), &m_project,
                                        m_playback.displayRefreshRate());
}

void AppController::startPlaybackBenchmark()
{
    if (m_benchmarkRunning.exchange(true))
        return;
    emit playbackBenchmarkRunningChanged();

    // The sweep opens its own readers against the same decoders and GL runtime the preview is
    // using. Leaving playback running has the two fight over hardware decoder sessions, which
    // measures neither of them, and the counters the report prints would describe the fight.
    m_playback.pause();

    // The reference clip is what makes two bug reports comparable; the timeline's own first
    // video clip is what reproduces the reporter's actual codec and frame rate. Measure both,
    // because either one alone leaves a question the other answers.
    QString timelineClip;
    for (const drift::Track &track : m_project.tracks()) {
        for (const drift::Clip &clip : track.clips) {
            if (clip.type == drift::ClipType::Video && !clip.path.isEmpty()) {
                timelineClip = clip.path;
                break;
            }
        }
        if (!timelineClip.isEmpty())
            break;
    }
    const QSize canvas(m_project.width(), m_project.height());

    // Off the GUI thread: the sweep decodes for a couple of seconds. Everything it touches —
    // the reader pool and the GL runtime — is already called from the compositor's own worker
    // threads, so this adds no new threading assumption.
    QThreadPool::globalInstance()->start([this, timelineClip, canvas] {
        QVariantMap out;
        out.insert(QStringLiteral("reference"), PlaybackDiagnostics::benchmarkClip({}, canvas));
        if (!timelineClip.isEmpty()) {
            out.insert(QStringLiteral("timeline"),
                       PlaybackDiagnostics::benchmarkClip(timelineClip, canvas));
        }
        QMetaObject::invokeMethod(
            this,
            [this, out] {
                m_benchmarkRunning.store(false);
                emit playbackBenchmarkRunningChanged();
                emit playbackBenchmarkFinished(out);
            },
            Qt::QueuedConnection);
    });
}

void AppController::copyDiagnosticsReport(const QVariantMap &benchmarkInfo)
{
    QVariantMap playbackInfo = playbackDiagnostics();
    // The benchmark's sections ride along; its "rows" key, if it ever grows one, must not
    // displace the ones just collected.
    for (auto it = benchmarkInfo.cbegin(); it != benchmarkInfo.cend(); ++it) {
        if (it.key() != QStringLiteral("rows"))
            playbackInfo.insert(it.key(), it.value());
    }

    QString report = DebugReport::formatPlainText(DebugReport::collect());
    report += QLatin1Char('\n');
    report += PlaybackDiagnostics::formatPlainText(playbackInfo);
    copyToClipboard(report);
}

QString AppController::debugInfoText() const
{
    return DebugReport::formatPlainText(debugInfo());
}

void AppController::exportDiagnostics()
{
    const dafin::log::ExportResult result = dafin::log::exportDiagnostics(debugInfoText());
    if (result.path.isEmpty()) {
        dafin::log::write(QStringLiteral("E"), QStringLiteral("export diagnostics failed: %1").arg(result.error));
        setLastMessage(tr("Couldn’t export diagnostics: %1").arg(result.error), QStringLiteral("error"));
        return;
    }
    dafin::log::openContainingFolder(result.path);
    setLastMessage(tr("Diagnostics saved to %1").arg(QDir::toNativeSeparators(result.path)),
                   QStringLiteral("success"));
}

void AppController::copyDebugInfo()
{
    copyToClipboard(debugInfoText());
}
namespace {


// Every file a project state points at. Thread-safe: it only reads the project it is given.
QStringList stateMediaPaths(const drift::Project &project)
{
    QStringList list;
    for (const drift::MediaAsset &asset : project.assets())
        list.append(asset.path);
    for (const drift::bundle::MediaEntry &entry : drift::bundle::collectMedia(project, false))
        list.append(entry.originalPath);
    list.removeDuplicates();
    return list;
}

const drift::ProjectSnapshotCommand *historyCommand(const QUndoStack &stack, int commandIndex)
{
    return dynamic_cast<const drift::ProjectSnapshotCommand *>(stack.command(commandIndex));
}

} // namespace

QString AppController::historyHashAt(int stackIndex) const
{
    if (stackIndex < 0 || stackIndex > m_undoStack.count())
        return {};
    if (stackIndex == 0) {
        if (m_undoStack.count() == 0)
            return m_project.contentHash();
        const auto *cmd = historyCommand(m_undoStack, 0);
        return cmd ? cmd->beforeHash() : m_project.contentHash();
    }
    const auto *cmd = historyCommand(m_undoStack, stackIndex - 1);
    return cmd ? cmd->afterHash() : QString();
}

int AppController::historyIndexForHash(const QString &prefix) const
{
    const QString needle = prefix.trimmed().toLower();
    if (needle.size() < 8)
        return -1;
    int exact = -1;
    int found = -1;
    int matches = 0;
    for (int i = 0; i <= m_undoStack.count(); ++i) {
        const QString hash = historyHashAt(i).toLower();
        if (hash == needle)
            exact = i;
        if (hash.startsWith(needle)) {
            ++matches;
            found = i;
        }
    }
    if (exact >= 0)
        return exact;
    return matches == 1 ? found : -1;
}

QSet<QString> AppController::historyMediaPaths() const
{
    QSet<QString> paths;
    const drift::HistoryCapture capture = drift::captureHistory(m_undoStack);
    const auto add = [&paths](const drift::HistoryStatePtr &state) {
        const QStringList list = state->mediaPaths(stateMediaPaths);
        for (const QString &path : list)
            paths.insert(path);
    };
    for (const drift::HistoryCapture::Step &step : capture.steps) {
        add(step.before);
        add(step.after);
    }
    return paths;
}

QJsonObject AppController::historyJson(const drift::HistoryCapture &capture,
                                       QList<drift::HistoryStatePtr> *states)
{
    drift::HistoryDocument document = drift::historyToJson(capture);
    if (document.json.isEmpty())
        return {};
    QSet<QString> media;
    for (const drift::HistoryStatePtr &state : std::as_const(document.states)) {
        const QStringList list = state->mediaPaths(stateMediaPaths);
        for (const QString &path : list)
            media.insert(path);
    }
    document.json.insert(QStringLiteral("media"), QJsonArray::fromStringList(media.values()));
    if (states)
        *states = std::move(document.states);
    return document.json;
}

QString AppController::historySnapshotDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + QStringLiteral("/history");
}

void AppController::pruneHistorySnapshots()
{
    constexpr int kMax = 32;
    QDir dir(historySnapshotDir());
    if (!dir.exists())
        return;
    QFileInfoList files = dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Time);
    for (int i = kMax; i < files.size(); ++i)
        QFile::remove(files.at(i).absoluteFilePath());
}

QByteArray AppController::historyJsonAt(int stackIndex) const
{
    if (stackIndex < 0 || stackIndex > m_undoStack.count())
        return {};
    if (stackIndex == 0) {
        if (m_undoStack.count() == 0)
            return m_project.toCompactJson();
        const auto *cmd = historyCommand(m_undoStack, 0);
        return cmd ? cmd->before().toCompactJson() : m_project.toCompactJson();
    }
    const auto *cmd = historyCommand(m_undoStack, stackIndex - 1);
    return cmd ? cmd->after().toCompactJson() : m_project.toCompactJson();
}
