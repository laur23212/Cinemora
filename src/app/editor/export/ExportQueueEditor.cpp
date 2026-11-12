#include "app/editor/export/ExportQueueEditor.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QPointer>

#include "app/editor/AppController.h"
#include "app/editor/ProjectFileController.h"
#include "app/editor/export/ExportQueue.h"
#include "app/editor/sessions/MogrtTitleController.h"
#include "app/platform/OsNotifier.h"
#include "core/project/RenderSnapshot.h"
#include "engine/encode/export/Exporter.h"

namespace {

QString text(const char *source)
{
    return QCoreApplication::translate("ExportQueue", source);
}

// The file the export dialog picked, with the extension its settings imply when it has none.
QString outputPathFor(const QUrl &url, const QVariantMap &settings)
{
    if (!url.isLocalFile())
        return {};
    QString path = url.toLocalFile();
    const ExportSettings s = Exporter::settingsFromMap(settings);
    const QString container =
        s.gifExport ? QStringLiteral("gif")
        : s.audioOnly ? Exporter::preferredAudioOnlyContainer(s.audioCodecId)
                      : Exporter::preferredContainer(s.videoCodecId, s.audioCodecId);
    const QString suffix = Exporter::defaultSuffix(container, s.audioOnly);
    if (QFileInfo(path).suffix().isEmpty() && !suffix.isEmpty())
        path += QLatin1Char('.') + suffix;
    return path;
}

bool renderSnapshot(const drift::RenderSnapshot &snapshot, const QVariantMap &settings,
                    const QString &outputPath, QString *errorOut, const ExportQueue::ProgressFn &progress)
{
    QVariantMap map = settings;
    // Settings without a preset (the CPU fallback drops the hardware one) get the encoder's own
    // default; settingsFromMap would otherwise take the app default, possibly a hardware preset.
    if (!map.contains(QStringLiteral("videoPreset"))) {
        const QString preset = Exporter::videoCodecById(map.value(QStringLiteral("videoCodecId")).toString())
                                   .value(QStringLiteral("defaultPreset"))
                                   .toString();
        if (!preset.isEmpty())
            map.insert(QStringLiteral("videoPreset"), preset);
    }
    QDir().mkpath(QFileInfo(outputPath).absolutePath());
    return Exporter::run(snapshot.project, Exporter::settingsFromMap(map), outputPath, errorOut, progress);
}

} // namespace

void installExportQueue(ExportQueue &queue, AppController &app)
{
    queue.setRunner(&renderSnapshot);

    queue.setCapture([&app](const QUrl &outputUrl, const QVariantMap &settings,
                            std::function<void(drift::RenderSnapshot)> done,
                            std::function<void(const QString &)> failed) {
        const QString outputPath = outputPathFor(outputUrl, settings);
        if (outputPath.isEmpty()) {
            failed(text("That save location isn’t valid"));
            return;
        }
        QPointer<AppController> guard(&app);
        // Taken only once template titles have their full-quality renders, exactly as a normal
        // export does — the render files are immutable, so the snapshot keeps pointing at them.
        const auto take = [guard, settings, outputPath, done]() {
            if (!guard)
                return;
            const QString source = guard->projectFile() ? guard->projectFile()->currentProjectPath() : QString();
            done(drift::RenderSnapshot::capture(*guard->project(), settings, outputPath, source));
        };
        MogrtTitleController *mogrt = app.mogrt();
        if (!mogrt) {
            take();
            return;
        }
        if (mogrt->finalizing()) {
            failed(text("Template titles are still rendering for the export"));
            return;
        }
        mogrt->finalizeForExport(take, failed);
    });

    // One encode at a time: the queue waits while the dialog's own Export runs.
    queue.setBlocked(app.exportInProgress());
    QObject::connect(&app, &AppController::exportInProgressChanged, &queue,
                     [&queue, &app] { queue.setBlocked(app.exportInProgress()); });

    QPointer<OsNotifier> notifier = new OsNotifier(&queue);
    queue.setNotifier([notifier](const QString &title, const QString &body, bool ok,
                                 std::function<void()> openFolder) {
        if (notifier)
            notifier->notify(title, body, ok, std::move(openFolder));
    });

    // The queue is a static in main.cpp and outlives QApplication: the render and the tray icon
    // must end while the app still exists.
    QObject::connect(qApp, &QCoreApplication::aboutToQuit, &queue, [&queue, notifier] {
        queue.setNotifier({});
        delete notifier.data();
        queue.shutdown();
    });
}
