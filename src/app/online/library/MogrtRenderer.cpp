#include "app/online/library/MogrtRenderer.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>

namespace dafin::assets {

namespace {

const QString kSettingsKey = QStringLiteral("mogrt/converterPath");

bool runnable(const QString &path)
{
    const QFileInfo info(path);
    return info.isFile() && info.isExecutable();
}

// Where the converter's own installers put it.
QStringList standardLocations()
{
    QStringList out;
#if defined(Q_OS_WIN)
    // Inno Setup's {autopf}: Program Files for an all-users install, the user's Programs folder for
    // a per-user one. mogrt.exe is the console twin of the windowed app, made for the command line.
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (const char *var : {"ProgramFiles", "ProgramW6432", "ProgramFiles(x86)"}) {
        const QString base = env.value(QLatin1String(var));
        if (!base.isEmpty())
            out << base + QStringLiteral("/MOGRT Converter/mogrt.exe");
    }
    const QString local = env.value(QStringLiteral("LOCALAPPDATA"));
    if (!local.isEmpty())
        out << local + QStringLiteral("/Programs/MOGRT Converter/mogrt.exe");
#elif defined(Q_OS_MACOS)
    out << QStringLiteral("/Applications/MOGRT Converter.app/Contents/MacOS/MOGRT Converter")
        << QDir::homePath() + QStringLiteral("/Applications/MOGRT Converter.app/Contents/MacOS/MOGRT Converter");
#else
    // packaging/linux/install.sh copies the app here; its binary answers the command-line verbs.
    out << QDir::homePath() + QStringLiteral("/.local/opt/mogrt-converter/mogrt-converter")
        << QDir::homePath() + QStringLiteral("/.local/bin/mogrt-converter");
#endif
    return out;
}

} // namespace

static MogrtRenderer *g_instance = nullptr;

MogrtRenderer::MogrtRenderer(QObject *parent)
    : QObject(parent)
{
    m_executable = findExecutable();
    g_instance = this;
}

MogrtRenderer::~MogrtRenderer()
{
    if (g_instance == this)
        g_instance = nullptr;
    for (Job &job : m_running) {
        if (job.process) {
            job.process->disconnect(this);
            job.process->kill();
            job.process->waitForFinished(2000);
        }
        cleanup(job);
    }
}

MogrtRenderer *MogrtRenderer::instance()
{
    return g_instance;
}

QString MogrtRenderer::customPath() const
{
    return QSettings().value(kSettingsKey).toString();
}

void MogrtRenderer::setCustomPath(const QString &path)
{
    if (path == customPath())
        return;
    if (path.isEmpty())
        QSettings().remove(kSettingsKey);
    else
        QSettings().setValue(kSettingsKey, path);
    refresh();
}

QString MogrtRenderer::downloadUrl() const
{
    return QStringLiteral("https://github.com/nielsfranke/MOGRT-Converter/releases");
}

QString MogrtRenderer::findExecutable() const
{
    const QString custom = customPath();
    if (!custom.isEmpty() && runnable(custom))
        return QFileInfo(custom).absoluteFilePath();
    for (const QString &path : standardLocations()) {
        if (runnable(path))
            return QFileInfo(path).absoluteFilePath();
    }
    // A pip/uv install puts `mogrt` on PATH.
    for (const char *name : {"mogrt", "mogrt-converter"}) {
        const QString found = QStandardPaths::findExecutable(QLatin1String(name));
        if (!found.isEmpty())
            return found;
    }
    return {};
}

void MogrtRenderer::refresh()
{
    const QString found = findExecutable();
    if (found == m_executable)
        return;
    m_executable = found;
    emit availabilityChanged();
}

bool MogrtRenderer::useProgram(const QString &pathOrUrl)
{
    const QUrl url(pathOrUrl);
    const QString path = url.isLocalFile() ? url.toLocalFile() : pathOrUrl;
    if (!runnable(path))
        return false;
    QString chosen = QFileInfo(path).absoluteFilePath();
#if defined(Q_OS_WIN)
    // Picking the windowed app is the natural thing to do; its console twin sits beside it.
    const QString twin = QFileInfo(chosen).absolutePath() + QStringLiteral("/mogrt.exe");
    if (QFileInfo(chosen).fileName().compare(QLatin1String("mogrt.exe"), Qt::CaseInsensitive) != 0 && runnable(twin))
        chosen = twin;
#endif
    QSettings().setValue(kSettingsKey, chosen);
    refresh();
    if (m_executable != chosen) {
        m_executable = chosen;
        emit availabilityChanged();
    }
    return true;
}

void MogrtRenderer::bump()
{
    ++m_revision;
    emit jobsChanged();
}

QString MogrtRenderer::signature() const
{
    if (m_executable.isEmpty())
        return {};
    const QFileInfo info(m_executable);
    return QStringLiteral("%1|%2|%3").arg(info.absoluteFilePath()).arg(info.size()).arg(
        info.lastModified().toSecsSinceEpoch());
}

QString MogrtRenderer::submit(const Request &request)
{
    const QString key = QStringLiteral("mogrt-%1").arg(++m_counter);
    Job job;
    job.request = request;
    job.order = m_counter;
    m_queued.insert(key, job);
    m_progress.insert(key, 0);
    bump();
    // Answered on the next turn of the event loop, never from inside submit(): the caller records
    // the key first.
    QMetaObject::invokeMethod(this, &MogrtRenderer::pump, Qt::QueuedConnection);
    return key;
}

void MogrtRenderer::pump()
{
    constexpr int kMaxRunning = 2;
    while (m_running.size() < kMaxRunning && !m_queued.isEmpty()) {
        QString next;
        for (auto it = m_queued.cbegin(); it != m_queued.cend(); ++it) {
            if (next.isEmpty()) {
                next = it.key();
                continue;
            }
            const Job &best = m_queued.value(next);
            if (it->request.priority > best.request.priority
                || (it->request.priority == best.request.priority && it->order < best.order))
                next = it.key();
        }
        start(next);
    }
}

void MogrtRenderer::start(const QString &key)
{
    Job job = m_queued.take(key);
    const Request &r = job.request;
    if (m_executable.isEmpty()) {
        m_running.insert(key, job);
        fail(key, tr("MOGRT Converter is not installed."));
        return;
    }
    if (!QFileInfo::exists(r.templatePath)) {
        m_running.insert(key, job);
        fail(key, tr("The template file is missing."));
        return;
    }

    QStringList args{r.verb, r.templatePath, QStringLiteral("--json")};
    QString workDir = QDir::tempPath();
    if (r.verb != QLatin1String("info")) {
        const QFileInfo out(r.output);
        QDir().mkpath(out.absolutePath());
        workDir = out.absolutePath();
        // Same folder, so the final rename never crosses drives.
        const QString tag = QUuid::createUuid().toString(QUuid::Id128).left(10);
        job.partial = out.absolutePath() + QLatin1Char('/') + out.completeBaseName() + QStringLiteral(".part-")
                      + tag + QLatin1Char('.') + out.suffix();
        args << QStringLiteral("-o") << job.partial;
        if (!r.values.isEmpty()) {
            job.valuesFile = out.absolutePath() + QStringLiteral("/values-") + tag + QStringLiteral(".json");
            QFile file(job.valuesFile);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)
                || file.write(QJsonDocument(r.values).toJson(QJsonDocument::Compact)) < 0) {
                m_running.insert(key, job);
                fail(key, tr("Could not write the template values (%1).").arg(QDir::toNativeSeparators(job.valuesFile)));
                return;
            }
            file.close();
            args << QStringLiteral("--values") << job.valuesFile;
        }
        if (r.scale > 0 && qAbs(r.scale - 1.0) > 1e-3)
            args << QStringLiteral("--scale") << QString::number(r.scale, 'f', 3);
        if (r.durationSeconds >= 0.1)
            args << QStringLiteral("-d") << QString::number(r.durationSeconds, 'f', 3);
        if (r.verb == QLatin1String("still")) {
            if (r.timeSeconds >= 0)
                args << QStringLiteral("-t") << QString::number(r.timeSeconds, 'f', 3);
        } else {
            // Titles carry no sound in Dafin, and ProRes 4444 keeps the transparency.
            args << QStringLiteral("-f") << QStringLiteral("prores4444") << QStringLiteral("--no-audio");
        }
    }

    auto *process = new QProcess(this);
    process->setProgram(m_executable);
    process->setArguments(args);
    process->setWorkingDirectory(workDir);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
    env.insert(QStringLiteral("PYTHONUTF8"), QStringLiteral("1"));
    process->setProcessEnvironment(env);
    job.process = process;
    m_running.insert(key, job);

    connect(process, &QProcess::readyReadStandardOutput, this, [this, key, process] {
        auto it = m_running.find(key);
        if (it != m_running.end())
            it->stdoutData += process->readAllStandardOutput();
    });
    connect(process, &QProcess::readyReadStandardError, this, [this, key, process] {
        auto it = m_running.find(key);
        if (it == m_running.end())
            return;
        const QByteArray chunk = process->readAllStandardError();
        it->stderrTail = (it->stderrTail + chunk).right(8192);
        // "  Frame 25/120  (3s, noch ~12s)" on a carriage-return line.
        static const QRegularExpression frame(QStringLiteral("Frame (\\d+)/(\\d+)"));
        QRegularExpressionMatchIterator m = frame.globalMatch(QString::fromUtf8(chunk));
        QRegularExpressionMatch last;
        while (m.hasNext())
            last = m.next();
        if (last.hasMatch() && last.captured(2).toDouble() > 0) {
            m_progress.insert(key, qBound(0.0, last.captured(1).toDouble() / last.captured(2).toDouble(), 1.0));
            bump();
        }
    });
    connect(process, &QProcess::finished, this, [this, key](int exitCode, QProcess::ExitStatus status) {
        finish(key, exitCode, status == QProcess::CrashExit);
    });
    connect(process, &QProcess::errorOccurred, this, [this, key](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            finish(key, -1, true);
    });
    process->start();
    bump();
}

void MogrtRenderer::cleanup(Job &job)
{
    if (!job.partial.isEmpty())
        QFile::remove(job.partial);
    if (!job.valuesFile.isEmpty())
        QFile::remove(job.valuesFile);
    job.partial.clear();
    job.valuesFile.clear();
}

void MogrtRenderer::fail(const QString &key, const QString &message)
{
    Job job = m_running.take(key);
    if (job.process) {
        job.process->disconnect(this);
        job.process->deleteLater();
    }
    cleanup(job);
    m_progress.remove(key);
    bump();
    emit jobFinished(key, false, {}, message);
    QMetaObject::invokeMethod(this, &MogrtRenderer::pump, Qt::QueuedConnection);
}

void MogrtRenderer::finish(const QString &key, int exitCode, bool crashed)
{
    auto it = m_running.find(key);
    if (it == m_running.end())
        return;
    QProcess *process = it->process;
    if (process)
        it->stdoutData += process->readAllStandardOutput();

    // With --json the converter ends with one JSON object on stdout, errors included.
    QJsonObject result;
    const QByteArray out = it->stdoutData.trimmed();
    const int brace = out.indexOf('{');
    if (brace >= 0)
        result = QJsonDocument::fromJson(out.mid(brace)).object();

    const bool fileVerb = it->request.verb != QLatin1String("info");
    const bool ok = !crashed && exitCode == 0 && result.value(QStringLiteral("ok")).toBool(true)
                    && !result.isEmpty();
    if (ok && fileVerb) {
        const QString written = result.value(QStringLiteral("output")).toString(it->partial);
        const QString target = it->request.output;
        QFile::remove(target);
        if (!QFileInfo::exists(written) || !QFile::rename(written, target)) {
            fail(key, tr("MOGRT Converter finished, but its file could not be put in place (%1).")
                          .arg(QDir::toNativeSeparators(target)));
            return;
        }
        result.insert(QStringLiteral("output"), target);
    }
    if (ok) {
        Job job = m_running.take(key);
        if (job.process)
            job.process->deleteLater();
        job.partial.clear();
        cleanup(job);
        m_progress.remove(key);
        bump();
        emit jobFinished(key, true, result, {});
        QMetaObject::invokeMethod(this, &MogrtRenderer::pump, Qt::QueuedConnection);
        return;
    }

    QString message = result.value(QStringLiteral("error")).toString();
    if (message.isEmpty() && crashed && exitCode == -1)
        message = tr("MOGRT Converter could not be started (%1).").arg(QDir::toNativeSeparators(m_executable));
    if (message.isEmpty()) {
        // The last meaningful line of what it printed.
        const QStringList lines = QString::fromUtf8(it->stderrTail).split(QRegularExpression(QStringLiteral("[\\r\\n]+")),
                                                                         Qt::SkipEmptyParts);
        for (auto line = lines.crbegin(); line != lines.crend(); ++line) {
            const QString l = line->trimmed();
            if (!l.isEmpty() && !l.startsWith(QLatin1String("Frame ")) && !l.startsWith(QLatin1String("WARNUNG"))) {
                message = l;
                break;
            }
        }
    }
    if (message.isEmpty())
        message = tr("MOGRT Converter stopped without rendering the template.");
    else if (message.contains(QLatin1String("After-Effects-MOGRTs")) || message.contains(QLatin1String("ppro")))
        message = tr("This template was made in Premiere Pro. MOGRT Converter only renders templates made in After Effects.");
    fail(key, message);
}

void MogrtRenderer::cancel(const QString &key)
{
    if (m_queued.contains(key)) {
        m_queued.remove(key);
        m_progress.remove(key);
        bump();
        emit jobFinished(key, false, {}, tr("Cancelled."));
        return;
    }
    auto it = m_running.find(key);
    if (it == m_running.end())
        return;
    QProcess *process = it->process;
    if (process) {
        process->disconnect(this);
        process->kill();
        process->waitForFinished(2000);
    }
    fail(key, tr("Cancelled."));
}

bool MogrtRenderer::isActive(const QString &key) const
{
    return m_queued.contains(key) || m_running.contains(key);
}

double MogrtRenderer::progress(const QString &key) const
{
    return m_progress.value(key);
}

} // namespace dafin::assets
