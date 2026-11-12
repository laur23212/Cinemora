#pragma once

#include <QJsonObject>
#include <QString>

#include <atomic>
#include <memory>

// Dafin: the worker side of WorkerSupervisor's protocol. Construct one at the top of a worker's
// main; it starts a heartbeat thread and a thread that listens on stdin for "cancel".
//
// The heartbeat is not sent blindly: a thread that beats while the work is stuck would hide the
// hang it is there to reveal. It beats only while the work shows signs of life:
//   Starting  — loading fonts, opening a GL context, probing media: beats for up to 2 minutes.
//   Working   — beats only while progress() was called in the last 12 seconds.
//   Finishing — writing the file's index and closing it, where no progress is reported: beats for
//               up to 10 minutes.
// So the supervisor's 15-second timeout trips when the work itself stops moving.
namespace drift::worker {

class WorkerChannel
{
public:
    enum class Phase { Starting, Working, Finishing };

    WorkerChannel();
    ~WorkerChannel();
    WorkerChannel(const WorkerChannel &) = delete;
    WorkerChannel &operator=(const WorkerChannel &) = delete;

    void setPhase(Phase phase);
    // Sends progress (throttled to a few per second) and marks the work as alive.
    void progress(double fraction);
    // The editor asked to stop, or went away (stdin closed).
    bool cancelled() const;

    void done(const QJsonObject &result = {});
    void error(const QString &message);

    // How long each phase may beat without progress, and how often it beats. Tests shorten them.
    static void setLimitsForTesting(int startingMs, int workingMs, int finishingMs, int beatIntervalMs);

private:
    struct State;
    std::shared_ptr<State> m_state;
};

} // namespace drift::worker
