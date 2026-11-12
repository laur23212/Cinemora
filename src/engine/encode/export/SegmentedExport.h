#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <functional>

#include "engine/encode/export/Exporter.h"

namespace drift {
class Project;
}

// Dafin: resumable long exports. A long video export renders in segments (about 45 s each, a whole
// number of frames) into a job folder, then joins them into the final file without re-encoding the
// video. If Dafin stops halfway (crash, power cut, killed process), the job folder survives and the
// next launch offers to resume from the last finished segment.
//
// Sync across the boundaries: each segment renders its frames at the same timeline times a
// single-pass export gives them (ExportSettings::segmentFirstFrame), and starts on a keyframe
// because every segment is a fresh encoder. Audio is not cut at all: the segments' own audio is
// dropped at the join and the range's audio is encoded once, as one piece, so there are no
// encoder-delay gaps at the cuts and the sound cannot drift from the picture.
namespace SegmentedExport {

struct Segment
{
    int64_t firstFrame = 0;
    int64_t frameCount = 0;
};

// Splits `totalFrames` frames at `fpsNum/fpsDen` into consecutive segments of about
// `segmentSeconds` each, every one a whole number of frames. A last piece shorter than a third of
// a segment is folded into the one before it, so no segment is a few frames long.
QList<Segment> planSegments(int64_t totalFrames, int fpsNum, int fpsDen, double segmentSeconds);

struct Options
{
    double segmentSeconds = 45.0;
    // Exports shorter than this run in one pass, as before.
    double minDurationSeconds = 120.0;
    // Where job folders live; empty = defaultJobsRoot().
    QString jobsRoot;
    // What the export dialog sent and the target the user picked, kept in the job so a resume
    // after a restart can run the same export again.
    QVariantMap settingsMap;
    QString outputUrl;
    // Continue the job already in `jobsRoot` for this output path instead of starting over.
    bool resume = false;
};

// <app data>/export-resume.
QString defaultJobsRoot();

// True for exports that can run in segments: video exports (not GIF, not audio-only) to a real
// file on desktop. Others always run in one pass.
bool supports(const ExportSettings &settings, const QString &outputPath);

// The job folder an export to `outputPath` uses under `jobsRoot`.
QString jobDirFor(const QString &jobsRoot, const QString &outputPath);

// Exports `project` to `outputPath`. Short or unsupported exports go straight to Exporter::run.
// Long ones render in segments under a job folder; with options.resume the segments already
// finished there are kept. The job folder is removed when the export succeeds or is cancelled,
// and kept when it fails or the process dies, so it can be resumed.
bool run(const drift::Project &project, const ExportSettings &settings, const QString &outputPath,
         const Options &options, QString *errorOut, const Exporter::ProgressFn &onProgress = {});

// A job left behind by an export that did not finish.
struct PendingJob
{
    QString dir;
    QString outputPath;
    QString outputUrl;
    int segmentsDone = 0;
    int segmentsTotal = 0;
    QDateTime updated;
};

QList<PendingJob> pendingJobs(const QString &jobsRoot = {});

// Reads a pending job back: the project snapshot it was exporting and the settings it used.
// Fails, with a message naming the first missing file, when media the project uses is gone.
bool loadJob(const QString &dir, drift::Project *project, QVariantMap *settingsMap,
             QString *outputPath, QString *outputUrl, QString *errorOut);

void discardJob(const QString &dir);

// Writes `outputPath` from the video packets of `segmentPaths` (in order, each shifted onto its
// segment's first frame) and the audio packets of `audioPath`, interleaved, with no re-encode.
// Fails if a segment does not start on a keyframe.
bool joinSegments(const QStringList &segmentPaths, const QList<Segment> &segments, int fpsNum,
                  int fpsDen, const QString &audioPath, const QString &outputPath,
                  QString *errorOut, const std::function<bool(double)> &onProgress = {});

} // namespace SegmentedExport
