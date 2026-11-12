#include "app/workers/ExportWorker.h"
#include "app/workers/WorkerChannel.h"

#include "core/project/Project.h"
#include "engine/encode/export/Exporter.h"
#include "engine/encode/proxies/ReverseProxyCache.h"
#include "engine/packages/assets/EmojiCatalog.h"
#include "engine/packages/assets/FontCatalog.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>

#include <cstdio>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace drift::worker {

int runExportWorker(int argc, char *argv[])
{
#ifdef Q_OS_WIN
    // A crash here must end the process, not wait on Windows' "stopped working" box: the editor
    // is watching and reports it.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
    QString jobPath;
    for (int i = 1; i + 1 < argc; ++i) {
        if (qstrcmp(argv[i], "--export-worker") == 0)
            jobPath = QString::fromLocal8Bit(argv[i + 1]);
    }

    // Same names as the editor, so settings, the add-on registry and caches are the same folders.
    QCoreApplication::setApplicationName(QStringLiteral("Dafin"));
    QCoreApplication::setOrganizationName(QStringLiteral("Dafin"));
    // QApplication as in HeadlessApp: the compositor needs a GL context on an offscreen surface,
    // and no window is ever opened.
    QApplication app(argc, argv);

    WorkerChannel channel;
    if (jobPath.isEmpty()) {
        channel.error(QStringLiteral("No export job was given"));
        return 2;
    }

    QFile file(jobPath);
    if (!file.open(QIODevice::ReadOnly)) {
        channel.error(QStringLiteral("Could not read the export job: %1").arg(file.errorString()));
        return 2;
    }
    const QJsonObject job = QJsonDocument::fromJson(file.readAll()).object();
    file.close();

    QString error;
    const drift::Project project =
        drift::Project::fromJson(job.value(QStringLiteral("project")).toObject(), &error);
    if (!error.isEmpty()) {
        channel.error(QStringLiteral("Could not read the project for export: %1").arg(error));
        return 2;
    }
    const ExportSettings settings =
        Exporter::settingsFromMap(job.value(QStringLiteral("settings")).toObject().toVariantMap());
    const QString outputPath = job.value(QStringLiteral("output")).toString();
    if (outputPath.isEmpty()) {
        channel.error(QStringLiteral("The export job has no output file"));
        return 2;
    }

    // What the editor loads at startup that rendering reads: text and caption faces, emoji, and
    // the reverse-clip proxies already built. Nothing is swept here; that is the editor's job.
    reloadFontCatalog();
    reloadUserFonts();
    reloadEmojiCatalog();
    drift::ReverseProxyCache::instance().load();

    bool working = false;
    bool finishing = false;
    const auto onProgress = [&](double fraction) {
        if (!working) {
            working = true;
            channel.setPhase(WorkerChannel::Phase::Working);
        }
        // The muxer closing the file reports no progress; that is not a hang.
        if (!finishing && fraction >= 0.999) {
            finishing = true;
            channel.setPhase(WorkerChannel::Phase::Finishing);
        }
        channel.progress(fraction);
        return !channel.cancelled();
    };

    const bool ok = Exporter::run(project, settings, outputPath, &error, onProgress);
    if (channel.cancelled())
        return 1; // the editor asked for it and already knows
    if (!ok) {
        channel.error(error.isEmpty() ? QStringLiteral("Export failed") : error);
        return 1;
    }
    channel.done();
    std::fflush(stdout);
    return 0;
}

} // namespace drift::worker
