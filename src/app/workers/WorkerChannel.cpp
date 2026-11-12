#include "app/workers/WorkerChannel.h"
#include "app/workers/WorkerSupervisor.h"

#include <QByteArray>
#include <QJsonDocument>

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace drift::worker {

namespace {

std::atomic<int> g_startingLimitMs{120000};
std::atomic<int> g_workingLimitMs{12000};
std::atomic<int> g_finishingLimitMs{600000};
std::atomic<int> g_beatIntervalMs{2000};

qint64 nowMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// Raw reads, not stdio: a thread parked in fgets holds stdin's FILE lock, and exit() flushing
// the streams would then wait on it forever.
qint64 readStdin(char *buffer, int size)
{
#ifdef Q_OS_WIN
    DWORD read = 0;
    if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), buffer, DWORD(size), &read, nullptr))
        return -1;
    return qint64(read);
#else
    return qint64(::read(0, buffer, size_t(size)));
#endif
}

} // namespace

struct WorkerChannel::State
{
    std::mutex writeMutex;
    std::mutex waitMutex;
    std::condition_variable wake;
    std::atomic<bool> stop{false};
    std::atomic<bool> cancel{false};
    std::atomic<int> phase{int(Phase::Starting)};
    std::atomic<qint64> phaseSince{0};
    std::atomic<qint64> lastProgress{0};
    qint64 lastProgressSent = 0; // under writeMutex

    void send(const QJsonObject &message)
    {
        const QByteArray line = QByteArray(kWorkerLinePrefix)
                                + QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
        std::lock_guard<std::mutex> lock(writeMutex);
        std::fwrite(line.constData(), 1, size_t(line.size()), stdout);
        std::fflush(stdout);
    }

    bool shouldBeat() const
    {
        const qint64 now = nowMs();
        switch (Phase(phase.load())) {
        case Phase::Starting:
            return now - phaseSince.load() < g_startingLimitMs.load();
        case Phase::Working:
            return now - lastProgress.load() < g_workingLimitMs.load();
        case Phase::Finishing:
            return now - phaseSince.load() < g_finishingLimitMs.load();
        }
        return false;
    }
};

WorkerChannel::WorkerChannel()
    : m_state(std::make_shared<State>())
{
    m_state->phaseSince = nowMs();
    m_state->lastProgress = nowMs();
    m_state->send({{QStringLiteral("t"), QStringLiteral("hb")}});

    std::shared_ptr<State> state = m_state;
    std::thread([state] {
        std::unique_lock<std::mutex> lock(state->waitMutex);
        while (!state->stop.load()) {
            state->wake.wait_for(lock, std::chrono::milliseconds(g_beatIntervalMs.load()));
            if (state->stop.load())
                break;
            if (state->shouldBeat())
                state->send({{QStringLiteral("t"), QStringLiteral("hb")}});
        }
    }).detach();

    // Detached on purpose: a blocking read cannot be woken portably. It ends with the process.
    std::thread([state] {
        QByteArray line;
        char chunk[256];
        for (;;) {
            const qint64 n = readStdin(chunk, int(sizeof chunk));
            if (n <= 0) {
                // The editor closed the pipe or is gone: nobody wants the result any more.
                state->cancel = true;
                return;
            }
            line.append(chunk, n);
            if (line.contains("cancel")) {
                state->cancel = true;
                return;
            }
            if (line.size() > 4096)
                line.clear();
        }
    }).detach();
}

WorkerChannel::~WorkerChannel()
{
    m_state->stop = true;
    m_state->wake.notify_all();
}

void WorkerChannel::setPhase(Phase phase)
{
    m_state->phase = int(phase);
    m_state->phaseSince = nowMs();
    m_state->lastProgress = nowMs();
}

void WorkerChannel::progress(double fraction)
{
    const qint64 now = nowMs();
    m_state->lastProgress = now;
    {
        std::lock_guard<std::mutex> lock(m_state->writeMutex);
        if (now - m_state->lastProgressSent < 200 && fraction < 1.0)
            return;
        m_state->lastProgressSent = now;
    }
    m_state->send({{QStringLiteral("t"), QStringLiteral("progress")}, {QStringLiteral("v"), fraction}});
}

bool WorkerChannel::cancelled() const
{
    return m_state->cancel.load();
}

void WorkerChannel::done(const QJsonObject &result)
{
    QJsonObject message = result;
    message.insert(QStringLiteral("t"), QStringLiteral("done"));
    m_state->send(message);
}

void WorkerChannel::error(const QString &message)
{
    m_state->send({{QStringLiteral("t"), QStringLiteral("error")}, {QStringLiteral("message"), message}});
}

void WorkerChannel::setLimitsForTesting(int startingMs, int workingMs, int finishingMs, int beatIntervalMs)
{
    g_beatIntervalMs = beatIntervalMs;
    g_startingLimitMs = startingMs;
    g_workingLimitMs = workingMs;
    g_finishingLimitMs = finishingMs;
}

} // namespace drift::worker
