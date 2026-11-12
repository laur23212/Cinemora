#include "app/workers/WorkerSupervisor.h"

#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QProcess>

namespace drift::worker {

namespace {

// After "done" or "error" the worker only has to tidy up; one stuck in a driver's teardown is
// killed rather than left behind, and the result it already sent still counts.
constexpr int kExitAfterResultMs = 10000;
constexpr int kPollMs = 250;

QString describeExit(const QProcess &process)
{
    if (process.exitStatus() == QProcess::CrashExit)
        return QStringLiteral("The worker process crashed");
    return QStringLiteral("The worker process stopped unexpectedly (exit code %1)").arg(process.exitCode());
}

} // namespace

bool parseWorkerLine(const QByteArray &line, QJsonObject *message)
{
    const QByteArray prefix(kWorkerLinePrefix);
    const QByteArray trimmed = line.trimmed();
    // Compare without the prefix's trailing space: trimmed() would have removed it on an empty body.
    if (!trimmed.startsWith(prefix.trimmed()))
        return false;
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(trimmed.mid(prefix.trimmed().size()).trimmed(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return false;
    const QJsonObject object = doc.object();
    if (!object.value(QStringLiteral("t")).isString())
        return false;
    if (message)
        *message = object;
    return true;
}

bool isRestartable(Outcome outcome)
{
    return outcome == Outcome::Crashed || outcome == Outcome::Hung;
}

WorkerResult run(const WorkerSpec &spec, const WorkerCallbacks &callbacks)
{
    WorkerResult result;

    QProcess process;
    // The worker's own log lines (Qt warnings, ffmpeg) land in the editor's log as before.
    process.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    process.start(spec.program, spec.arguments);
    if (!process.waitForStarted(10000)) {
        result.outcome = Outcome::FailedToStart;
        result.message = process.errorString();
        return result;
    }

    QByteArray buffer;
    bool gotDone = false;
    bool gotError = false;
    bool hung = false;
    bool cancelSent = false;
    QElapsedTimer sinceBeat;
    sinceBeat.start();
    QElapsedTimer sinceCancel;
    QElapsedTimer sinceResult;

    const auto drain = [&] {
        buffer += process.readAllStandardOutput();
        qsizetype newline;
        while ((newline = buffer.indexOf('\n')) >= 0) {
            const QByteArray line = buffer.left(newline);
            buffer.remove(0, newline + 1);
            QJsonObject message;
            if (!parseWorkerLine(line, &message))
                continue;
            sinceBeat.restart();
            const QString type = message.value(QStringLiteral("t")).toString();
            if (type == QLatin1String("progress")) {
                if (callbacks.progress)
                    callbacks.progress(qBound(0.0, message.value(QStringLiteral("v")).toDouble(), 1.0));
            } else if (type == QLatin1String("done") && !gotError) {
                gotDone = true;
                message.remove(QStringLiteral("t"));
                result.result = message;
                sinceResult.start();
            } else if (type == QLatin1String("error") && !gotDone) {
                gotError = true;
                result.message = message.value(QStringLiteral("message")).toString();
                sinceResult.start();
            }
        }
    };

    while (process.state() != QProcess::NotRunning) {
        process.waitForReadyRead(kPollMs);
        drain();
        if (process.state() == QProcess::NotRunning)
            break;

        if (!cancelSent && callbacks.cancelled && callbacks.cancelled()) {
            cancelSent = true;
            process.write("cancel\n");
            process.waitForBytesWritten(1000);
            sinceCancel.start();
        }
        if (cancelSent && sinceCancel.elapsed() > spec.cancelGraceMs) {
            process.kill();
            break;
        }
        if ((gotDone || gotError) && sinceResult.elapsed() > kExitAfterResultMs) {
            process.kill();
            break;
        }
        if (!cancelSent && !gotDone && !gotError && sinceBeat.elapsed() > spec.heartbeatTimeoutMs) {
            hung = true;
            process.kill();
            break;
        }
    }
    process.waitForFinished(5000);
    drain();
    if (!buffer.isEmpty()) {
        buffer += '\n';
        drain();
    }
    result.exitCode = process.exitCode();

    if (gotDone) {
        result.outcome = Outcome::Finished;
    } else if (cancelSent) {
        result.outcome = Outcome::Cancelled;
        result.message = QStringLiteral("Cancelled");
    } else if (gotError) {
        result.outcome = Outcome::Failed;
    } else if (hung) {
        result.outcome = Outcome::Hung;
        result.message = QStringLiteral("The worker process stopped responding for %1 seconds")
                             .arg(spec.heartbeatTimeoutMs / 1000);
    } else {
        result.outcome = Outcome::Crashed;
        result.message = describeExit(process);
    }
    return result;
}

WorkerResult runWithWatchdog(const WorkerSpec &spec, int maxRestarts, const WorkerCallbacks &callbacks)
{
    WorkerResult result = run(spec, callbacks);
    int restarts = 0;
    while (isRestartable(result.outcome) && restarts < maxRestarts
           && !(callbacks.cancelled && callbacks.cancelled())) {
        ++restarts;
        if (callbacks.restarting)
            callbacks.restarting(result.outcome, restarts);
        result = run(spec, callbacks);
    }
    result.restarts = restarts;
    return result;
}

} // namespace drift::worker
