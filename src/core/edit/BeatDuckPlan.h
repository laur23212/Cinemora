#pragma once

// Dafin: the planning half of the timeline toolbar's Beat markers and Auto ducking buttons.
// Pure arithmetic over seconds and levels, so it is tested without audio or a project; the
// controller (AppControllerBeatsDucking.cpp) mixes the sound, calls these and writes the result.

#include <QList>
#include <QString>
#include <QVector>

namespace drift::beatduck {

// ---- Beat markers ---------------------------------------------------------------------------

struct BeatMarker
{
    double seconds = 0.0; // timeline seconds
    bool downbeat = false; // first beat of a bar
    int bar = 0;           // 1-based bar number; 0 for an onset marker (no steady tempo)
};

struct OnsetIn
{
    double seconds = 0.0;
    float strength = 0.0f; // 0..1
};

// Markers for [rangeStart, rangeEnd). With a beat grid (`beats` non-empty) every beat inside
// the range, downbeats flagged from `firstDownbeat` / `beatsPerBar`. Without one (no steady
// tempo) the strong onsets instead, at least `minOnsetGap` apart, strongest kept first.
QList<BeatMarker> planBeatMarkers(const QList<double> &beats, int beatsPerBar, int firstDownbeat,
                                  const QList<OnsetIn> &onsets, double rangeStart, double rangeEnd,
                                  float minOnsetStrength = 0.35f, double minOnsetGap = 0.25);

// Bookmarks the button made carry this prefix, so a second press finds and removes them and
// never touches the user's own bookmarks. Not translated: it is stored in the project.
inline const QString &beatMarkerPrefix()
{
    static const QString prefix = QStringLiteral("♪ ");
    return prefix;
}
QString beatMarkerLabel(const BeatMarker &marker);
bool isBeatMarkerLabel(const QString &label);

// ---- Auto ducking ---------------------------------------------------------------------------

struct Span
{
    double start = 0.0;
    double end = 0.0;
};

// Speech from a loudness envelope: `levelsDb[i]` is the level of the window starting at
// `startSeconds + i * hopSeconds`. A window is speech when it is louder than both
// `floorDb` (absolute) and the quietest tenth of the envelope + `aboveNoiseDb`. Gaps shorter
// than `holdGap` are bridged (pauses between words) and spans shorter than `minSpeech` dropped
// (clicks, breaths).
QList<Span> speechSpans(const QVector<float> &levelsDb, double hopSeconds, double startSeconds,
                        float floorDb = -42.0f, float aboveNoiseDb = 10.0f, double holdGap = 0.35,
                        double minSpeech = 0.15);

// Sorts, merges overlapping spans and spans closer than `mergeGap`, and clips them to [lo, hi].
QList<Span> mergeSpans(QList<Span> spans, double mergeGap, double lo, double hi);

struct DuckSettings
{
    double amount = 0.25;  // ducked level as a share of the music's own level (0.25 ≈ -12 dB)
    double attack = 0.25;  // seconds the music takes to go down before speech
    double release = 0.5;  // seconds it takes to come back after
};

struct DuckKey
{
    double seconds = 0.0; // timeline seconds
    bool ducked = false;  // at the ducked level (else at the music's own level there)
};

// Four dots per dip: rest before the attack, ducked at the start and end of the speech, rest
// after the release. Dips closer than attack + release are merged first, so the music stays
// down between sentences instead of pumping. Keys outside [clipStart, clipEnd] are clamped to
// it; a dip whose ramp would run past a clip edge starts or ends ducked there.
QList<DuckKey> planDuckKeys(const QList<Span> &speech, double clipStart, double clipEnd,
                            const DuckSettings &settings);

// The windows a ducking pass owns: [first key, last key] of each dip. A re-run first clears
// the music's volume dots inside these, so pressing twice leaves the same line as once.
QList<Span> duckWindows(const QList<DuckKey> &keys);

} // namespace drift::beatduck
