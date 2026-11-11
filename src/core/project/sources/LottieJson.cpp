#include "core/project/sources/LottieJson.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtGlobal>

namespace drift {

namespace {

// A number Lottie writers sometimes put in a string ("fr": "30").
double lottieNumber(const QJsonValue &value, double fallback)
{
    if (value.isDouble())
        return value.toDouble();
    if (value.isString()) {
        bool ok = false;
        const double parsed = value.toString().trimmed().toDouble(&ok);
        if (ok)
            return parsed;
    }
    return fallback;
}

bool looksLikeLottieRoot(const QJsonObject &object)
{
    return object.value(QStringLiteral("layers")).isArray();
}

} // namespace

QByteArray normalizeLottieJson(const QByteArray &data, bool rewrite)
{
    QByteArray bytes = data;
    bool changed = false;
    if (bytes.startsWith("\xEF\xBB\xBF")) {
        bytes.remove(0, 3);
        changed = true;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(bytes);
    if (!doc.isObject())
        return changed ? bytes : data;
    QJsonObject root = doc.object();

    if (!looksLikeLottieRoot(root)) {
        // Players and sites that hand the animation out inside an object of their own.
        static const char *const kWrappers[] = {"animationData", "animation", "lottie", "data",
                                                "json",          "content",   "src"};
        for (const char *key : kWrappers) {
            QJsonValue inner = root.value(QLatin1String(key));
            if (inner.isString())
                inner = QJsonDocument::fromJson(inner.toString().toUtf8()).object();
            if (inner.isObject() && looksLikeLottieRoot(inner.toObject())) {
                root = inner.toObject();
                changed = true;
                break;
            }
        }
        if (!looksLikeLottieRoot(root))
            return changed ? bytes : data;
    }

    // Skottie's own checks (Animation::Builder::make): a version string, a positive frame rate,
    // an out-point after the in-point and a non-empty size, or no animation at all.
    const QJsonValue version = root.value(QStringLiteral("v"));
    if (!version.isString() || version.toString().isEmpty()) {
        root.insert(QStringLiteral("v"), version.isDouble() ? QString::number(version.toDouble())
                                                            : QStringLiteral("5.7.4"));
        changed = true;
    }
    double fr = lottieNumber(root.value(QStringLiteral("fr")), 0.0);
    if (!(fr > 0.0) || !root.value(QStringLiteral("fr")).isDouble()) {
        fr = fr > 0.0 ? fr : 30.0;
        root.insert(QStringLiteral("fr"), fr);
        changed = true;
    }
    const double ip = lottieNumber(root.value(QStringLiteral("ip")), 0.0);
    if (!root.value(QStringLiteral("ip")).isDouble()) {
        root.insert(QStringLiteral("ip"), ip);
        changed = true;
    }
    double op = lottieNumber(root.value(QStringLiteral("op")), 0.0);
    if (!(op > ip) || !root.value(QStringLiteral("op")).isDouble()) {
        if (!(op > ip)) {
            // The latest layer end, else three seconds.
            op = ip;
            const QJsonArray layers = root.value(QStringLiteral("layers")).toArray();
            for (const QJsonValue &layer : layers)
                op = qMax(op, lottieNumber(layer.toObject().value(QStringLiteral("op")), 0.0));
            if (!(op > ip))
                op = ip + 3.0 * fr;
        }
        root.insert(QStringLiteral("op"), op);
        changed = true;
    }
    double w = lottieNumber(root.value(QStringLiteral("w")), 0.0);
    double h = lottieNumber(root.value(QStringLiteral("h")), 0.0);
    if (!(w > 0.0) || !(h > 0.0) || !root.value(QStringLiteral("w")).isDouble()
        || !root.value(QStringLiteral("h")).isDouble()) {
        if (!(w > 0.0) || !(h > 0.0)) {
            // A precomp or solid layer says how big the author's canvas was; else a square.
            w = h = 0.0;
            const QJsonArray layers = root.value(QStringLiteral("layers")).toArray();
            for (const QJsonValue &layer : layers) {
                const QJsonObject o = layer.toObject();
                w = qMax(w, lottieNumber(o.value(QStringLiteral("w")), lottieNumber(o.value(QStringLiteral("sw")), 0.0)));
                h = qMax(h, lottieNumber(o.value(QStringLiteral("h")), lottieNumber(o.value(QStringLiteral("sh")), 0.0)));
            }
            if (!(w > 0.0) || !(h > 0.0))
                w = h = 512.0;
        }
        root.insert(QStringLiteral("w"), w);
        root.insert(QStringLiteral("h"), h);
        changed = true;
    }
    if (!changed && !rewrite)
        return data;
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

} // namespace drift
