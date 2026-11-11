#include "engine/audio/dsp/FfmpegAudioFilter.h"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

extern "C" {
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/channel_layout.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/opt.h>
}

namespace drift {

namespace {

QString trTools(const char *text)
{
    return QCoreApplication::translate("AudioToolsController", text);
}

QString avError(int code)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(code, buf, sizeof(buf));
    return QString::fromUtf8(buf);
}

bool fail(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}

QString layoutName(int channels)
{
    return channels == 1 ? QStringLiteral("mono") : QStringLiteral("stereo");
}

// Frames are handed to the graph in blocks of this size, so a long clip never makes one huge frame.
constexpr int kBlockFrames = 4096;

} // namespace

struct FfmpegAudioFilter::Impl
{
    AVFilterGraph *graph = nullptr;
    AVFilterContext *src = nullptr;
    AVFilterContext *sink = nullptr;
    AVFrame *frame = nullptr;
    int rate = 0;
    int channels = 0;
    int64_t pushed = 0;

    ~Impl()
    {
        av_frame_free(&frame);
        avfilter_graph_free(&graph);
    }
};

FfmpegAudioFilter::FfmpegAudioFilter() = default;
FfmpegAudioFilter::~FfmpegAudioFilter() = default;

bool FfmpegAudioFilter::hasFilter(const char *name)
{
    return avfilter_get_by_name(name) != nullptr;
}

bool FfmpegAudioFilter::open(int sampleRate, int channels, const QString &chain, QString *error)
{
    if (sampleRate <= 0 || (channels != 1 && channels != 2))
        return fail(error, QStringLiteral("unsupported audio format"));
    d = std::make_unique<Impl>();
    d->rate = sampleRate;
    d->channels = channels;
    d->graph = avfilter_graph_alloc();
    d->frame = av_frame_alloc();
    if (!d->graph || !d->frame)
        return fail(error, QStringLiteral("out of memory"));

    const QByteArray srcArgs = QStringLiteral("sample_rate=%1:sample_fmt=flt:channel_layout=%2:time_base=1/%1")
                                   .arg(sampleRate)
                                   .arg(layoutName(channels))
                                   .toUtf8();
    int rc = avfilter_graph_create_filter(&d->src, avfilter_get_by_name("abuffer"), "in",
                                          srcArgs.constData(), nullptr, d->graph);
    if (rc < 0)
        return fail(error, QStringLiteral("abuffer: %1").arg(avError(rc)));
    rc = avfilter_graph_create_filter(&d->sink, avfilter_get_by_name("abuffersink"), "out", nullptr,
                                      nullptr, d->graph);
    if (rc < 0)
        return fail(error, QStringLiteral("abuffersink: %1").arg(avError(rc)));

    // The tail brings whatever the chain did (loudnorm's 192 kHz, ebur128's doubles) back to the
    // input's own shape, so callers can treat output and input as the same timeline.
    QString body = chain.trimmed();
    if (body.isEmpty())
        body = QStringLiteral("anull");
    const QByteArray spec = QStringLiteral("%1,aresample=%2,aformat=sample_fmts=flt:sample_rates=%2:channel_layouts=%3")
                                .arg(body)
                                .arg(sampleRate)
                                .arg(layoutName(channels))
                                .toUtf8();

    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs = avfilter_inout_alloc();
    outputs->name = av_strdup("in");
    outputs->filter_ctx = d->src;
    outputs->pad_idx = 0;
    outputs->next = nullptr;
    inputs->name = av_strdup("out");
    inputs->filter_ctx = d->sink;
    inputs->pad_idx = 0;
    inputs->next = nullptr;
    rc = avfilter_graph_parse_ptr(d->graph, spec.constData(), &inputs, &outputs, nullptr);
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
    if (rc < 0)
        return fail(error, QStringLiteral("%1: %2").arg(body, avError(rc)));
    rc = avfilter_graph_config(d->graph, nullptr);
    if (rc < 0)
        return fail(error, QStringLiteral("%1: %2").arg(body, avError(rc)));
    return true;
}

bool FfmpegAudioFilter::push(const float *interleaved, int frames, std::vector<float> &out,
                             QString *error)
{
    if (!d || !d->graph)
        return fail(error, QStringLiteral("filter not open"));
    for (int done = 0; done < frames;) {
        const int n = std::min(kBlockFrames, frames - done);
        AVFrame *f = av_frame_alloc();
        if (!f)
            return fail(error, QStringLiteral("out of memory"));
        f->format = AV_SAMPLE_FMT_FLT;
        f->sample_rate = d->rate;
        f->nb_samples = n;
        av_channel_layout_default(&f->ch_layout, d->channels);
        f->pts = d->pushed;
        int rc = av_frame_get_buffer(f, 0);
        if (rc < 0) {
            av_frame_free(&f);
            return fail(error, avError(rc));
        }
        std::memcpy(f->data[0], interleaved + size_t(done) * d->channels,
                    size_t(n) * d->channels * sizeof(float));
        rc = av_buffersrc_add_frame(d->src, f);
        av_frame_free(&f);
        if (rc < 0)
            return fail(error, avError(rc));
        d->pushed += n;
        done += n;
        if (!drain(out, error))
            return false;
    }
    return true;
}

bool FfmpegAudioFilter::finish(std::vector<float> &out, QString *error)
{
    if (!d || !d->graph)
        return fail(error, QStringLiteral("filter not open"));
    const int rc = av_buffersrc_add_frame(d->src, nullptr);
    if (rc < 0 && rc != AVERROR_EOF)
        return fail(error, avError(rc));
    return drain(out, error);
}

bool FfmpegAudioFilter::drain(std::vector<float> &out, QString *error)
{
    const AVRational tb = av_buffersink_get_time_base(d->sink);
    for (;;) {
        const int rc = av_buffersink_get_frame(d->sink, d->frame);
        if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF)
            return true;
        if (rc < 0)
            return fail(error, avError(rc));
        const size_t samples = size_t(d->frame->nb_samples) * size_t(d->channels);
        const float *data = reinterpret_cast<const float *>(d->frame->data[0]);
        out.insert(out.end(), data, data + samples);
        if (m_onMetadata && d->frame->metadata) {
            Metadata meta;
            const AVDictionaryEntry *e = nullptr;
            while ((e = av_dict_get(d->frame->metadata, "", e, AV_DICT_IGNORE_SUFFIX)))
                meta.append({QString::fromUtf8(e->key), QString::fromUtf8(e->value)});
            const double at = d->frame->pts == AV_NOPTS_VALUE ? 0.0 : double(d->frame->pts) * av_q2d(tb);
            m_onMetadata(at, meta);
        }
        av_frame_unref(d->frame);
    }
}

bool streamAudioFilter(int sampleRate, int channels, const QString &chain, const AudioFeed &feed,
                       const AudioSink &sink, QString *error,
                       const FfmpegAudioFilter::MetadataCallback &onMetadata)
{
    FfmpegAudioFilter filter;
    filter.setMetadataCallback(onMetadata);
    if (!filter.open(sampleRate, channels, chain, error))
        return false;
    const size_t ch = size_t(channels);
    std::vector<float> in(size_t(kBlockFrames) * ch);
    std::vector<float> out;
    int64_t inFrames = 0;
    int64_t outFrames = 0;
    // Output beyond the input's length (a resampler's tail) is dropped, so callers get a buffer that
    // lines up sample for sample with the source.
    const auto deliver = [&](int64_t limit) {
        const int64_t have = int64_t(out.size() / ch);
        const int64_t n = std::min(have, std::max<int64_t>(0, limit - outFrames));
        if (n > 0 && sink && !sink(out.data(), int(n)))
            return false;
        outFrames += n;
        out.clear();
        return true;
    };
    for (;;) {
        const int got = feed(in.data(), kBlockFrames);
        if (got < 0)
            return fail(error, trTools("Cancelled"));
        if (got == 0)
            break;
        inFrames += got;
        if (!filter.push(in.data(), got, out, error))
            return false;
        if (!deliver(inFrames))
            return fail(error, trTools("Cancelled"));
    }
    if (!filter.finish(out, error))
        return false;
    if (!deliver(inFrames))
        return fail(error, trTools("Cancelled"));
    // ...and a short tail is padded with silence for the same reason.
    std::fill(in.begin(), in.end(), 0.0f);
    while (outFrames < inFrames) {
        const int n = int(std::min<int64_t>(kBlockFrames, inFrames - outFrames));
        if (sink && !sink(in.data(), n))
            return fail(error, trTools("Cancelled"));
        outFrames += n;
    }
    return true;
}

AudioFeed bufferFeed(const std::vector<float> &interleaved, int channels)
{
    const size_t ch = size_t(std::max(1, channels));
    auto pos = std::make_shared<size_t>(0);
    return [&interleaved, ch, pos](float *dst, int maxFrames) {
        const size_t left = (interleaved.size() - *pos) / ch;
        const size_t n = std::min(left, size_t(maxFrames));
        std::memcpy(dst, interleaved.data() + *pos, n * ch * sizeof(float));
        *pos += n * ch;
        return int(n);
    };
}

std::vector<float> runAudioFilter(const std::vector<float> &interleaved, int sampleRate, int channels,
                                  const QString &chain, QString *error,
                                  const FfmpegAudioFilter::MetadataCallback &onMetadata)
{
    std::vector<float> out;
    out.reserve(interleaved.size());
    const bool ok = streamAudioFilter(
        sampleRate, channels, chain, bufferFeed(interleaved, channels),
        [&out, channels](const float *data, int frames) {
            out.insert(out.end(), data, data + size_t(frames) * size_t(channels));
            return true;
        },
        error, onMetadata);
    if (!ok)
        return {};
    return out;
}

LoudnessStats measureEbuR128(const AudioFeed &feed, int sampleRate, int channels, QString *error)
{
    LoudnessStats stats;
    QString integrated, range;
    double peak = -70.0;
    bool sawPeak = false;
    const bool ran = streamAudioFilter(
        sampleRate, channels, QStringLiteral("ebur128=metadata=1:peak=true"), feed, {}, error,
        [&](double, const FfmpegAudioFilter::Metadata &meta) {
            // Every frame carries the running totals; the last frame's are the whole clip's.
            double framePeak = -70.0;
            bool frameHasPeak = false;
            for (const auto &kv : meta) {
                if (kv.first == QLatin1String("lavfi.r128.I"))
                    integrated = kv.second;
                else if (kv.first == QLatin1String("lavfi.r128.LRA"))
                    range = kv.second;
                else if (kv.first.startsWith(QLatin1String("lavfi.r128.true_peaks_ch"))) {
                    // Linear sample values (1.0 = full scale), not dB.
                    const double linear = kv.second.toDouble();
                    framePeak = std::max(framePeak, linear > 0.0 ? 20.0 * std::log10(linear) : -70.0);
                    frameHasPeak = true;
                }
            }
            if (frameHasPeak) {
                peak = framePeak;
                sawPeak = true;
            }
        });
    if (!ran)
        return stats;
    bool okI = false;
    stats.integratedLufs = integrated.toDouble(&okI);
    stats.rangeLu = std::max(0.0, range.toDouble());
    stats.truePeakDb = sawPeak && std::isfinite(peak) ? peak : -70.0;
    // loudnorm's measured_thresh is the relative gate of the integrated measurement: I - 10 LU.
    stats.thresholdLufs = stats.integratedLufs - 10.0;
    stats.ok = okI && std::isfinite(stats.integratedLufs) && stats.integratedLufs > -69.0;
    if (!stats.ok && error && error->isEmpty())
        *error = trTools("The clip is silent or too short to measure");
    return stats;
}

LoudnessStats measureEbuR128(const std::vector<float> &interleaved, int sampleRate, int channels,
                             QString *error)
{
    return measureEbuR128(bufferFeed(interleaved, channels), sampleRate, channels, error);
}

QString loudnormChain(double targetLufs, double truePeakDb, double rangeLu, const LoudnessStats &m)
{
    const auto num = [](double v) { return QString::number(v, 'f', 2); };
    // loudnorm takes LRA 1-50 and its measured values within its own documented ranges.
    const double measuredRange = std::clamp(m.rangeLu, 0.0, 99.0);
    const double targetRange = std::clamp(std::max(rangeLu, measuredRange), 1.0, 50.0);
    return QStringLiteral("loudnorm=I=%1:TP=%2:LRA=%3:measured_I=%4:measured_LRA=%5:measured_TP=%6:"
                          "measured_thresh=%7:linear=true:print_format=none")
        .arg(num(std::clamp(targetLufs, -70.0, -5.0)), num(std::clamp(truePeakDb, -9.0, 0.0)),
             num(targetRange), num(std::clamp(m.integratedLufs, -99.0, 0.0)), num(measuredRange),
             num(std::clamp(m.truePeakDb, -99.0, 99.0)), num(std::clamp(m.thresholdLufs, -99.0, 0.0)));
}

QString afftdnChain(double strength)
{
    const double s = std::clamp(strength, 0.0, 1.0);
    // nr: how far the noise is pushed down (dB); nf: where the noise floor starts its estimate. A
    // gentle setting keeps voices natural, the strong end is for a fan or hum right up front.
    const double nr = 6.0 + 34.0 * s;
    const double nf = -50.0 + 20.0 * s;
    return QStringLiteral("afftdn=nr=%1:nf=%2:tn=1")
        .arg(QString::number(nr, 'f', 1), QString::number(nf, 'f', 1));
}

QList<SilenceSpanSeconds> detectSilenceFfmpeg(const AudioFeed &feed, int sampleRate, int channels,
                                              double thresholdDb, double minSeconds, QString *error)
{
    QList<SilenceSpanSeconds> spans;
    int64_t frames = 0;
    double openStart = -1.0;
    const auto counted = [&](float *dst, int maxFrames) {
        const int n = feed(dst, maxFrames);
        if (n > 0)
            frames += n;
        return n;
    };
    const QString chain = QStringLiteral("silencedetect=noise=%1dB:d=%2")
                              .arg(QString::number(std::clamp(thresholdDb, -90.0, -5.0), 'f', 1),
                                   QString::number(std::max(0.05, minSeconds), 'f', 3));
    const bool ran = streamAudioFilter(
        sampleRate, channels, chain, counted, {}, error,
        [&](double, const FfmpegAudioFilter::Metadata &meta) {
            for (const auto &kv : meta) {
                if (kv.first.startsWith(QLatin1String("lavfi.silence_start"))) {
                    openStart = std::max(0.0, kv.second.toDouble());
                } else if (kv.first.startsWith(QLatin1String("lavfi.silence_end")) && openStart >= 0.0) {
                    const double end = kv.second.toDouble();
                    if (end > openStart)
                        spans.append({openStart, end});
                    openStart = -1.0;
                }
            }
        });
    if (!ran)
        return {};
    const double total = double(frames) / double(std::max(1, sampleRate));
    for (SilenceSpanSeconds &s : spans)
        s.end = std::min(s.end, total);
    // silencedetect reports a silence that runs to the end only in its log, not as metadata.
    if (openStart >= 0.0 && total - openStart >= minSeconds)
        spans.append({openStart, total});
    return spans;
}

QList<SilenceSpanSeconds> detectSilenceFfmpeg(const std::vector<float> &interleaved, int sampleRate,
                                              int channels, double thresholdDb, double minSeconds,
                                              QString *error)
{
    return detectSilenceFfmpeg(bufferFeed(interleaved, channels), sampleRate, channels, thresholdDb,
                               minSeconds, error);
}

} // namespace drift
