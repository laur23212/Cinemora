#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <functional>

// Dafin: runs heavy work in a separate process so a crash or a hang there never takes the editor
// down. The worker talks over its stdout, one line per message, each line starting with
// kWorkerLinePrefix and followed by a JSON object (see WorkerChannel.h for the worker side):
//
//   {"t":"hb"}                         heartbeat
//   {"t":"progress","v":0.42}          progress 0–1 (counts as a heartbeat too)
//   {"t":"done"}                       finished; anything else in the object is the result
//   {"t":"error","message":"…"}        the work itself failed (no restart: it would fail again)
//
// Anything else on stdout (a library's own logging) is ignored. Writing "cancel" to the worker's
// stdin asks it to stop; closing stdin (the editor went away) means the same thing.
//
// run() blocks the calling thread until the worker ends, so call it from a pool thread, never the
// GUI thread. No event loop is needed on that thread.
namespace drift::worker {

inline constexpr char kWorkerLinePrefix[] = "@@dafin-worker ";

// No heartbeat for this long means the worker is hung: it is killed.
inline constexpr int kDefaultHeartbeatTimeoutMs = 15000;

enum class Outcome {
    Finished,     // sent "done"
    Failed,       // sent "error": the work failed, the worker itself is fine
    Crashed,      // exited without "done"/"error" (crash, abort, killed from outside)
    Hung,         // no heartbeat within the timeout; killed by the watchdog
    FailedToStart,
    Cancelled,
};

struct WorkerSpec
{
    QString program;
    QStringList arguments;
    int heartbeatTimeoutMs = kDefaultHeartbeatTimeoutMs;
    // After asking a worker to cancel, how long it gets to stop before it is killed.
    int cancelGraceMs = 5000;
};

struct WorkerResult
{
    Outcome outcome = Outcome::Crashed;
    QString message;    // from "error", or what went wrong with the process
    QJsonObject result; // the "done" object
    int exitCode = -1;
    int restarts = 0; // how many times the watchdog started it again
};

// Callbacks run on the thread that called run(). `cancelled` is polled about four times a second.
struct WorkerCallbacks
{
    std::function<void(double)> progress;
    std::function<bool()> cancelled;
    // runWithWatchdog: called before each automatic restart, with why the last run stopped.
    std::function<void(Outcome, int attempt)> restarting;
};

// One run of the worker.
WorkerResult run(const WorkerSpec &spec, const WorkerCallbacks &callbacks = {});

// Runs the worker and, when it crashes or hangs, starts it again up to `maxRestarts` times. A
// worker that reports an error, is cancelled or cannot start at all is not restarted.
WorkerResult runWithWatchdog(const WorkerSpec &spec, int maxRestarts,
                             const WorkerCallbacks &callbacks = {});

// True for the outcomes a restart could fix.
bool isRestartable(Outcome outcome);

// Parses one stdout line; false when it is not a worker message.
bool parseWorkerLine(const QByteArray &line, QJsonObject *message);

} // namespace drift::worker
