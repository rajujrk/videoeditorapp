#include "decode/AudioDecoder.h"

#include <algorithm>
#include <cstring>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

namespace tve {

namespace {

constexpr AVRational kTickTimeBase{1, static_cast<int>(kTicksPerSecond)};

std::string avError(int code)
{
    char buffer[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(code, buffer, sizeof buffer);
    return buffer;
}

} // namespace

struct AudioDecoder::Impl {
    AVFormatContext* format = nullptr;
    AVCodecContext* codec = nullptr;
    SwrContext* swr = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;
    int streamIndex = -1;
    AVRational timeBase{};
    std::int64_t startTime = 0;
    bool inputDone = false;
    bool drained = false;

    // Converted samples not yet handed out, and the media position of the first of them.
    std::vector<float> pending;
    std::size_t pendingOffset = 0; // in floats
    std::int64_t pendingPosition = 0; // sample index of pending[pendingOffset]
    std::int64_t discardUntil = -1;   // after a seek: drop samples before this index
    bool positionKnown = false;

    AudioStreamInfo info;

    ~Impl()
    {
        swr_free(&swr);
        av_frame_free(&frame);
        av_packet_free(&packet);
        avcodec_free_context(&codec);
        avformat_close_input(&format);
    }

    bool resetResampler()
    {
        swr_free(&swr);
        AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
        if (swr_alloc_set_opts2(&swr, &stereo, AV_SAMPLE_FMT_FLT, audio::kSampleRate, &codec->ch_layout,
                                codec->sample_fmt, codec->sample_rate, 0, nullptr) < 0)
            return false;
        // Mono plays at full level on both sides (a voice-over peaking at -6 dB meters at
        // -6 dB), rather than FFmpeg's default of 3 dB down on each.
        if (codec->ch_layout.nb_channels == 1) {
            const double unity[2] = {1.0, 1.0};
            if (swr_set_matrix(swr, unity, 1) < 0)
                return false;
        }
        return swr_init(swr) >= 0;
    }

    std::size_t available() const { return (pending.size() - pendingOffset) / audio::kChannels; }

    // Decodes one more chunk into `pending`. Returns false at end of stream.
    bool decodeMore();
};

bool AudioDecoder::Impl::decodeMore()
{
    while (true) {
        const int rc = avcodec_receive_frame(codec, frame);
        if (rc == 0) {
            if (!positionKnown) {
                // First frame after open/seek: anchor the sample position to its timestamp.
                const std::int64_t ts = frame->best_effort_timestamp != AV_NOPTS_VALUE ? frame->best_effort_timestamp
                                                                                         : frame->pts;
                const Ticks t = ts == AV_NOPTS_VALUE ? 0 : av_rescale_q(ts - startTime, timeBase, kTickTimeBase);
                pendingPosition = audio::samplesFromTicks(std::max<Ticks>(t, 0)) - std::int64_t(available());
                positionKnown = true;
            }
            const int maxOut = swr_get_out_samples(swr, frame->nb_samples);
            const std::size_t oldSize = pending.size();
            pending.resize(oldSize + std::size_t(maxOut) * audio::kChannels);
            auto* out = reinterpret_cast<std::uint8_t*>(pending.data() + oldSize);
            const int converted = swr_convert(swr, &out, maxOut, const_cast<const std::uint8_t**>(frame->extended_data),
                                              frame->nb_samples);
            pending.resize(oldSize + std::size_t(std::max(converted, 0)) * audio::kChannels);
            av_frame_unref(frame);
            return true;
        }
        if (rc == AVERROR_EOF) {
            if (drained)
                return false;
            // Flush what the resampler still holds.
            drained = true;
            const int maxOut = swr_get_out_samples(swr, 0);
            if (maxOut <= 0)
                return false;
            const std::size_t oldSize = pending.size();
            pending.resize(oldSize + std::size_t(maxOut) * audio::kChannels);
            auto* out = reinterpret_cast<std::uint8_t*>(pending.data() + oldSize);
            const int converted = swr_convert(swr, &out, maxOut, nullptr, 0);
            pending.resize(oldSize + std::size_t(std::max(converted, 0)) * audio::kChannels);
            return converted > 0;
        }
        if (rc != AVERROR(EAGAIN) || inputDone)
            return false;
        if (av_read_frame(format, packet) < 0) {
            inputDone = true;
            avcodec_send_packet(codec, nullptr);
            continue;
        }
        if (packet->stream_index == streamIndex)
            avcodec_send_packet(codec, packet);
        av_packet_unref(packet);
    }
}

AudioDecoder::AudioDecoder(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}

AudioDecoder::~AudioDecoder() = default;

std::unique_ptr<AudioDecoder> AudioDecoder::open(const std::string& path, std::string* error)
{
    auto fail = [&](std::string message) -> std::unique_ptr<AudioDecoder> {
        if (error)
            *error = std::move(message);
        return nullptr;
    };

    auto d = std::make_unique<Impl>();
    if (int rc = avformat_open_input(&d->format, path.c_str(), nullptr, nullptr); rc < 0)
        return fail("cannot open " + path + ": " + avError(rc));
    if (int rc = avformat_find_stream_info(d->format, nullptr); rc < 0)
        return fail("cannot read stream info: " + avError(rc));

    const AVCodec* decoder = nullptr;
    d->streamIndex = av_find_best_stream(d->format, AVMEDIA_TYPE_AUDIO, -1, -1, &decoder, 0);
    if (d->streamIndex < 0 || !decoder)
        return fail("no audio stream in " + path);

    AVStream* stream = d->format->streams[d->streamIndex];
    d->timeBase = stream->time_base;
    d->startTime = stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time;
    d->codec = avcodec_alloc_context3(decoder);
    if (!d->codec || avcodec_parameters_to_context(d->codec, stream->codecpar) < 0)
        return fail("cannot configure audio decoder");
    if (int rc = avcodec_open2(d->codec, decoder, nullptr); rc < 0)
        return fail("cannot open audio decoder: " + avError(rc));
    if (!d->resetResampler())
        return fail("cannot convert this audio format");

    d->packet = av_packet_alloc();
    d->frame = av_frame_alloc();
    d->info.sourceSampleRate = d->codec->sample_rate;
    d->info.sourceChannels = d->codec->ch_layout.nb_channels;
    d->info.codecName = decoder->name;
    if (stream->duration != AV_NOPTS_VALUE)
        d->info.duration = av_rescale_q(stream->duration, d->timeBase, kTickTimeBase);
    else if (d->format->duration != AV_NOPTS_VALUE)
        d->info.duration = av_rescale_q(d->format->duration, AV_TIME_BASE_Q, kTickTimeBase);
    return std::unique_ptr<AudioDecoder>(new AudioDecoder(std::move(d)));
}

const AudioStreamInfo& AudioDecoder::info() const
{
    return m_impl->info;
}

bool AudioDecoder::seek(Ticks target)
{
    Impl& d = *m_impl;
    target = std::max<Ticks>(target, 0);
    const std::int64_t timestamp = d.startTime + av_rescale_q(target, kTickTimeBase, d.timeBase);
    if (av_seek_frame(d.format, d.streamIndex, timestamp, AVSEEK_FLAG_BACKWARD) < 0)
        return false;
    avcodec_flush_buffers(d.codec);
    if (!d.resetResampler())
        return false;
    d.inputDone = false;
    d.drained = false;
    d.pending.clear();
    d.pendingOffset = 0;
    d.positionKnown = false;
    d.discardUntil = audio::samplesFromTicks(target);
    return true;
}

int AudioDecoder::read(float* out, int frames)
{
    Impl& d = *m_impl;
    int written = 0;
    while (written < frames) {
        if (d.available() == 0) {
            d.pending.clear();
            d.pendingOffset = 0;
            if (!d.decodeMore())
                break;
            continue;
        }
        // Drop samples before a seek target (decoding resumed from an earlier packet).
        if (d.discardUntil >= 0 && d.positionKnown) {
            const std::int64_t drop = std::min<std::int64_t>(d.discardUntil - d.pendingPosition, std::int64_t(d.available()));
            if (drop > 0) {
                d.pendingOffset += std::size_t(drop) * audio::kChannels;
                d.pendingPosition += drop;
                continue;
            }
            if (d.pendingPosition > d.discardUntil) {
                // The stream started after the target: pad with silence up to the first sample.
                const std::int64_t gap = std::min<std::int64_t>(d.pendingPosition - d.discardUntil, frames - written);
                std::memset(out + written * audio::kChannels, 0, std::size_t(gap) * audio::kChannels * sizeof(float));
                written += int(gap);
                d.discardUntil += gap;
                continue;
            }
            d.discardUntil = -1;
        }
        const int n = std::min(frames - written, int(d.available()));
        std::memcpy(out + written * audio::kChannels, d.pending.data() + d.pendingOffset,
                    std::size_t(n) * audio::kChannels * sizeof(float));
        d.pendingOffset += std::size_t(n) * audio::kChannels;
        d.pendingPosition += n;
        written += n;
    }
    return written;
}

Ticks AudioDecoder::position() const
{
    const Impl& d = *m_impl;
    if (d.discardUntil >= 0)
        return audio::ticksFromSamples(d.discardUntil);
    return audio::ticksFromSamples(d.pendingPosition);
}

} // namespace tve
