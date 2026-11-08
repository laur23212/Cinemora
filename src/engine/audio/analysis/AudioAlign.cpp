#include "engine/audio/analysis/AudioAlign.h"

extern "C" {
#include <libavutil/mem.h>
#include <libavutil/tx.h>
}

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace drift {

QList<SilenceSpan> findSilence(const float *mono, qint64 frames, int sampleRate,
                               const SilenceParams &params)
{
    QList<SilenceSpan> out;
    const qint64 win = std::max<qint64>(1, qint64(params.windowSeconds * sampleRate));
    const qint64 count = frames / win;
    if (count <= 0)
        return out;

    const double thresholdSq = double(params.threshold) * double(params.threshold);
    const double winSeconds = double(win) / sampleRate;
    qint64 run = -1;
    for (qint64 w = 0; w <= count; ++w) {
        bool quiet = false;
        if (w < count) {
            double sum = 0.0;
            for (qint64 i = w * win; i < (w + 1) * win; ++i)
                sum += double(mono[i]) * double(mono[i]);
            quiet = sum / double(win) < thresholdSq;
        }
        if (quiet && run < 0)
            run = w;
        if (!quiet && run >= 0) {
            const double start = run * winSeconds;
            const double end = w * winSeconds;
            if (end - start >= params.minSilence) {
                SilenceSpan span;
                span.start = run == 0 ? start : start + params.padding;
                span.end = w == count ? end : end - params.padding;
                if (span.end > span.start)
                    out.append(span);
            }
            run = -1;
        }
    }
    return out;
}

namespace {

double rmsOf(const std::vector<float> &x)
{
    if (x.empty())
        return 0.0;
    double sum = 0.0;
    for (const float v : x)
        sum += double(v) * double(v);
    return std::sqrt(sum / double(x.size()));
}

struct AlignedBuffer
{
    explicit AlignedBuffer(size_t floats) : data(static_cast<float *>(av_mallocz(floats * sizeof(float)))) {}
    ~AlignedBuffer() { av_free(data); }
    AlignedBuffer(const AlignedBuffer &) = delete;
    AlignedBuffer &operator=(const AlignedBuffer &) = delete;
    float *data = nullptr;
};

} // namespace

AudioOffsetResult findAudioOffset(const std::vector<float> &anchorIn, const std::vector<float> &otherIn,
                                  int sampleRate, const AudioOffsetParams &params,
                                  const std::function<bool()> &keepGoing)
{
    AudioOffsetResult result;
    const qint64 na = qint64(anchorIn.size());
    const qint64 nb = qint64(otherIn.size());
    const qint64 minOverlap = qint64(params.minOverlapSeconds * sampleRate);
    if (na < minOverlap || nb < minOverlap) {
        result.status = AudioOffsetResult::Status::TooShort;
        return result;
    }
    if (rmsOf(anchorIn) < params.silentRms || rmsOf(otherIn) < params.silentRms) {
        result.status = AudioOffsetResult::Status::Silent;
        return result;
    }

    qint64 size = 1;
    while (size < na + nb)
        size <<= 1;

    // Spectra hold size/2 + 1 complex bins; the spare floats keep a packed Nyquist from overrunning.
    const size_t specFloats = size_t(size) + 4;
    AlignedBuffer timeA(size), timeB(size), specA(specFloats), specB(specFloats), corr(size);
    if (!timeA.data || !timeB.data || !specA.data || !specB.data || !corr.data)
        return result;

    auto removeMean = [](const std::vector<float> &x, float *dst) {
        double mean = 0.0;
        for (const float v : x)
            mean += v;
        mean /= double(x.size());
        for (size_t i = 0; i < x.size(); ++i)
            dst[i] = float(double(x[i]) - mean);
    };
    removeMean(anchorIn, timeA.data);
    removeMean(otherIn, timeB.data);

    AVTXContext *fwd = nullptr;
    AVTXContext *inv = nullptr;
    av_tx_fn fwdFn = nullptr;
    av_tx_fn invFn = nullptr;
    float fwdScale = 1.0f;
    float invScale = 1.0f / float(size);
    const bool ready = av_tx_init(&fwd, &fwdFn, AV_TX_FLOAT_RDFT, 0, int(size), &fwdScale, 0) >= 0
                       && av_tx_init(&inv, &invFn, AV_TX_FLOAT_RDFT, 1, int(size), &invScale, 0) >= 0;
    auto release = [&]() {
        if (fwd)
            av_tx_uninit(&fwd);
        if (inv)
            av_tx_uninit(&inv);
    };
    if (!ready) {
        release();
        return result;
    }

    fwdFn(fwd, specA.data, timeA.data, sizeof(float));
    fwdFn(fwd, specB.data, timeB.data, sizeof(float));
    if (keepGoing && !keepGoing()) {
        release();
        return result;
    }
    const qint64 bins = size / 2 + 1;
    for (qint64 i = 0; i < bins; ++i) {
        const float ar = specA.data[2 * i], ai = specA.data[2 * i + 1];
        const float br = specB.data[2 * i], bi = specB.data[2 * i + 1];
        specA.data[2 * i] = ar * br + ai * bi;
        specA.data[2 * i + 1] = ai * br - ar * bi;
    }
    invFn(inv, corr.data, specA.data, sizeof(float));
    release();
    if (keepGoing && !keepGoing()) {
        return result;
    }

    // corr[k mod size] = sum_n anchor[n + k] * other[n]. Dividing by the energy of the two
    // overlapping stretches turns it into a correlation coefficient, so a short overlap at the
    // edge of the search range can't win just by being quiet or loud.
    std::vector<double> sumA(size_t(na) + 1, 0.0), sumB(size_t(nb) + 1, 0.0);
    for (qint64 i = 0; i < na; ++i)
        sumA[size_t(i) + 1] = sumA[size_t(i)] + double(timeA.data[i]) * double(timeA.data[i]);
    for (qint64 i = 0; i < nb; ++i)
        sumB[size_t(i) + 1] = sumB[size_t(i)] + double(timeB.data[i]) * double(timeB.data[i]);

    const qint64 maxLag = qint64(params.maxLagSeconds * sampleRate);
    const qint64 guard = std::max<qint64>(1, qint64(0.1 * sampleRate));
    std::vector<float> ncc(size_t(2 * maxLag + 1), 0.0f);
    qint64 best = 0;
    float bestValue = 0.0f;
    bool any = false;
    for (qint64 k = -maxLag; k <= maxLag; ++k) {
        const qint64 n0 = std::max<qint64>(0, -k);
        const qint64 n1 = std::min(nb, na - k);
        if (n1 - n0 < minOverlap)
            continue;
        const double ea = sumA[size_t(n1 + k)] - sumA[size_t(n0 + k)];
        const double eb = sumB[size_t(n1)] - sumB[size_t(n0)];
        const double denom = std::sqrt(ea * eb);
        if (denom <= 0.0)
            continue;
        const float value = float(double(corr.data[(k + size) % size]) / denom);
        ncc[size_t(k + maxLag)] = value;
        if (!any || value > bestValue) {
            any = true;
            best = k;
            bestValue = value;
        }
    }
    if (!any) {
        result.status = AudioOffsetResult::Status::TooShort;
        return result;
    }

    float second = 0.0f;
    for (qint64 k = -maxLag; k <= maxLag; ++k) {
        if (std::llabs(k - best) <= guard)
            continue;
        second = std::max(second, ncc[size_t(k + maxLag)]);
    }

    result.lagSeconds = double(best) / sampleRate;
    result.peak = bestValue;
    result.secondPeak = second;
    const bool clear = bestValue >= params.minPeak && bestValue >= params.minPeakRatio * second;
    result.status = clear ? AudioOffsetResult::Status::Ok : AudioOffsetResult::Status::Ambiguous;
    return result;
}


std::vector<float> loudnessEnvelope(const std::vector<float> &mono, int sampleRate, int envelopeRate)
{
    std::vector<float> out;
    const qint64 hop = std::max<qint64>(1, sampleRate / std::max(1, envelopeRate));
    const qint64 frames = qint64(mono.size()) / hop;
    if (frames <= 0)
        return out;
    std::vector<double> level(size_t(frames), 0.0);
    double mean = 0.0;
    for (qint64 f = 0; f < frames; ++f) {
        double sum = 0.0;
        for (qint64 i = f * hop; i < (f + 1) * hop; ++i)
            sum += double(mono[size_t(i)]) * double(mono[size_t(i)]);
        level[size_t(f)] = sum / double(hop);
        mean += level[size_t(f)];
    }
    mean /= double(frames);
    // Pauses all read as the same quiet, whatever each microphone's own noise floor is.
    const double floor = std::max(1e-12, mean * 0.1);
    for (double &value : level)
        value = std::log10(value + floor);
    // A slow swell (someone walking towards one microphone) is not part of the pattern.
    const qint64 reach = std::max<qint64>(1, envelopeRate);
    std::vector<double> sums(size_t(frames) + 1, 0.0);
    for (qint64 f = 0; f < frames; ++f)
        sums[size_t(f) + 1] = sums[size_t(f)] + level[size_t(f)];
    out.resize(size_t(frames), 0.0f);
    for (qint64 f = 0; f < frames; ++f) {
        const qint64 a = std::max<qint64>(0, f - reach);
        const qint64 b = std::min<qint64>(frames, f + reach + 1);
        out[size_t(f)] = float(level[size_t(f)] - (sums[size_t(b)] - sums[size_t(a)]) / double(b - a));
    }
    return out;
}

namespace {

// Generalised cross-correlation with phase transform (GCC-PHAT) of two equally long windows: the
// lag k within +-maxLag that best lines up other[n] with anchor[n + k]. Whitening the
// cross-spectrum keeps a room's reflections and a voice's pitch from smearing the peak, so it lands
// on the direct sound where plain correlation can be pulled a frame off in a reverberant room.
// `sharpness` is the peak over the mean magnitude elsewhere in the range: about 4 to 5 for
// unrelated windows, 6 and more for two microphones on the same sound.
bool phatLag(const std::vector<float> &anchor, const std::vector<float> &other, qint64 maxLag, qint64 *lagOut,
             double *sharpnessOut)
{
    const qint64 n = qint64(std::min(anchor.size(), other.size()));
    if (maxLag < 1 || n <= 2 * maxLag)
        return false;
    qint64 size = 1;
    while (size < n + maxLag + 1)
        size <<= 1;
    const size_t specFloats = size_t(size) + 4;
    AlignedBuffer timeA(size), timeB(size), specA(specFloats), specB(specFloats), corr(size);
    if (!timeA.data || !timeB.data || !specA.data || !specB.data || !corr.data)
        return false;
    double meanA = 0.0;
    double meanB = 0.0;
    for (qint64 i = 0; i < n; ++i) {
        meanA += anchor[size_t(i)];
        meanB += other[size_t(i)];
    }
    meanA /= double(n);
    meanB /= double(n);
    for (qint64 i = 0; i < n; ++i) {
        timeA.data[i] = float(double(anchor[size_t(i)]) - meanA);
        timeB.data[i] = float(double(other[size_t(i)]) - meanB);
    }

    AVTXContext *fwd = nullptr;
    AVTXContext *inv = nullptr;
    av_tx_fn fwdFn = nullptr;
    av_tx_fn invFn = nullptr;
    float fwdScale = 1.0f;
    float invScale = 1.0f / float(size);
    const bool ready = av_tx_init(&fwd, &fwdFn, AV_TX_FLOAT_RDFT, 0, int(size), &fwdScale, 0) >= 0
                       && av_tx_init(&inv, &invFn, AV_TX_FLOAT_RDFT, 1, int(size), &invScale, 0) >= 0;
    if (!ready) {
        if (fwd)
            av_tx_uninit(&fwd);
        if (inv)
            av_tx_uninit(&inv);
        return false;
    }
    fwdFn(fwd, specA.data, timeA.data, sizeof(float));
    fwdFn(fwd, specB.data, timeB.data, sizeof(float));
    const qint64 bins = size / 2 + 1;
    double largest = 0.0;
    for (qint64 i = 0; i < bins; ++i) {
        const double ar = specA.data[2 * i], ai = specA.data[2 * i + 1];
        const double br = specB.data[2 * i], bi = specB.data[2 * i + 1];
        largest = std::max(largest, std::hypot(ar * br + ai * bi, ai * br - ar * bi));
    }
    // Bins with next to nothing in them are not blown up to full weight.
    const double floor = std::max(1e-20, largest * 1e-6);
    for (qint64 i = 0; i < bins; ++i) {
        const double ar = specA.data[2 * i], ai = specA.data[2 * i + 1];
        const double br = specB.data[2 * i], bi = specB.data[2 * i + 1];
        const double re = ar * br + ai * bi;
        const double im = ai * br - ar * bi;
        const double scale = 1.0 / (std::hypot(re, im) + floor);
        specA.data[2 * i] = float(re * scale);
        specA.data[2 * i + 1] = float(im * scale);
    }
    invFn(inv, corr.data, specA.data, sizeof(float));
    av_tx_uninit(&fwd);
    av_tx_uninit(&inv);

    qint64 best = 0;
    double bestValue = -std::numeric_limits<double>::infinity();
    for (qint64 k = -maxLag; k <= maxLag; ++k) {
        const double value = corr.data[(k + size) % size];
        if (value > bestValue) {
            bestValue = value;
            best = k;
        }
    }
    // The peak's own lobe (a millisecond either side) does not count as "elsewhere".
    const qint64 lobe = std::max<qint64>(1, maxLag / 60);
    double elsewhere = 0.0;
    qint64 count = 0;
    for (qint64 k = -maxLag; k <= maxLag; ++k) {
        if (std::llabs(k - best) <= lobe)
            continue;
        elsewhere += std::abs(double(corr.data[(k + size) % size]));
        ++count;
    }
    *lagOut = best;
    *sharpnessOut = count > 0 && elsewhere > 0.0 ? bestValue / (elsewhere / double(count)) : 0.0;
    return true;
}

} // namespace

AudioOffsetResult findRecordingOffset(const std::vector<float> &anchor, const std::vector<float> &other,
                                      int sampleRate, const std::function<bool()> &keepGoing)
{
    constexpr int kEnvelopeRate = 100;
    if (rmsOf(anchor) < AudioOffsetParams().silentRms || rmsOf(other) < AudioOffsetParams().silentRms) {
        AudioOffsetResult silent;
        silent.status = AudioOffsetResult::Status::Silent;
        return silent;
    }
    const std::vector<float> envelopeA = loudnessEnvelope(anchor, sampleRate, kEnvelopeRate);
    const std::vector<float> envelopeB = loudnessEnvelope(other, sampleRate, kEnvelopeRate);

    AudioOffsetParams coarse;
    // Every lag that leaves the two enough in common: a quarter of the shorter one, between 4 s and
    // a minute. A sliver of overlap at the very edge of the search correlates by chance too easily.
    const double shorter = double(std::min(envelopeA.size(), envelopeB.size())) / kEnvelopeRate;
    coarse.maxLagSeconds = double(envelopeA.size() + envelopeB.size()) / kEnvelopeRate;
    coarse.minOverlapSeconds = std::clamp(shorter * 0.25, 4.0, 60.0);
    coarse.minPeak = 0.3;
    coarse.minPeakRatio = 1.5;
    coarse.silentRms = 1e-6;
    AudioOffsetResult result = findAudioOffset(envelopeA, envelopeB, kEnvelopeRate, coarse, keepGoing);
    if (result.status != AudioOffsetResult::Status::Ok)
        return result;
    // A short stretch in common lines up by chance far more readily than a long one: two
    // unrelated conversations reach 0.5 over a few seconds, while two microphones on the same one
    // stay near 0.8 however short. So the peak has to be the higher the shorter the overlap, as
    // chance falls with its square root.
    {
        const qint64 k = std::llround(result.lagSeconds * kEnvelopeRate);
        const qint64 na = qint64(envelopeA.size());
        const qint64 nb = qint64(envelopeB.size());
        const double overlap = double(std::min(nb, na - k) - std::max<qint64>(0, -k)) / kEnvelopeRate;
        constexpr double kMinEvidence = 1.75;
        if (overlap <= 0.0 || result.peak * std::sqrt(overlap) < kMinEvidence) {
            result.status = AudioOffsetResult::Status::Ambiguous;
            return result;
        }
    }

    // Refine on the waveforms over up to 30 s from the middle of what the two have in common.
    const qint64 na = qint64(anchor.size());
    const qint64 nb = qint64(other.size());
    const qint64 lag = std::llround(result.lagSeconds * sampleRate);
    const qint64 lo = std::max<qint64>(0, lag);
    const qint64 hi = std::min<qint64>(na, nb + lag);
    if (hi - lo < 2 * qint64(sampleRate))
        return result;
    const qint64 window = std::min<qint64>(hi - lo, 30 * qint64(sampleRate));
    const qint64 start = lo + (hi - lo - window) / 2;
    if (start - lag < 0 || start - lag + window > nb)
        return result;
    if (keepGoing && !keepGoing())
        return result;
    const std::vector<float> windowA(anchor.begin() + start, anchor.begin() + start + window);
    const std::vector<float> windowB(other.begin() + (start - lag), other.begin() + (start - lag) + window);
    // The contour is good to a few of its 10 ms windows; reverb drags it late by up to ~20 ms.
    const qint64 reach = qint64(0.06 * sampleRate);
    qint64 residual = 0;
    double sharpness = 0.0;
    // Below the threshold the waveforms do not share a clear peak (a recording processed beyond
    // recognition, say): the contour's answer stands.
    constexpr double kMinSharpness = 5.5;
    if (phatLag(windowA, windowB, reach, &residual, &sharpness) && sharpness >= kMinSharpness)
        result.lagSeconds = double(lag + residual) / sampleRate;
    return result;
}

} // namespace drift
