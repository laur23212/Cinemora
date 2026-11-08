#include "app/online/library/MogrtInfo.h"

#include "core/base/ZipArchive.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

#include <algorithm>

namespace dafin::assets::mogrt {

namespace {

constexpr double kPremiereTicksPerSecond = 254016000000.0;
constexpr int kMaxTextFields = 8;

// Adobe stores display strings as {"strDB": [{"localeString": "en_US", "str": "…"}, …]}; some
// fields are plain strings or {"en_US": "…"}. English first, then whatever is there.
QString localized(const QJsonValue &value)
{
    if (value.isString())
        return value.toString();
    if (!value.isObject())
        return {};
    const QJsonObject o = value.toObject();
    if (o.value(QStringLiteral("strDB")).isArray()) {
        QString first;
        for (const QJsonValue &v : o.value(QStringLiteral("strDB")).toArray()) {
            const QJsonObject entry = v.toObject();
            const QString str = entry.value(QStringLiteral("str")).toString();
            if (str.isEmpty())
                continue;
            if (entry.value(QStringLiteral("localeString")).toString().startsWith(QLatin1String("en")))
                return str;
            if (first.isEmpty())
                first = str;
        }
        return first;
    }
    for (const char *locale : {"en_US", "en_GB", "en"}) {
        const QJsonValue v = o.value(QLatin1String(locale));
        if (v.isString())
            return v.toString();
    }
    return {};
}

// The first array called `name` at most a few levels down: the controls list sits at the root in
// After Effects templates and one level deeper in some Premiere ones.
QJsonArray findArray(const QJsonObject &root, const QString &name, int depth = 0)
{
    if (root.value(name).isArray())
        return root.value(name).toArray();
    if (depth >= 3)
        return {};
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        if (it.value().isObject()) {
            const QJsonArray found = findArray(it.value().toObject(), name, depth + 1);
            if (!found.isEmpty())
                return found;
        }
    }
    return {};
}

bool looksLikeFileOrColour(const QString &text)
{
    static const QRegularExpression colour(QStringLiteral("^#?[0-9a-fA-F]{6,8}$"));
    static const QRegularExpression file(QStringLiteral("[/\\\\].*\\.[A-Za-z0-9]{2,4}$"));
    return colour.match(text).hasMatch() || file.match(text).hasMatch();
}

// A control's default text, or empty when the control is not a text field.
QString textOf(const QJsonObject &control)
{
    const auto plain = [](const QJsonValue &v) { return v.isString() ? v.toString() : QString(); };
    QString text = plain(control.value(QStringLiteral("textEditValue")));
    bool textual = !text.isEmpty();
    const QJsonValue value = control.value(QStringLiteral("value"));
    if (text.isEmpty() && value.isObject()) {
        const QJsonObject v = value.toObject();
        text = plain(v.value(QStringLiteral("textEditValue")));
        if (text.isEmpty())
            text = plain(v.value(QStringLiteral("text")));
        textual = !text.isEmpty();
        if (text.isEmpty() && v.contains(QStringLiteral("strDB")))
            text = localized(v);
    }
    if (text.isEmpty() && value.isString())
        text = value.toString();

    // Without an explicit text value, only a control typed as text counts: sliders, colours and
    // checkboxes carry strings too (names, hex values).
    if (!textual) {
        const QJsonValue type = control.value(QStringLiteral("type"));
        const QString typeName = type.isString() ? type.toString().toLower() : QString();
        const bool textType = typeName == QLatin1String("text") || typeName == QLatin1String("string")
                              || typeName == QLatin1String("sourcetext") || (type.isDouble() && type.toInt() == 6);
        if (!textType)
            return {};
    }
    text = text.trimmed();
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    if (text.size() > 500 || looksLikeFileOrColour(text))
        return {};
    return text;
}

double secondsOf(const QJsonValue &value, double fps)
{
    if (value.isDouble()) {
        const double n = value.toDouble();
        if (n <= 0)
            return 0;
        if (n >= 1e9)
            return n / kPremiereTicksPerSecond;
        if (n <= 600)
            return n;
        return fps > 0 ? n / fps : 0; // a frame count
    }
    if (value.isString()) {
        bool ok = false;
        const double n = value.toString().toDouble(&ok);
        return ok ? secondsOf(QJsonValue(n), fps) : 0;
    }
    if (value.isObject()) {
        const QJsonObject o = value.toObject();
        if (o.contains(QStringLiteral("ticks")))
            return secondsOf(o.value(QStringLiteral("ticks")), fps);
        if (o.contains(QStringLiteral("seconds")))
            return o.value(QStringLiteral("seconds")).toDouble();
        const double scale = o.value(QStringLiteral("scale")).toDouble(o.value(QStringLiteral("timescale")).toDouble());
        if (scale > 0)
            return o.value(QStringLiteral("value")).toDouble() / scale;
    }
    return 0;
}

// Duration and frame size: in the root or in a per-locale "sourceInfoLocalized" block.
void readTiming(const QJsonObject &root, Info &info)
{
    QList<QJsonObject> places{root};
    const QJsonObject source = root.value(QStringLiteral("sourceInfoLocalized")).toObject();
    for (auto it = source.constBegin(); it != source.constEnd(); ++it) {
        if (it.value().isObject())
            places.append(it.value().toObject());
    }
    for (const QJsonObject &o : std::as_const(places)) {
        double fps = 0;
        for (const char *key : {"frameRate", "framerate", "fps"}) {
            if (o.value(QLatin1String(key)).isDouble())
                fps = o.value(QLatin1String(key)).toDouble();
        }
        if (info.durationSeconds <= 0) {
            for (const char *key : {"durationSeconds", "duration", "durationTime", "compDuration"}) {
                const double s = secondsOf(o.value(QLatin1String(key)), fps);
                if (s > 0) {
                    info.durationSeconds = s;
                    break;
                }
            }
            if (info.durationSeconds <= 0 && fps > 0 && o.value(QStringLiteral("durationFrames")).isDouble())
                info.durationSeconds = o.value(QStringLiteral("durationFrames")).toDouble() / fps;
        }
        if (info.width <= 0 && o.value(QStringLiteral("width")).isDouble()) {
            info.width = o.value(QStringLiteral("width")).toInt();
            info.height = o.value(QStringLiteral("height")).toInt();
        }
    }
    if (info.durationSeconds > 0)
        info.durationSeconds = std::clamp(info.durationSeconds, 0.5, 300.0);
}

void readDefinition(const QByteArray &json, Info &info)
{
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    if (root.isEmpty())
        return;

    QString title = localized(root.value(QStringLiteral("capsuleNameLocalized")));
    if (title.isEmpty())
        title = localized(root.value(QStringLiteral("templateNameLocalized")));
    if (title.isEmpty()) {
        const QJsonObject source = root.value(QStringLiteral("sourceInfoLocalized")).toObject();
        for (const char *locale : {"en_US", "en_GB", "en"}) {
            title = source.value(QLatin1String(locale)).toObject().value(QStringLiteral("name")).toString();
            if (!title.isEmpty())
                break;
        }
        if (title.isEmpty() && !source.isEmpty())
            title = source.constBegin().value().toObject().value(QStringLiteral("name")).toString();
    }
    if (title.isEmpty())
        title = root.value(QStringLiteral("name")).toString(root.value(QStringLiteral("title")).toString());
    if (!title.trimmed().isEmpty())
        info.title = title.trimmed();

    info.authorApp = root.value(QStringLiteral("authorApp")).toString();

    QJsonArray controls = findArray(root, QStringLiteral("clientControls"));
    if (controls.isEmpty())
        controls = findArray(root, QStringLiteral("controls"));
    if (controls.isEmpty())
        controls = findArray(root, QStringLiteral("properties"));
    QSet<QString> seen;
    for (const QJsonValue &v : std::as_const(controls)) {
        if (info.texts.size() >= kMaxTextFields)
            break;
        const QJsonObject control = v.toObject();
        const QString text = textOf(control);
        if (text.isEmpty())
            continue;
        QString label = localized(control.value(QStringLiteral("uiName")));
        if (label.isEmpty())
            label = control.value(QStringLiteral("name")).toString(control.value(QStringLiteral("displayName")).toString());
        const QString key = label + QLatin1Char('\x1f') + text;
        if (seen.contains(key))
            continue;
        seen.insert(key);
        const QJsonValue idValue = control.value(QStringLiteral("id"));
        const QString id = idValue.isString() ? idValue.toString()
                           : idValue.isDouble() ? QString::number(idValue.toInteger()) : QString();
        info.texts.append({id, label.trimmed(), text});
    }
    readTiming(root, info);
}

bool isImage(const QString &suffix)
{
    return suffix == QLatin1String("png") || suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg")
           || suffix == QLatin1String("gif") || suffix == QLatin1String("webp");
}

bool isVideo(const QString &suffix)
{
    return suffix == QLatin1String("mp4") || suffix == QLatin1String("mov") || suffix == QLatin1String("m4v")
           || suffix == QLatin1String("webm");
}

} // namespace

std::optional<Info> read(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QObject::tr("Can't open %1.").arg(QFileInfo(path).fileName());
        return std::nullopt;
    }
    QString zipError;
    const QList<drift::zip::Entry> entries = drift::zip::readEntries(file, &zipError);
    const drift::zip::Entry *definition = nullptr;
    for (const drift::zip::Entry &e : entries) {
        if (!e.isDir && QFileInfo(e.path).fileName().compare(QLatin1String("definition.json"), Qt::CaseInsensitive) == 0
            && (!definition || e.path.count(QLatin1Char('/')) < definition->path.count(QLatin1Char('/'))))
            definition = &e;
    }
    if (!definition) {
        if (error)
            *error = QObject::tr("%1 is not a Motion Graphics Template.").arg(QFileInfo(path).fileName());
        return std::nullopt;
    }

    Info info;
    info.title = QFileInfo(path).completeBaseName();
    if (definition->uncompressedSize <= 32u * 1024 * 1024) {
        QByteArray json;
        if (drift::zip::extractEntry(file, *definition, json))
            readDefinition(json, info);
    }

    // Poster: thumb.* (what After Effects writes), else the shallowest image. Preview: a video,
    // preferring one named like a preview. Nothing inside the nested project archives is visible
    // here, so footage the animation uses is not mistaken for either.
    int posterRank = 1 << 20;
    int previewRank = 1 << 20;
    for (const drift::zip::Entry &e : entries) {
        if (e.isDir)
            continue;
        const QString name = QFileInfo(e.path).fileName().toLower();
        const QString suffix = QFileInfo(name).suffix();
        const int depth = int(e.path.count(QLatin1Char('/')));
        if (isImage(suffix)) {
            int rank = depth * 10 + 5;
            if (name.startsWith(QLatin1String("thumb")))
                rank = depth * 10;
            else if (name.contains(QLatin1String("poster")) || name.contains(QLatin1String("preview")))
                rank = depth * 10 + 1;
            if (rank < posterRank) {
                posterRank = rank;
                info.posterEntry = e.path;
            }
        } else if (isVideo(suffix) && e.uncompressedSize > 0 && e.uncompressedSize <= 200u * 1024 * 1024) {
            const int rank = depth * 10 + (name.contains(QLatin1String("preview")) ? 0 : 5);
            if (rank < previewRank) {
                previewRank = rank;
                info.previewEntry = e.path;
            }
        }
    }
    return info;
}

bool extract(const QString &archivePath, const QString &entryPath, const QString &destPath, qint64 maxBytes)
{
    QFile file(archivePath);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    for (const drift::zip::Entry &e : drift::zip::readEntries(file, nullptr)) {
        if (e.path != entryPath || e.isDir)
            continue;
        if (qint64(e.uncompressedSize) > maxBytes)
            return false;
        QByteArray data;
        if (!drift::zip::extractEntry(file, e, data) || data.isEmpty())
            return false;
        QSaveFile out(destPath);
        if (!out.open(QIODevice::WriteOnly))
            return false;
        out.write(data);
        return out.commit();
    }
    return false;
}

QString titleText(const Info &info)
{
    QStringList lines;
    for (const TextField &f : info.texts)
        lines.append(f.text);
    const QString text = lines.join(QLatin1Char('\n')).trimmed();
    return text.isEmpty() ? info.title : text;
}

QString titlePreset(const Info &info)
{
    QString haystack = info.title;
    for (const TextField &f : info.texts)
        haystack += QLatin1Char(' ') + f.label;
    haystack = haystack.toLower();
    haystack.remove(QLatin1Char('-'));
    haystack.remove(QLatin1Char('_'));
    haystack.remove(QLatin1Char(' '));
    if (haystack.contains(QLatin1String("lowerthird")) || haystack.contains(QLatin1String("namestrap"))
        || haystack.contains(QLatin1String("nametag")))
        return QStringLiteral("lower-third");
    return QStringLiteral("title");
}

} // namespace dafin::assets::mogrt
