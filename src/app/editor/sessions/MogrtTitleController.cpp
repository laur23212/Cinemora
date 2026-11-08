#include "app/editor/sessions/MogrtTitleController.h"

#include "app/editor/AppController.h"
#include "app/editor/AppControllerDetail.h"
#include "app/editor/appcontroller/AppControllerInternal.h"
#include "app/online/library/MogrtInfo.h"
#include "app/online/library/MogrtRenderer.h"
#include "core/edit/TimelineOps.h"
#include "core/project/Project.h"

#include <QColor>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>

#include <cmath>

using dafin::assets::MogrtRenderer;
using drift::appdetail::setClipLayoutPixels;

namespace {

// Bumped when what a render key covers changes, so old renders are not mistaken for new ones.
constexpr int kRenderKeyVersion = 1;
constexpr double kDefaultSeconds = 5.0;
// Renders not touched for this long are deleted at startup. Saved projects carry their own copy.
constexpr qint64 kCacheMaxAgeDays = 45;

QString localPath(const QString &pathOrUrl)
{
    const QUrl url(pathOrUrl);
    return url.isLocalFile() ? url.toLocalFile() : pathOrUrl;
}

bool editableType(const QString &type)
{
    static const QStringList kinds{QStringLiteral("text"), QStringLiteral("slider"), QStringLiteral("angle"),
                                   QStringLiteral("checkbox"), QStringLiteral("color"), QStringLiteral("point"),
                                   QStringLiteral("point3d"), QStringLiteral("scale")};
    return kinds.contains(type);
}

// The converter writes its warnings in German; the common ones in the user's words.
QString readableWarning(const QString &raw)
{
    QString w = raw.trimmed();
    if (w.startsWith(QLatin1String("WARNUNG:")))
        w = w.mid(8).trimmed();
    QRegularExpressionMatch m;
    static const QRegularExpression font(QStringLiteral("^Font '([^']+)' nicht gefunden"));
    static const QRegularExpression effect(QStringLiteral("^Effekt '([^']+)'.*nicht unterstützt"));
    static const QRegularExpression style(QStringLiteral("^Ebenenstil '([^']+)'.*nicht unterstützt"));
    static const QRegularExpression shape(QStringLiteral("^Shape-Element '([^']+)'.*nicht unterstützt"));
    static const QRegularExpression expr(QStringLiteral("^Expression auf '([^']*)' / '([^']*)' fehlgeschlagen: ?(.*)$"));
    static const QRegularExpression control(QStringLiteral("^Ziel für Regler '([^']+)' nicht gefunden"));
    static const QRegularExpression eps(QStringLiteral("^EPS '([^']+)'"));
    if ((m = font.match(w)).hasMatch())
        return QObject::tr("The font “%1” is not installed; another font is used in its place.").arg(m.captured(1));
    if ((m = effect.match(w)).hasMatch())
        return QObject::tr("The effect “%1” is not supported yet and was left out.").arg(m.captured(1));
    if ((m = style.match(w)).hasMatch())
        return QObject::tr("The layer style “%1” is not supported yet and was left out.").arg(m.captured(1));
    if ((m = shape.match(w)).hasMatch())
        return QObject::tr("The shape element “%1” is not supported yet and was left out.").arg(m.captured(1));
    if ((m = expr.match(w)).hasMatch())
        return QObject::tr("An expression on “%1” › “%2” could not be run (%3).").arg(m.captured(1), m.captured(2), m.captured(3));
    if ((m = control.match(w)).hasMatch())
        return QObject::tr("The control “%1” changes nothing in this template.").arg(m.captured(1));
    if ((m = eps.match(w)).hasMatch())
        return QObject::tr("The vector artwork “%1” needs Ghostscript; a low-resolution copy is used.").arg(m.captured(1));
    return w;
}

QStringList readableWarnings(const QJsonArray &list)
{
    QStringList out;
    for (const QJsonValue &v : list) {
        const QString w = readableWarning(v.toString());
        if (!w.isEmpty() && !out.contains(w))
            out << w;
    }
    return out;
}

// A control's value from QML as the converter reads it.
QJsonValue jsonValueFor(const QString &type, const QVariant &value)
{
    if (type == QLatin1String("text"))
        return value.toString();
    if (type == QLatin1String("checkbox"))
        return value.toBool();
    if (type == QLatin1String("slider") || type == QLatin1String("angle"))
        return value.toDouble();
    if (type == QLatin1String("color")) {
        QColor c;
        if (value.metaType().id() == QMetaType::QColor)
            c = value.value<QColor>();
        else if (value.canConvert<QString>() && !value.toString().isEmpty())
            c = QColor(value.toString());
        if (c.isValid())
            return QJsonArray{c.redF(), c.greenF(), c.blueF(), c.alphaF()};
    }
    if (value.canConvert<QVariantList>()) {
        QJsonArray a;
        for (const QVariant &x : value.toList())
            a.append(x.toDouble());
        if (!a.isEmpty())
            return a;
    }
    return QJsonValue::fromVariant(value);
}

// The converter looks a value up by control id, then by control name. Its ids keep the type they
// have in definition.json, so a numeric id never matches a JSON key: those controls also get their
// value under their name.
QJsonObject converterValues(const drift::MogrtSource &source)
{
    QJsonObject out = source.values;
    static const QRegularExpression digits(QStringLiteral("^\\d+$"));
    for (const QJsonValue &v : source.controls) {
        const QJsonObject c = v.toObject();
        const QString id = c.value(QStringLiteral("id")).toString();
        const QString label = c.value(QStringLiteral("label")).toString();
        if (source.values.contains(id) && digits.match(id).hasMatch() && !label.isEmpty() && !out.contains(label))
            out.insert(label, source.values.value(id));
    }
    return out;
}

QVariant qmlValueFor(const QString &type, const QJsonValue &value)
{
    if (type == QLatin1String("color") && value.isArray()) {
        const QJsonArray a = value.toArray();
        QColor c;
        c.setRgbF(float(qBound(0.0, a.at(0).toDouble(), 1.0)), float(qBound(0.0, a.at(1).toDouble(), 1.0)),
                  float(qBound(0.0, a.at(2).toDouble(), 1.0)), float(qBound(0.0, a.size() > 3 ? a.at(3).toDouble() : 1.0, 1.0)));
        return c;
    }
    return value.toVariant();
}

} // namespace

MogrtTitleController::MogrtTitleController(AppController *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
{
    m_reconcileTimer = new QTimer(this);
    m_reconcileTimer->setSingleShot(true);
    // Long enough to fold a drag's many updates into one pass, short enough to feel immediate.
    m_reconcileTimer->setInterval(250);
    connect(m_reconcileTimer, &QTimer::timeout, this, [this] { reconcile(); });
    connect(m_app, &AppController::tracksChanged, this, &MogrtTitleController::scheduleReconcile);

    if (MogrtRenderer *renderer = MogrtRenderer::instance()) {
        connect(renderer, &MogrtRenderer::jobFinished, this, &MogrtTitleController::onJobFinished);
        // Queued: a card's binding can start a check, and the change must not land inside it.
        connect(renderer, &MogrtRenderer::jobsChanged, this, &MogrtTitleController::bump, Qt::QueuedConnection);
        connect(renderer, &MogrtRenderer::availabilityChanged, this, [this] {
            // A converter that just appeared may render what failed without one.
            m_failed.clear();
            for (Template &t : m_templates) {
                if (t.checkState == QLatin1String("failed"))
                    t.checkState.clear();
            }
            bump();
            scheduleReconcile();
        });
    }

    // Old renders go; anything the open project still uses is made again on demand.
    const QDateTime cutoff = QDateTime::currentDateTime().addDays(-kCacheMaxAgeDays);
    QDirIterator it(storeRoot() + QStringLiteral("/cache"), QDir::Files);
    while (it.hasNext()) {
        const QFileInfo info(it.next());
        if (info.lastModified() < cutoff || info.fileName().contains(QLatin1String(".part-")))
            QFile::remove(info.absoluteFilePath());
    }
}

MogrtTitleController::~MogrtTitleController() = default;

QString MogrtTitleController::storeRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/mogrt");
}

// The same id the Titles grid gives an imported template (UserLibrary): its file's SHA-1.
QString MogrtTitleController::contentId(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha1);
    if (!hash.addData(&f))
        return {};
    return QString::fromLatin1(hash.result().toHex().left(16));
}

bool MogrtTitleController::converterAvailable() const
{
    const MogrtRenderer *renderer = MogrtRenderer::instance();
    return renderer && renderer->available();
}

bool MogrtTitleController::busy() const
{
    for (const Render &r : m_jobs) {
        if (!r.key.isEmpty())
            return true;
    }
    return false;
}

double MogrtTitleController::finalizeProgress() const
{
    if (!m_finalizing || m_finalizeTotal <= 0)
        return 0;
    double done = m_finalizeTotal - m_finalizeWants.size();
    const MogrtRenderer *renderer = MogrtRenderer::instance();
    for (const QString &want : m_finalizeWants) {
        const QString job = m_jobForWant.value(want);
        if (renderer && !job.isEmpty())
            done += renderer->progress(job);
    }
    return qBound(0.0, done / m_finalizeTotal, 1.0);
}

void MogrtTitleController::bump()
{
    ++m_revision;
    emit changed();
}

// ---- templates ---------------------------------------------------------------------------------

QString MogrtTitleController::importToStore(const QString &path, QString *error)
{
    const QFileInfo info(path);
    if (!info.isFile()) {
        *error = tr("The template file is missing.");
        return {};
    }
    const QString fileKey = info.absoluteFilePath() + QLatin1Char('|') + QString::number(info.size())
                            + QLatin1Char('|') + QString::number(info.lastModified().toMSecsSinceEpoch());
    QString id = m_idForFile.value(fileKey);
    if (id.isEmpty()) {
        id = contentId(path);
        if (id.isEmpty()) {
            *error = tr("Couldn't read %1.").arg(info.fileName());
            return {};
        }
        m_idForFile.insert(fileKey, id);
    }
    const QString dir = storeRoot() + QStringLiteral("/templates");
    const QString stored = dir + QLatin1Char('/') + id + QStringLiteral(".mogrt");
    if (!QFileInfo::exists(stored)) {
        QDir().mkpath(dir);
        const QString partial = stored + QStringLiteral(".part-") + QUuid::createUuid().toString(QUuid::Id128).left(8);
        if (!QFile::copy(path, partial) || !QFile::rename(partial, stored)) {
            QFile::remove(partial);
            // Still usable where it is; only the managed copy is missing.
            Template &t = m_templates[id];
            if (t.path.isEmpty())
                t.path = info.absoluteFilePath();
            return id;
        }
    }
    Template &t = m_templates[id];
    t.path = stored;
    return id;
}

MogrtTitleController::Template *MogrtTitleController::templateFor(const QString &id, const QString &path)
{
    if (id.isEmpty())
        return nullptr;
    Template &t = m_templates[id];
    if (t.path.isEmpty() || !QFileInfo::exists(t.path)) {
        const QString stored = storeRoot() + QStringLiteral("/templates/") + id + QStringLiteral(".mogrt");
        t.path = QFileInfo::exists(stored) ? stored : path;
    }
    if (!t.parsed)
        parse(t);
    if (t.checkState.isEmpty())
        loadCheck(id, t);
    return &t;
}

void MogrtTitleController::parse(Template &t)
{
    t.parsed = true;
    QString error;
    const std::optional<dafin::assets::mogrt::Info> info = dafin::assets::mogrt::read(t.path, &error);
    t.readable = info.has_value();
    if (!info) {
        t.readError = error.isEmpty() ? tr("This file is not a Motion Graphics Template.") : error;
        return;
    }
    t.title = info->title;
    t.authorApp = info->authorApp;
    t.controls = info->controls;
    t.width = info->width;
    t.height = info->height;
    t.fps = info->fps;
    t.durationSeconds = info->durationSeconds;
    if (!info->posterEntry.isEmpty()) {
        const QString poster = storeRoot() + QStringLiteral("/templates/") + QFileInfo(t.path).completeBaseName()
                               + QStringLiteral(".poster.") + QFileInfo(info->posterEntry).suffix().toLower();
        if (QFileInfo::exists(poster)
            || dafin::assets::mogrt::extract(t.path, info->posterEntry, poster, 32LL * 1024 * 1024))
            t.posterPath = poster;
    }
}

// The converter's verdict on a template is kept beside it, for the converter build that gave it.
void MogrtTitleController::loadCheck(const QString &id, Template &t) const
{
    QFile file(storeRoot() + QStringLiteral("/templates/") + id + QStringLiteral(".check.json"));
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject o = QJsonDocument::fromJson(file.readAll()).object();
    const MogrtRenderer *renderer = MogrtRenderer::instance();
    if (!renderer || o.value(QStringLiteral("converter")).toString() != renderer->signature())
        return;
    t.checkState = o.value(QStringLiteral("state")).toString();
    t.checkMessage = o.value(QStringLiteral("message")).toString();
    t.warnings.clear();
    for (const QJsonValue &w : o.value(QStringLiteral("warnings")).toArray())
        t.warnings << w.toString();
}

void MogrtTitleController::saveCheck(const QString &id, const Template &t) const
{
    const MogrtRenderer *renderer = MogrtRenderer::instance();
    if (!renderer)
        return;
    QDir().mkpath(storeRoot() + QStringLiteral("/templates"));
    QSaveFile file(storeRoot() + QStringLiteral("/templates/") + id + QStringLiteral(".check.json"));
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(QJsonObject{{QStringLiteral("state"), t.checkState},
                                         {QStringLiteral("message"), t.checkMessage},
                                         {QStringLiteral("warnings"), QJsonArray::fromStringList(t.warnings)},
                                         {QStringLiteral("converter"), renderer->signature()}})
                   .toJson(QJsonDocument::Compact));
    file.commit();
}

void MogrtTitleController::startCheck(const QString &id, Template &t)
{
    MogrtRenderer *renderer = MogrtRenderer::instance();
    if (!renderer || !renderer->available() || !t.readable || !t.checkJob.isEmpty())
        return;
    MogrtRenderer::Request r;
    r.verb = QStringLiteral("still");
    r.templatePath = t.path;
    r.output = storeRoot() + QStringLiteral("/cache/") + id + QStringLiteral("-check.png");
    r.scale = 0.25;
    r.priority = -1;  // behind renders the user is waiting to see
    t.checkState = QStringLiteral("checking");
    t.checkJob = renderer->submit(r);
    m_jobs.insert(t.checkJob, Render{QString(), QStringLiteral("check"), id, r.output});
    // Asked from a card's binding: announcing the change from inside it would loop.
    QMetaObject::invokeMethod(this, &MogrtTitleController::bump, Qt::QueuedConnection);
}

QVariantMap MogrtTitleController::templateStatus(const QString &templateId, const QString &pathOrUrl)
{
    const QString path = localPath(pathOrUrl);
    // Only a content id (16 hex digits, as the Titles grid gives) names a template here.
    static const QRegularExpression hashId(QStringLiteral("^[0-9a-f]{16}$"));
    QString id = hashId.match(templateId).hasMatch() ? templateId : QString();
    if (id.isEmpty()) {
        QString error;
        id = importToStore(path, &error);
        if (id.isEmpty())
            return {{QStringLiteral("state"), QStringLiteral("failed")}, {QStringLiteral("message"), error}};
    }
    Template *t = templateFor(id, path);
    if (!t->readable)
        return {{QStringLiteral("state"), QStringLiteral("failed")}, {QStringLiteral("message"), t->readError}};
    if (t->authorApp == QLatin1String("ppro")) {
        return {{QStringLiteral("state"), QStringLiteral("unsupported")},
                {QStringLiteral("message"), tr("Made in Premiere Pro. MOGRT Converter only animates templates made in After Effects; "
                                               "it can still be added as an ordinary Dafin title.")}};
    }
    if (!converterAvailable()) {
        return {{QStringLiteral("state"), QStringLiteral("needsConverter")},
                {QStringLiteral("message"), tr("Install the free MOGRT Converter to play this template's animation.")}};
    }
    if (t->checkState.isEmpty())
        startCheck(id, *t);
    return {{QStringLiteral("state"), t->checkState.isEmpty() ? QStringLiteral("checking") : t->checkState},
            {QStringLiteral("warnings"), t->warnings},
            {QStringLiteral("message"), t->checkMessage}};
}

double MogrtTitleController::templateDurationSeconds(const QString &pathOrUrl)
{
    const QString path = localPath(pathOrUrl);
    QString error;
    const QString id = importToStore(path, &error);
    const Template *t = templateFor(id, path);
    if (!t || t->durationSeconds < 0.1)
        return kDefaultSeconds;
    return qBound(0.5, t->durationSeconds, 300.0);
}

QVariantMap MogrtTitleController::addTemplate(const QString &pathOrUrl, double atSeconds, int trackIndex,
                                              const QString &label)
{
    const QString path = localPath(pathOrUrl);
    QString error;
    const QString id = importToStore(path, &error);
    if (id.isEmpty()) {
        m_app->setLastMessage(error, QStringLiteral("error"));
        return {{QStringLiteral("ok"), false}, {QStringLiteral("message"), error}};
    }
    Template *t = templateFor(id, path);
    if (!t->readable) {
        m_app->setLastMessage(t->readError, QStringLiteral("error"));
        return {{QStringLiteral("ok"), false}, {QStringLiteral("message"), t->readError}};
    }
    const QString name = (t->title.isEmpty() ? label : t->title).trimmed();
    const double seconds = t->durationSeconds >= 0.1 ? qBound(0.5, t->durationSeconds, 300.0) : kDefaultSeconds;

    // Nothing here can draw it: an ordinary title with the template's text, said plainly.
    const bool premiere = t->authorApp == QLatin1String("ppro");
    if (premiere || !converterAvailable()) {
        if (const std::optional<dafin::assets::mogrt::Info> info = dafin::assets::mogrt::read(t->path)) {
            if (atSeconds >= 0)
                m_app->setPlayheadSeconds(atSeconds);
            m_app->addTemplateTitle(dafin::assets::mogrt::titleText(*info), dafin::assets::mogrt::titlePreset(*info),
                                    seconds, name);
        }
        const QString message = premiere
                                    ? tr("“%1” was made in Premiere Pro, which MOGRT Converter can't animate. "
                                         "It was added as an editable Dafin title.").arg(name)
                                    : tr("“%1” was added as an editable Dafin title. Install MOGRT Converter "
                                         "(Settings › Asset providers) to keep the template's animation.").arg(name);
        m_app->setLastMessage(message, QStringLiteral("warning"));
        return {{QStringLiteral("ok"), true}, {QStringLiteral("fallback"), true}, {QStringLiteral("message"), message}};
    }

    drift::Project &project = m_app->m_project;
    const drift::Project before = project;
    const drift::TimeUs duration = drift::secondsToUs(seconds);
    const drift::TimeUs wantStart = atSeconds < 0 ? m_app->m_playheadUs : drift::secondsToUs(atSeconds);

    int target = trackIndex;
    if (target < 0 || target >= project.tracks().size()
        || !project.tracks().at(target).allowsClipType(drift::ClipType::Video)) {
        target = drift::ensureTrackForClipType(project, drift::ClipType::Video, true);
        if (target < 0)
            return {{QStringLiteral("ok"), false}};
        // Over whatever already plays there, like a second title.
        const AppController::StackPlan plan = m_app->stackPlan(target, drift::ClipType::Video, wantStart, duration);
        if (plan.insertIndex >= 0)
            target = drift::insertTrackAboveForClipType(project, plan.insertIndex, drift::ClipType::Video);
        else if (plan.track >= 0)
            target = plan.track;
    }
    drift::Track &track = project.tracks()[target];
    const drift::TimeUs start = drift::resolveClipStart(project, track, -1, wantStart, duration, m_app->m_snapEnabled,
                                                        m_app->m_playheadUs);

    drift::Clip clip;
    clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    clip.type = drift::ClipType::Video;
    clip.name = (name.isEmpty() ? QFileInfo(path).completeBaseName() : name).left(48);
    clip.timelineStart = start;
    clip.timelineDuration = duration;
    clip.srcIn = 0;
    clip.srcOut = duration;
    clip.thumbnailPath = t->posterPath;
    clip.mogrt.templateId = id;
    clip.mogrt.templatePath = t->path;
    clip.mogrt.name = clip.name;
    clip.mogrt.authorApp = t->authorApp;
    clip.mogrt.controls = t->controls;
    clip.mogrt.width = t->width;
    clip.mogrt.height = t->height;
    clip.mogrt.fps = t->fps;
    clip.mogrt.templateDurationUs = drift::secondsToUs(t->durationSeconds);
    clip.mogrt.status = t->checkState == QLatin1String("warnings") ? QStringLiteral("warnings") : QString();
    clip.mogrt.warnings = t->warnings;

    // The template's frame, fitted to the canvas the way a video of that shape would be.
    const double canvasW = qMax(1, project.width());
    const double canvasH = qMax(1, project.height());
    if (t->width > 0 && t->height > 0) {
        const double scale = qMin(canvasW / t->width, canvasH / t->height);
        const double w = t->width * scale;
        const double h = t->height * scale;
        setClipLayoutPixels(clip, (canvasW - w) / 2.0, (canvasH - h) / 2.0, w, h);
    } else {
        setClipLayoutPixels(clip, 0, 0, canvasW, canvasH);
    }

    track.clips.append(clip);
    const int clipIndex = int(track.clips.size()) - 1;
    m_app->pushProjectEdit(before, tr("Template title added"));
    m_app->finishEdit(tr("Template title added"));
    m_app->selectClip(target, clipIndex);
    if (m_app->m_playheadUs < start || m_app->m_playheadUs >= start + duration)
        m_app->setPlayheadSeconds(drift::usToSeconds(start));
    // First render now rather than after the pass's delay.
    m_reconcileTimer->stop();
    reconcile();
    return {{QStringLiteral("ok"), true}, {QStringLiteral("fallback"), false}};
}

// ---- clips -------------------------------------------------------------------------------------

drift::Clip *MogrtTitleController::clipAt(int track, int clip) const
{
    QList<drift::Track> &tracks = m_app->m_project.tracks();
    if (track < 0 || track >= tracks.size() || clip < 0 || clip >= tracks[track].clips.size())
        return nullptr;
    drift::Clip &c = tracks[track].clips[clip];
    return c.mogrt.isEmpty() ? nullptr : &c;
}

int64_t MogrtTitleController::frameRoundedDurationUs(const drift::Clip &clip)
{
    const double fps = clip.mogrt.fps > 0 ? clip.mogrt.fps : 30.0;
    const double frames = std::max(1.0, std::round(drift::usToSeconds(clip.timelineDuration) * fps));
    return drift::secondsToUs(frames / fps);
}

QString MogrtTitleController::renderKey(const drift::Clip &clip)
{
    // QJsonObject keeps its keys sorted, so equal states give equal bytes.
    const QJsonObject state{{QStringLiteral("v"), kRenderKeyVersion},
                            {QStringLiteral("t"), clip.mogrt.templateId},
                            {QStringLiteral("values"), clip.mogrt.values},
                            {QStringLiteral("d"), double(frameRoundedDurationUs(clip))}};
    return QString::fromLatin1(
        QCryptographicHash::hash(QJsonDocument(state).toJson(QJsonDocument::Compact), QCryptographicHash::Sha1)
            .toHex()
            .left(24));
}

QString MogrtTitleController::renderPath(const QString &key, const QString &quality)
{
    return storeRoot() + QStringLiteral("/cache/") + key + QLatin1Char('-') + quality + QStringLiteral(".mov");
}

QString MogrtTitleController::resolveTemplatePath(const drift::Clip &clip) const
{
    if (QFileInfo::exists(clip.mogrt.templatePath))
        return clip.mogrt.templatePath;
    const QString stored = storeRoot() + QStringLiteral("/templates/") + clip.mogrt.templateId + QStringLiteral(".mogrt");
    if (QFileInfo::exists(stored))
        return stored;
    const auto it = m_templates.constFind(clip.mogrt.templateId);
    if (it != m_templates.constEnd() && QFileInfo::exists(it->path))
        return it->path;
    return {};
}

void MogrtTitleController::request(const drift::Clip &clip, const QString &key, const QString &quality, int priority)
{
    const QString want = wantKey(key, quality);
    if (m_jobForWant.contains(want) || m_failed.contains(want))
        return;
    MogrtRenderer *renderer = MogrtRenderer::instance();
    const QString templatePath = resolveTemplatePath(clip);
    if (!renderer || !renderer->available() || templatePath.isEmpty())
        return;
    MogrtRenderer::Request r;
    r.verb = QStringLiteral("render");
    r.templatePath = templatePath;
    r.values = converterValues(clip.mogrt);
    r.output = renderPath(key, quality);
    r.durationSeconds = drift::usToSeconds(frameRoundedDurationUs(clip));
    // Half size is a quarter of the pixels: quick enough to follow typing, sharp enough to judge.
    r.scale = quality == QLatin1String("full") || clip.mogrt.width < 960 ? 1.0 : 0.5;
    r.priority = priority;
    const QString job = renderer->submit(r);
    m_jobs.insert(job, Render{key, quality, clip.mogrt.templateId, r.output});
    m_jobForWant.insert(want, job);
}

void MogrtTitleController::scheduleReconcile()
{
    m_reconcileTimer->start();
}

bool MogrtTitleController::reconcile()
{
    drift::Project &project = m_app->m_project;
    bool changed = false;
    QSet<QString> previewsWanted;
    QList<std::pair<drift::Clip, QString>> toRequest;

    project.forEachTrackList([&](QList<drift::Track> &tracks) {
        for (drift::Track &track : tracks) {
            for (drift::Clip &clip : track.clips) {
                if (clip.mogrt.isEmpty())
                    continue;
                // The render is the template at the clip's length: no speed, no slip, no reverse.
                const drift::TimeUs length = clip.timelineDuration;
                if (clip.srcIn != 0 || clip.srcOut != length || clip.speed != 1.0 || clip.reverse
                    || clip.hasSpeedCurve()) {
                    clip.srcIn = 0;
                    clip.srcOut = length;
                    clip.speed = 1.0;
                    clip.reverse = false;
                    clip.speedCurve = {};
                    changed = true;
                }
                const QString templatePath = resolveTemplatePath(clip);
                if (templatePath.isEmpty()) {
                    if (clip.mogrt.status != QLatin1String("missing")) {
                        clip.mogrt.status = QStringLiteral("missing");
                        changed = true;
                    }
                } else {
                    if (templatePath != clip.mogrt.templatePath) {
                        clip.mogrt.templatePath = templatePath;
                        changed = true;
                    }
                    if (clip.mogrt.status == QLatin1String("missing")) {
                        clip.mogrt.status.clear();
                        changed = true;
                    }
                }

                const QString key = renderKey(clip);
                const QString full = renderPath(key, QStringLiteral("full"));
                const QString preview = renderPath(key, QStringLiteral("preview"));
                const bool current = clip.mogrt.renderKey == key && QFileInfo::exists(clip.path);
                const auto use = [&](const QString &path, const QString &quality) {
                    if (clip.path == path && clip.mogrt.renderKey == key && clip.mogrt.renderQuality == quality)
                        return;
                    clip.path = path;
                    clip.mogrt.renderKey = key;
                    clip.mogrt.renderQuality = quality;
                    const auto warnings = m_renderWarnings.constFind(key);
                    if (warnings != m_renderWarnings.constEnd()) {
                        clip.mogrt.warnings = *warnings;
                        clip.mogrt.status = warnings->isEmpty() ? QStringLiteral("ok") : QStringLiteral("warnings");
                    }
                    clip.mogrt.lastError.clear();
                    changed = true;
                };
                if (current && clip.mogrt.renderQuality == QLatin1String("full"))
                    continue;
                if (QFileInfo::exists(full)) {
                    use(full, QStringLiteral("full"));
                    continue;
                }
                if (current)
                    continue;
                if (QFileInfo::exists(preview)) {
                    use(preview, QStringLiteral("preview"));
                    continue;
                }
                // The last render stays on screen until the new one is in.
                if (templatePath.isEmpty())
                    continue;
                previewsWanted.insert(wantKey(key, QStringLiteral("preview")));
                const auto failed = m_failed.constFind(wantKey(key, QStringLiteral("preview")));
                if (failed != m_failed.constEnd()) {
                    if (clip.mogrt.lastError != *failed) {
                        clip.mogrt.lastError = *failed;
                        clip.mogrt.status = QStringLiteral("failed");
                        changed = true;
                    }
                    continue;
                }
                toRequest.append({clip, key});
            }
        }
    });

    // Renders nothing shows any more (the text was typed on, the edit undone) are dropped.
    if (MogrtRenderer *renderer = MogrtRenderer::instance()) {
        const QHash<QString, QString> active = m_jobForWant;
        for (auto it = active.cbegin(); it != active.cend(); ++it) {
            if (it.key().endsWith(QLatin1String("|preview")) && !previewsWanted.contains(it.key()))
                renderer->cancel(it.value());
        }
    }
    for (const auto &[clip, key] : std::as_const(toRequest))
        request(clip, key, QStringLiteral("preview"), 1);

    if (changed)
        m_app->notifyTracksChanged();
    bump();
    return changed;
}

void MogrtTitleController::onJobFinished(const QString &job, bool ok, const QJsonObject &result, const QString &error)
{
    const auto it = m_jobs.constFind(job);
    if (it == m_jobs.constEnd())
        return;
    const Render render = *it;
    m_jobs.erase(it);

    if (render.quality == QLatin1String("check")) {
        Template &t = m_templates[render.templateId];
        t.checkJob.clear();
        if (error == tr("Cancelled.")) {
            t.checkState.clear();
        } else if (ok) {
            t.warnings = readableWarnings(result.value(QStringLiteral("warnings")).toArray());
            t.checkState = t.warnings.isEmpty() ? QStringLiteral("ok") : QStringLiteral("warnings");
            t.checkMessage.clear();
            saveCheck(render.templateId, t);
        } else {
            const bool premiere = error.contains(QLatin1String("Premiere"));
            t.checkState = premiere ? QStringLiteral("unsupported") : QStringLiteral("failed");
            t.checkMessage = error;
            if (premiere || converterAvailable())
                saveCheck(render.templateId, t);
        }
        QFile::remove(render.output);
        bump();
        return;
    }

    const QString want = wantKey(render.key, render.quality);
    m_jobForWant.remove(want);
    if (ok) {
        m_renderWarnings.insert(render.key, readableWarnings(result.value(QStringLiteral("warnings")).toArray()));
    } else if (error != tr("Cancelled.")) {
        m_failed.insert(want, error);
        if (m_finalizeWants.contains(want)) {
            const auto failed = m_finalizeFailed;
            cancelFinalize();
            if (failed)
                failed(tr("A template title could not be rendered: %1").arg(error));
        }
    }
    reconcile();
    checkFinalize();
}

QVariantMap MogrtTitleController::clipState(int track, int clipIndex) const
{
    const drift::Clip *clip = clipAt(track, clipIndex);
    if (!clip)
        return {};
    const MogrtRenderer *renderer = MogrtRenderer::instance();
    const QString key = renderKey(*clip);
    QString renderingJob = m_jobForWant.value(wantKey(key, QStringLiteral("full")));
    QString renderingQuality = QStringLiteral("full");
    if (renderingJob.isEmpty()) {
        renderingJob = m_jobForWant.value(wantKey(key, QStringLiteral("preview")));
        renderingQuality = QStringLiteral("preview");
    }
    const bool rendering = !renderingJob.isEmpty();
    const bool upToDate = clip->mogrt.renderKey == key && QFileInfo::exists(clip->path);
    const bool templateMissing = resolveTemplatePath(*clip).isEmpty();

    QString status = clip->mogrt.status;
    QString message;
    const QString failed = m_failed.value(wantKey(key, QStringLiteral("preview")),
                                          m_failed.value(wantKey(key, QStringLiteral("full"))));
    if (templateMissing) {
        status = QStringLiteral("missing");
        message = tr("The template file is missing. Locate it to render this title again.");
    } else if (clip->mogrt.authorApp == QLatin1String("ppro")) {
        status = QStringLiteral("unsupported");
        message = tr("Made in Premiere Pro: MOGRT Converter can't render it.");
    } else if (!converterAvailable()) {
        status = QStringLiteral("needsConverter");
        message = tr("MOGRT Converter is not installed, so changes can't be rendered. The last render keeps playing.");
    } else if (!failed.isEmpty() && !upToDate) {
        status = QStringLiteral("failed");
        message = failed;
    } else if (rendering) {
        message = renderingQuality == QLatin1String("full") ? tr("Rendering at full quality…") : tr("Updating preview…");
    }

    QVariantList controls;
    for (const QJsonValue &v : clip->mogrt.controls) {
        const QJsonObject c = v.toObject();
        const QString id = c.value(QStringLiteral("id")).toString();
        const QString type = c.value(QStringLiteral("type")).toString();
        const QJsonValue def = c.value(QStringLiteral("default"));
        const QJsonValue value = clip->mogrt.values.contains(id) ? clip->mogrt.values.value(id) : def;
        QVariantMap m{{QStringLiteral("id"), id},
                      {QStringLiteral("type"), type},
                      {QStringLiteral("label"), c.value(QStringLiteral("label")).toString()},
                      {QStringLiteral("value"), qmlValueFor(type, value)},
                      {QStringLiteral("default"), qmlValueFor(type, def)},
                      {QStringLiteral("changed"), clip->mogrt.values.contains(id)},
                      {QStringLiteral("editable"), editableType(type)}};
        if (c.contains(QStringLiteral("min")))
            m.insert(QStringLiteral("min"), c.value(QStringLiteral("min")).toDouble());
        if (c.contains(QStringLiteral("max")))
            m.insert(QStringLiteral("max"), c.value(QStringLiteral("max")).toDouble());
        controls.append(m);
    }

    return {{QStringLiteral("name"), clip->mogrt.name},
            {QStringLiteral("status"), status},
            {QStringLiteral("message"), message},
            {QStringLiteral("warnings"), clip->mogrt.warnings},
            {QStringLiteral("controls"), controls},
            {QStringLiteral("hasChanges"), !clip->mogrt.values.isEmpty()},
            {QStringLiteral("rendering"), rendering},
            {QStringLiteral("renderingQuality"), rendering ? renderingQuality : QString()},
            {QStringLiteral("progress"), rendering && renderer ? renderer->progress(renderingJob) : 0.0},
            {QStringLiteral("quality"), upToDate ? clip->mogrt.renderQuality : QString()},
            {QStringLiteral("hasRender"), QFileInfo::exists(clip->path)},
            {QStringLiteral("templateMissing"), templateMissing},
            {QStringLiteral("converterAvailable"), converterAvailable()},
            {QStringLiteral("authorApp"), clip->mogrt.authorApp},
            {QStringLiteral("size"), clip->mogrt.width > 0 ? QStringLiteral("%1 × %2").arg(clip->mogrt.width).arg(clip->mogrt.height)
                                                           : QString()}};
}

void MogrtTitleController::setValue(int track, int clipIndex, const QString &controlId, const QVariant &value)
{
    drift::Clip *clip = clipAt(track, clipIndex);
    if (!clip)
        return;
    QString type;
    QJsonValue def;
    for (const QJsonValue &v : clip->mogrt.controls) {
        const QJsonObject c = v.toObject();
        if (c.value(QStringLiteral("id")).toString() == controlId) {
            type = c.value(QStringLiteral("type")).toString();
            def = c.value(QStringLiteral("default"));
            break;
        }
    }
    if (type.isEmpty() || !editableType(type))
        return;
    const QJsonValue next = jsonValueFor(type, value);
    const QJsonValue now = clip->mogrt.values.contains(controlId) ? clip->mogrt.values.value(controlId) : def;
    if (next == now)
        return;
    const drift::Project before = m_app->m_project;
    clip = clipAt(track, clipIndex);
    // Back at the default, the value is dropped: the template's own default then applies.
    if (next == def)
        clip->mogrt.values.remove(controlId);
    else
        clip->mogrt.values.insert(controlId, next);
    m_app->pushProjectEdit(before, tr("Edit template title"));
    m_app->finishEdit(tr("Edit template title"));
}

void MogrtTitleController::resetValues(int track, int clipIndex)
{
    drift::Clip *clip = clipAt(track, clipIndex);
    if (!clip || clip->mogrt.values.isEmpty())
        return;
    const drift::Project before = m_app->m_project;
    clipAt(track, clipIndex)->mogrt.values = {};
    m_app->pushProjectEdit(before, tr("Reset template title"));
    m_app->finishEdit(tr("Reset template title"));
}

void MogrtTitleController::renderFullQuality(int track, int clipIndex)
{
    const drift::Clip *clip = clipAt(track, clipIndex);
    if (!clip)
        return;
    const QString key = renderKey(*clip);
    m_failed.remove(wantKey(key, QStringLiteral("full")));
    if (!QFileInfo::exists(renderPath(key, QStringLiteral("full"))))
        request(*clip, key, QStringLiteral("full"), 2);
    bump();
}

void MogrtTitleController::renderAgain(int track, int clipIndex)
{
    drift::Clip *clip = clipAt(track, clipIndex);
    if (!clip)
        return;
    const QString key = renderKey(*clip);
    for (const QString &quality : {QStringLiteral("preview"), QStringLiteral("full")}) {
        m_failed.remove(wantKey(key, quality));
        if (MogrtRenderer *renderer = MogrtRenderer::instance()) {
            const QString job = m_jobForWant.value(wantKey(key, quality));
            if (!job.isEmpty())
                renderer->cancel(job);
        }
        QFile::remove(renderPath(key, quality));
    }
    // The old file stays on screen until the new one is in.
    clip->mogrt.renderKey.clear();
    reconcile();
}

bool MogrtTitleController::locateTemplate(int track, int clipIndex, const QUrl &url)
{
    drift::Clip *clip = clipAt(track, clipIndex);
    if (!clip)
        return false;
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    if (contentId(path) != clip->mogrt.templateId) {
        m_app->setLastMessage(tr("That is a different template. Pick the .mogrt this title was made from."),
                              QStringLiteral("warning"));
        return false;
    }
    QString error;
    importToStore(path, &error);
    const QString id = clip->mogrt.templateId;
    m_failed.clear();
    // Every clip of that template finds it in the store now; the pass points them there.
    m_app->m_project.forEachTrackList([&](QList<drift::Track> &tracks) {
        for (drift::Track &t : tracks) {
            for (drift::Clip &c : t.clips) {
                if (c.mogrt.templateId == id)
                    c.mogrt.templatePath = m_templates.value(id).path;
            }
        }
    });
    reconcile();
    m_app->notifyTracksChanged();
    return true;
}

// ---- export ------------------------------------------------------------------------------------

void MogrtTitleController::finalizeForExport(std::function<void()> ready, std::function<void(const QString &)> failed)
{
    m_reconcileTimer->stop();
    reconcile();
    QSet<QString> wants;
    QStringList problems;
    QList<std::pair<drift::Clip, QString>> toRequest;
    m_app->m_project.forEachTrackList([&](QList<drift::Track> &tracks) {
        for (const drift::Track &track : tracks) {
            for (const drift::Clip &clip : track.clips) {
                if (clip.mogrt.isEmpty())
                    continue;
                const QString key = renderKey(clip);
                if (clip.mogrt.renderKey == key && clip.mogrt.renderQuality == QLatin1String("full")
                    && QFileInfo::exists(clip.path))
                    continue;
                if (resolveTemplatePath(clip).isEmpty() || !converterAvailable()) {
                    // What is on screen goes out; the user hears why it may not match.
                    if (!QFileInfo::exists(clip.path))
                        problems << clip.mogrt.name;
                    continue;
                }
                const QString want = wantKey(key, QStringLiteral("full"));
                m_failed.remove(want);
                if (!wants.contains(want)) {
                    wants.insert(want);
                    toRequest.append({clip, key});
                }
            }
        }
    });
    if (!problems.isEmpty()) {
        m_app->setLastMessage(tr("%n template title(s) have no render and will be missing from the export: %1", nullptr,
                                 int(problems.size()))
                                  .arg(problems.join(QStringLiteral(", "))),
                              QStringLiteral("warning"));
    }
    if (wants.isEmpty()) {
        ready();
        return;
    }
    m_finalizing = true;
    m_finalizeWants = wants;
    m_finalizeTotal = int(wants.size());
    m_finalizeReady = std::move(ready);
    m_finalizeFailed = std::move(failed);
    for (const auto &[clip, key] : std::as_const(toRequest))
        request(clip, key, QStringLiteral("full"), 3);
    m_app->setLastMessage(tr("Rendering %n template title(s) at full quality before the export…", nullptr,
                             m_finalizeTotal));
    bump();
    checkFinalize();
}

void MogrtTitleController::checkFinalize()
{
    if (!m_finalizing)
        return;
    for (auto it = m_finalizeWants.begin(); it != m_finalizeWants.end();) {
        const QString key = it->section(QLatin1Char('|'), 0, 0);
        if (QFileInfo::exists(renderPath(key, QStringLiteral("full"))))
            it = m_finalizeWants.erase(it);
        else
            ++it;
    }
    if (!m_finalizeWants.isEmpty()) {
        bump();
        return;
    }
    // Every clip now plays its full render; the export snapshots the project after this.
    m_reconcileTimer->stop();
    reconcile();
    const auto ready = std::move(m_finalizeReady);
    m_finalizing = false;
    m_finalizeReady = {};
    m_finalizeFailed = {};
    m_finalizeTotal = 0;
    bump();
    if (ready)
        ready();
}

void MogrtTitleController::cancelFinalize()
{
    if (!m_finalizing)
        return;
    if (MogrtRenderer *renderer = MogrtRenderer::instance()) {
        for (const QString &want : std::as_const(m_finalizeWants)) {
            const QString job = m_jobForWant.value(want);
            if (!job.isEmpty())
                renderer->cancel(job);
        }
    }
    m_finalizing = false;
    m_finalizeWants.clear();
    m_finalizeReady = {};
    m_finalizeFailed = {};
    m_finalizeTotal = 0;
    bump();
}

// ---- storage -----------------------------------------------------------------------------------

double MogrtTitleController::cacheMegabytes() const
{
    qint64 bytes = 0;
    QDirIterator it(storeRoot() + QStringLiteral("/cache"), QDir::Files);
    while (it.hasNext())
        bytes += QFileInfo(it.next()).size();
    return double(bytes) / (1024.0 * 1024.0);
}

int MogrtTitleController::clearUnusedRenders()
{
    QSet<QString> used;
    m_app->m_project.forEachTrackList([&](QList<drift::Track> &tracks) {
        for (const drift::Track &t : tracks) {
            for (const drift::Clip &c : t.clips) {
                if (!c.mogrt.isEmpty())
                    used.insert(QFileInfo(c.path).absoluteFilePath());
            }
        }
    });
    int removed = 0;
    QDirIterator it(storeRoot() + QStringLiteral("/cache"), QDir::Files);
    while (it.hasNext()) {
        const QString path = QFileInfo(it.next()).absoluteFilePath();
        if (used.contains(path) || path.contains(QLatin1String(".part-")))
            continue;
        if (QFile::remove(path))
            ++removed;
    }
    bump();
    return removed;
}
