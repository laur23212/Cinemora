#include "core/edit/BeatDuckPlan.h"

#include <algorithm>
#include <cmath>

namespace drift::beatduck {

QList<BeatMarker> planBeatMarkers(const QList<double> &beats, int beatsPerBar, int firstDownbeat,
                                  const QList<OnsetIn> &onsets, double rangeStart, double rangeEnd,
                                  float minOnsetStrength, double minOnsetGap)
{
    QList<BeatMarker> out;
    if (rangeEnd <= rangeStart)
        return out;

    if (!beats.isEmpty()) {
        const int perBar = qMax(1, beatsPerBar);
        const int first = qBound(0, firstDownbeat, int(beats.size()) - 1);
        for (int i = 0; i < beats.size(); ++i) {
            const double t = beats.at(i);
            if (t < rangeStart || t >= rangeEnd)
                continue;
            BeatMarker m;
            m.seconds = t;
            // Beats before the first downbeat belong to a pickup bar (bar 0 is not shown).
            const int fromFirst = i - first;
            const int barIndex = fromFirst >= 0 ? fromFirst / perBar : -1;
            m.downbeat = fromFirst >= 0 && fromFirst % perBar == 0;
            m.bar = barIndex + 1;
            out.append(m);
        }
        return out;
    }

    // No steady tempo: strongest onsets first, each keeping its neighbours at a distance.
    QList<OnsetIn> strong;
    for (const OnsetIn &o : onsets) {
        if (o.strength >= minOnsetStrength && o.seconds >= rangeStart && o.seconds < rangeEnd)
            strong.append(o);
    }
    std::stable_sort(strong.begin(), strong.end(),
                     [](const OnsetIn &a, const OnsetIn &b) { return a.strength > b.strength; });
    QList<double> kept;
    for (const OnsetIn &o : std::as_const(strong)) {
        const bool crowded = std::any_of(kept.cbegin(), kept.cend(), [&](double k) {
            return std::abs(k - o.seconds) < minOnsetGap;
        });
        if (!crowded)
            kept.append(o.seconds);
    }
    std::sort(kept.begin(), kept.end());
    for (double t : std::as_const(kept))
        out.append(BeatMarker{t, false, 0});
    return out;
}

QString beatMarkerLabel(const BeatMarker &marker)
{
    if (marker.downbeat && marker.bar > 0)
        return beatMarkerPrefix() + QStringLiteral("Bar %1").arg(marker.bar);
    return beatMarkerPrefix() + QStringLiteral("Beat");
}

bool isBeatMarkerLabel(const QString &label)
{
    return label.startsWith(beatMarkerPrefix());
}

QList<Span> speechSpans(const QVector<float> &levelsDb, double hopSeconds, double startSeconds,
                        float floorDb, float aboveNoiseDb, double holdGap, double minSpeech)
{
    QList<Span> raw;
    if (levelsDb.isEmpty() || hopSeconds <= 0.0)
        return raw;

    QVector<float> sorted = levelsDb;
    std::sort(sorted.begin(), sorted.end());
    const float noise = sorted.at(qMin<qsizetype>(sorted.size() - 1, sorted.size() / 10));
    const float threshold = std::max(floorDb, noise + aboveNoiseDb);

    int runStart = -1;
    for (int i = 0; i <= levelsDb.size(); ++i) {
        const bool loud = i < levelsDb.size() && levelsDb.at(i) > threshold;
        if (loud && runStart < 0) {
            runStart = i;
        } else if (!loud && runStart >= 0) {
            raw.append({startSeconds + runStart * hopSeconds, startSeconds + i * hopSeconds});
            runStart = -1;
        }
    }

    // Bridge the pauses between words, then drop what is still too short to be speech.
    QList<Span> bridged;
    for (const Span &s : std::as_const(raw)) {
        if (!bridged.isEmpty() && s.start - bridged.last().end < holdGap)
            bridged.last().end = s.end;
        else
            bridged.append(s);
    }
    QList<Span> out;
    for (const Span &s : std::as_const(bridged)) {
        if (s.end - s.start >= minSpeech)
            out.append(s);
    }
    return out;
}

QList<Span> mergeSpans(QList<Span> spans, double mergeGap, double lo, double hi)
{
    std::sort(spans.begin(), spans.end(),
              [](const Span &a, const Span &b) { return a.start < b.start; });
    QList<Span> out;
    for (Span s : std::as_const(spans)) {
        s.start = qMax(s.start, lo);
        s.end = qMin(s.end, hi);
        if (s.end <= s.start)
            continue;
        if (!out.isEmpty() && s.start - out.last().end < mergeGap)
            out.last().end = qMax(out.last().end, s.end);
        else
            out.append(s);
    }
    return out;
}

QList<DuckKey> planDuckKeys(const QList<Span> &speech, double clipStart, double clipEnd,
                            const DuckSettings &settings)
{
    QList<DuckKey> keys;
    if (clipEnd <= clipStart)
        return keys;
    const double attack = qMax(0.0, settings.attack);
    const double release = qMax(0.0, settings.release);
    const QList<Span> dips = mergeSpans(speech, attack + release, clipStart, clipEnd);

    constexpr double kMinStep = 0.02; // two dots never share a time (as on the volume line)
    for (const Span &d : dips) {
        const double downAt = d.start - attack;
        const double upAt = d.end + release;
        if (downAt > clipStart + kMinStep)
            keys.append({downAt, false});
        keys.append({qMax(clipStart, d.start), true});
        if (d.end - d.start >= kMinStep)
            keys.append({qMin(clipEnd, d.end), true});
        if (upAt < clipEnd - kMinStep)
            keys.append({upAt, false});
    }
    return keys;
}

QList<Span> duckWindows(const QList<DuckKey> &keys)
{
    QList<Span> windows;
    // A dip is a run of keys starting at its first key; a rest key after a ducked one closes it.
    int i = 0;
    while (i < keys.size()) {
        const int begin = i;
        bool sawDucked = false;
        while (i < keys.size()) {
            if (keys.at(i).ducked) {
                sawDucked = true;
            } else if (sawDucked) {
                ++i; // the release key belongs to this dip
                break;
            }
            ++i;
        }
        windows.append({keys.at(begin).seconds, keys.at(i - 1).seconds});
    }
    return windows;
}

} // namespace drift::beatduck
