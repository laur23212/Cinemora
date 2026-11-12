#pragma once

#include "core/animation/SpeedCurve.h"
#include "core/base/Time.h"

#include <QList>
#include <QString>

namespace drift {

struct Clip;

// Dafin: Filmora-style speed controls — uniform speed, speed ramping presets and freeze frames.

// The uniform speed the Speed tab and the timeline's speed strip offer, as in Filmora (0.01×–100×).
// Ramps keep the narrower kMinCurveSpeed..kMaxCurveSpeed of SpeedCurve.h.
inline constexpr double kMinUniformSpeed = 0.01;
inline constexpr double kMaxUniformSpeed = 100.0;

// Sound can only be time-stretched inside the curve range (ClipAudioRetimer clamps the tempo to
// it). A clip retimed past it — a 20× fast-forward, a 0.05× crawl, a freeze frame — keeps its
// picture and plays no sound, rather than letting the sound slide away from the picture.
bool clipAudioFollowsSpeed(const Clip &clip);

// A freeze frame is a clip holding one source frame: a 1 µs sliver of source stretched over the
// held length. Uniform speeds never go this low, so the rate alone tells a freeze frame apart, and
// a project needs no new field for it (older builds simply play it as the held frame it is).
inline constexpr double kFreezeSpeedCeiling = 0.001;
bool isFreezeFrameClip(const Clip &clip);

// Turns `clip` (a copy of the clip being frozen) into a freeze frame of the source moment
// `sourceUs` lasting `durationUs`: no ramp, no reverse, no fades or entrance/exit animations, and
// every keyframed property held at the value it has at the clip's start.
void makeFreezeFrame(Clip &clip, TimeUs sourceUs, TimeUs durationUs);
// Gives a freeze frame a new length, keeping its properties held for all of it.
void setFreezeFrameLength(Clip &clip, TimeUs durationUs);

// Speed ramping presets, as in Filmora's Speed › Speed Ramping (Montage, Hero, Bullet Time,
// Jumper, Flash in, Flash out). Each is a smooth curve over the clip; "none" is the plain clip.
struct SpeedRampPreset
{
    QString id;
    QString label;
    QList<SpeedPoint> points;
};
const QList<SpeedRampPreset> &speedRampPresets();
// The preset `id` as a curve; empty for "none" or an unknown id.
SpeedCurve speedRampPresetCurve(const QString &id);
// The preset whose curve `curve` is ("none" for no curve), or "custom" for any other ramp.
QString matchSpeedRampPreset(const SpeedCurve &curve);

// What the timeline's speed strip says: "Normal", "Fast", "Slow", "Ramp" or "Freeze", the kind
// colouring the strip.
QString speedKind(const Clip &clip);

} // namespace drift
