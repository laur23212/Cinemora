#pragma once

#include "core/animation/Keyframe.h"
#include "core/base/Time.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QVariant>

namespace drift {

// Per-clip libavfilter effect with named parameters.
struct Effect
{
    QString name;
    QMap<QString, QVariant> parameters;

    // Params with a non-empty track are animated: the track wins over `parameters` at
    // render time. `parameters` still holds the last static value, so clearing a track
    // returns the param to a constant rather than to the catalog default.
    QMap<QString, KeyframeTrack<double>> paramKeyframes;

    // Id into the effect preset catalog (src/engine/packages/effects/EffectCatalog.h) this effect was
    // created from; UI-only bookkeeping so the inspector can show the right
    // label/sliders. Rendering uses catalog metadata in the engine layer.
    QString catalogId;

    // When false the effect stays on the clip (params/keyframes preserved) but is
    // skipped at render/mix time. Default true so older projects load as enabled.
    bool enabled = true;

    // Dafin: set when a title template added this effect — its key inside the template
    // ("sparks"). Template bindings address the effect by it ("effect.sparks.intensity"), and
    // applying another template replaces only effects that carry a key, never the user's own.
    QString templateKey;

    QString filterGraphString() const;

    // parameters[key], overridden by paramKeyframes[key] evaluated at clipTimeUs.
    QVariant valueAt(const QString &key, TimeUs clipTimeUs) const;

    // A copy with every animated param baked down to its value at clipTimeUs.
    Effect resolvedAt(TimeUs clipTimeUs) const;
};

QJsonObject keyframesToJson(const KeyframeTrack<double> &track);
// Also migrates the pre-tangent "interpolation" field older projects carry.
KeyframeTrack<double> keyframesFromJson(const QJsonObject &object);

QJsonArray effectsToJson(const QList<Effect> &effects);
QList<Effect> effectsFromJson(const QJsonArray &array);

// Moves every keyframe on `effects` off a clip of `fromDurationUs` and onto one of
// `toDurationUs`, keeping each key at the same fraction of the clip. This is what lets a stack
// copied from a two-second shot read the same way on a six-second one.
void rescaleEffectKeyframes(QList<Effect> &effects, TimeUs fromDurationUs, TimeUs toDurationUs);

} // namespace drift
