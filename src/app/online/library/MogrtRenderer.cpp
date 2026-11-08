#include "app/online/library/MogrtRenderer.h"

#include "app/online/library/SessionFiles.h"

#include <QCoreApplication>
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

MogrtRenderer::MogrtRenderer(QObject *parent)
    : QObject(parent)
{
    m_executable = findExecutable();
}

MogrtRenderer::~MogrtRenderer()
{
    for (Job &job : m_jobs) {
        if (job.process) {
            job.process->disconnect(this);
            job.process->kill();
            job.process->waitForFinished(2000);
        }
    }
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

QString MogrtRenderer::render(const QString &mogrtPath, const QVariantList &fields, double durationSeconds,
                              const QString &title)
{
    const QString key = QStringLiteral("mogrt-%1").arg(++m_counter);
    if (m_executable.isEmpty()) {
        m_jobs.insert(key, Job{nullptr, QStringLiteral("failed"), tr("MOGRT Converter is not installed."), {}, title, {}, {}, 0});
        bump();
        emit failed(key, m_jobs.value(key).message);
        return key;
    }
    if (!QFileInfo::exists(mogrtPath)) {
        m_jobs.insert(key, Job{nullptr, QStringLiteral("failed"), tr("The template file is missing."), {}, title, {}, {}, 0});
        bump();
        emit failed(key, m_jobs.value(key).message);
        return key;
    }

    // In the session folder like other downloads: deleted when Dafin closes, and embedded in a
    // saved project that uses it, so the project keeps it.
    const QString dir = session::root() + QStringLiteral("/mogrt-renders");
    QDir().mkpath(dir);
    QString base = QFileInfo(mogrtPath).completeBaseName();
    base.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|\\x00-\\x1f]+")), QStringLiteral("_"));
    base = base.simplified().left(60);
    if (base.isEmpty())
        base = QStringLiteral("title");
    const QString output = dir + QLatin1Char('/') + base + QLatin1Char('-')
                           + QUuid::createUuid().toString(QUuid::Id128).left(8) + QStringLiteral(".mov");

    QStringList args{QStringLiteral("render"), mogrtPath, QStringLiteral("--json"), QStringLiteral("-f"),
                     QStringLiteral("prores4444"), QStringLiteral("-o"), output};
    if (durationSeconds >= 0.5)
        args << QStringLiteral("-d") << QString::number(durationSeconds, 'f', 3);
    for (const QVariant &v : fields) {
        const QVariantMap field = v.toMap();
        QString id = field.value(QStringLiteral("id")).toString().trimmed();
        if (id.isEmpty())
            id = field.value(QStringLiteral("label")).toString().trimmed();
        if (id.isEmpty() || id.contains(QLatin1Char('=')))
            continue;
        // The converter reads "\n" in a value as a line break.
        QString text = field.value(QStringLiteral("text")).toString();
        text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
        text.replace(QLatin1Char('\n'), QLatin1String("\\n"));
        args << QStringLiteral("--set") << id + QLatin1Char('=') + text;
    }

    auto *process = new QProcess(this);
    process->setProgram(m_executable);
    process->setArguments(args);
    process->setWorkingDirectory(dir);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
    env.insert(QStringLiteral("PYTHONUTF8"), QStringLiteral("1"));
    process->setProcessEnvironment(env);

    Job job;
    job.process = process;
    job.state = QStringLiteral("rendering");
    job.message = tr("Starting MOGRT Converter…");
    job.output = output;
    job.title = title;
    m_jobs.insert(key, job);

    connect(process, &QProcess::readyReadStandardOutput, this, [this, key, process] {
        auto it = m_jobs.find(key);
        if (it != m_jobs.end())
            it->stdoutData += process->readAllStandardOutput();
    });
    connect(process, &QProcess::readyReadStandardError, this, [this, key, process] {
        auto it = m_jobs.find(key);
        if (it == m_jobs.end())
            return;
        const QByteArray chunk = process->readAllStandardError();
        it->stderrTail = (it->stderrTail + chunk).right(8192);
        // "  Frame 25/120  (3s, noch ~12s)" on a carriage-return line.
        static const QRegularExpression frame(QStringLiteral("Frame (\\d+)/(\\d+)"));
        QRegularExpressionMatchIterator m = frame.globalMatch(QString::fromUtf8(chunk));
        QRegularExpressionMatch last;
        while (m.hasNext())
            last = m.next();
        if (last.hasMatch()) {
            const double n = last.captured(2).toDouble();
            if (n > 0) {
                it->progress = qBound(0.0, last.captured(1).toDouble() / n, 1.0);
                it->message = tr("Rendering frame %1 of %2…").arg(last.captured(1), last.captured(2));
                bump();
            }
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
    return key;
}

void MogrtRenderer::finish(const QString &key, int exitCode, bool crashed)
{
    auto it = m_jobs.find(key);
    if (it == m_jobs.end() || it->state != QLatin1String("rendering"))
        return;
    QProcess *process = it->process;
    if (process) {
        it->stdoutData += process->readAllStandardOutput();
        process->deleteLater();
    }
    it->process = nullptr;

    // With --json the converter ends with one JSON object on stdout, errors included.
    QJsonObject result;
    const QByteArray out = it->stdoutData.trimmed();
    const int brace = out.indexOf('{');
    if (brace >= 0)
        result = QJsonDocument::fromJson(out.mid(brace)).object();

    QStringList warnings;
    for (const QJsonValue &w : result.value(QStringLiteral("warnings")).toArray())
        warnings << w.toString();
    const QString output = result.value(QStringLiteral("output")).toString(it->output);

    if (!crashed && exitCode == 0 && result.value(QStringLiteral("ok")).toBool(true) && QFileInfo::exists(output)) {
        it->state = QStringLiteral("done");
        it->progress = 1;
        it->output = output;
        it->message = tr("Rendered.");
        bump();
        emit rendered(key, output, it->title, warnings);
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
            if (!l.isEmpty() && !l.startsWith(QLatin1String("Frame "))) {
                message = l;
                break;
            }
        }
    }
    if (message.isEmpty())
        message = tr("MOGRT Converter stopped without rendering the template.");
    else if (message.contains(QLatin1String("After-Effects-MOGRTs")) || message.contains(QLatin1String("ppro")))
        message = tr("This template was made in Premiere Pro. MOGRT Converter only renders templates made in After Effects.");
    it->state = QStringLiteral("failed");
    it->message = message;
    QFile::remove(it->output);
    bump();
    emit failed(key, message);
}

void MogrtRenderer::cancel(const QString &key)
{
    auto it = m_jobs.find(key);
    if (it == m_jobs.end() || it->state != QLatin1String("rendering"))
        return;
    QProcess *process = it->process;
    it->state = QStringLiteral("failed");
    it->message = tr("Cancelled.");
    it->process = nullptr;
    if (process) {
        process->disconnect(this);
        process->kill();
        process->waitForFinished(2000);
        process->deleteLater();
    }
    QFile::remove(it->output);
    bump();
}

QString MogrtRenderer::state(const QString &key) const
{
    return m_jobs.value(key).state;
}

double MogrtRenderer::progress(const QString &key) const
{
    return m_jobs.value(key).progress;
}

QString MogrtRenderer::message(const QString &key) const
{
    return m_jobs.value(key).message;
}

} // namespace dafin::assets
