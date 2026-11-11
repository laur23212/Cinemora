#pragma once

#include <QList>
#include <QPair>
#include <QString>

#include <functional>
#include <memory>
#include <vector>

namespace drift {

// Runs interleaved float audio through an FFmpeg (libavfilter) filter chain, such as
// "afftdn=nr=12" or "loudnorm=I=-14". The chain is written the way `ffmpeg -af` takes it; the
// output is converted back to the input's rate and channel count, so what comes out lines up
// sample for sample with what went in (loudnorm, which works at 192 kHz, is resampled back).
//
// Frame metadata the filters attach (silencedetect's lavfi.silence_start / _end, ebur128's
// lavfi.r128.*) is handed to `onMetadata` with the frame's time in seconds from the first sample.
//
// Not thread-safe; one instance per job. Blocking, so run it off the GUI thread.
class FfmpegAudioFilter
{
public:
    using Metadata = QList<QPair<QString, QString>>;
    using MetadataCallback = std::function<void(double seconds, const Metadata &)>;

    FfmpegAudioFilter();
    ~FfmpegAudioFilter();

    bool open(int sampleRate, int channels, const QString &chain, QString *error);

    // Appends `frames` frames to the graph and collects whatever it has ready into `out`.
    bool push(const float *interleaved, int frames, std::vector<float> &out, QString *error);
    // Signals the end of the input and drains the graph into `out`.
    bool finish(std::vector<float> &out, QString *error);

    void setMetadataCallback(MetadataCallback cb) { m_onMetadata = std::move(cb); }

    // True when this FFmpeg build has a filter with that name (loudnorm, afftdn, silencedetect…).
    static bool hasFilter(const char *name);

    FfmpegAudioFilter(const FfmpegAudioFilter &) = delete;
    FfmpegAudioFilter &operator=(const FfmpegAudioFilter &) = delete;

private:
    bool drain(std::vector<float> &out, QString *error);

    struct Impl;
    std::unique_ptr<Impl> d;
    MetadataCallback m_onMetadata;
};

// Pulls input from `feed` (fills up to maxFrames interleaved frames; returns how many, 0 at the end,
// -1 to cancel) and hands the filtered audio to `sink` (return false to cancel) block by block, so
// an hour-long clip never sits in memory. Exactly as many frames come out as went in.
using AudioFeed = std::function<int(float *interleaved, int maxFrames)>;
using AudioSink = std::function<bool(const float *interleaved, int frames)>;
bool streamAudioFilter(int sampleRate, int channels, const QString &chain, const AudioFeed &feed,
                       const AudioSink &sink, QString *error,
                       const FfmpegAudioFilter::MetadataCallback &onMetadata = {});

// A feed over an in-memory buffer.
AudioFeed bufferFeed(const std::vector<float> &interleaved, int channels);

// Convenience: the whole buffer through `chain` in one go. Empty result plus `error` on failure.
std::vector<float> runAudioFilter(const std::vector<float> &interleaved, int sampleRate, int channels,
                                  const QString &chain, QString *error,
                                  const FfmpegAudioFilter::MetadataCallback &onMetadata = {});

// ---- The three Dafin audio tools, as filter chains ----------------------------------------

// What ebur128 measured over a whole buffer: the inputs loudnorm's second (linear) pass needs.
struct LoudnessStats
{
    bool ok = false;
    double integratedLufs = -70.0;
    double rangeLu = 0.0;
    double truePeakDb = -70.0;
    double thresholdLufs = -80.0;
};
LoudnessStats measureEbuR128(const AudioFeed &feed, int sampleRate, int channels, QString *error);
LoudnessStats measureEbuR128(const std::vector<float> &interleaved, int sampleRate, int channels,
                             QString *error);

// Two-pass loudnorm: the measurement above, then loudnorm in linear mode, so the whole clip gets one
// gain (no pumping). loudnorm itself falls back to its dynamic mode when one gain can't reach the
// target without passing the true-peak ceiling.
QString loudnormChain(double targetLufs, double truePeakDb, double rangeLu, const LoudnessStats &measured);

// afftdn with a 0-1 strength: noise reduction 6-40 dB, the noise floor tracked through the clip.
QString afftdnChain(double strength);

struct SilenceSpanSeconds
{
    double start = 0.0;
    double end = 0.0;
};
// silencedetect over a buffer: the stretches below `thresholdDb` for at least `minSeconds`, in
// seconds from the first sample. A silence still open at the end closes at the end.
QList<SilenceSpanSeconds> detectSilenceFfmpeg(const AudioFeed &feed, int sampleRate, int channels,
                                              double thresholdDb, double minSeconds, QString *error);
QList<SilenceSpanSeconds> detectSilenceFfmpeg(const std::vector<float> &interleaved, int sampleRate,
                                              int channels, double thresholdDb, double minSeconds,
                                              QString *error);

} // namespace drift
