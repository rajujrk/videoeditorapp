#include "encode/Mp4Writer.h"

#include "decode/AudioDecoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
}

#include <algorithm>
#include <cstring>
#include <vector>

namespace tve {

namespace {

std::string avError(int code)
{
    char text[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, text, sizeof text);
    return text;
}

constexpr int kAudioFrameSize = 1024; // AAC

} // namespace

std::int64_t defaultVideoBitrate(const Mp4Settings& settings)
{
    const double fps = double(settings.frameRate.num) / double(settings.frameRate.den);
    const double h264 = 0.15 * settings.width * settings.height * fps;
    return std::int64_t(settings.codec == VideoCodec::Hevc ? h264 * 2.0 / 3.0 : h264);
}

struct Mp4Writer::Impl {
    Mp4Settings settings;
    std::string path;
    AVFormatContext* format = nullptr;
    AVCodecContext* video = nullptr;
    AVCodecContext* audio = nullptr;
    AVStream* videoStream = nullptr;
    AVStream* audioStream = nullptr;
    AVFrame* videoFrame = nullptr;
    AVFrame* audioFrame = nullptr;
    AVPacket* packet = nullptr;
    std::int64_t videoPts = 0;
    std::int64_t audioPts = 0;
    std::vector<float> pendingAudio; // interleaved, less than one AAC frame
    bool headerWritten = false;
    bool finished = false;
    std::string error;

    ~Impl()
    {
        av_packet_free(&packet);
        av_frame_free(&videoFrame);
        av_frame_free(&audioFrame);
        avcodec_free_context(&video);
        avcodec_free_context(&audio);
        if (format) {
            if (format->pb)
                avio_closep(&format->pb);
            avformat_free_context(format);
        }
    }

    bool fail(const std::string& message)
    {
        error = message;
        return false;
    }

    // Sends a frame (or nullptr to flush) and writes every packet that comes out.
    bool encode(AVCodecContext* codec, AVStream* stream, AVFrame* frame)
    {
        if (int rc = avcodec_send_frame(codec, frame); rc < 0)
            return fail("encoding failed: " + avError(rc));
        for (;;) {
            const int rc = avcodec_receive_packet(codec, packet);
            if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF)
                return true;
            if (rc < 0)
                return fail("encoding failed: " + avError(rc));
            av_packet_rescale_ts(packet, codec->time_base, stream->time_base);
            packet->stream_index = stream->index;
            if (int w = av_interleaved_write_frame(format, packet); w < 0)
                return fail("writing failed: " + avError(w));
        }
    }

    bool writeAudioFrame(const float* interleaved)
    {
        if (av_frame_make_writable(audioFrame) < 0)
            return fail("out of memory");
        auto* left = reinterpret_cast<float*>(audioFrame->data[0]);
        auto* right = reinterpret_cast<float*>(audioFrame->data[1]);
        for (int i = 0; i < kAudioFrameSize; ++i) {
            left[i] = interleaved[i * 2];
            right[i] = interleaved[i * 2 + 1];
        }
        audioFrame->pts = audioPts;
        audioPts += kAudioFrameSize;
        return encode(audio, audioStream, audioFrame);
    }
};

Mp4Writer::Mp4Writer(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}

Mp4Writer::~Mp4Writer() = default;

std::unique_ptr<Mp4Writer> Mp4Writer::open(const std::string& path, const Mp4Settings& settings, std::string* error)
{
    auto d = std::make_unique<Impl>();
    d->settings = settings;
    d->path = path;
    const auto fail = [&](const std::string& message) -> std::unique_ptr<Mp4Writer> {
        if (error)
            *error = message;
        return nullptr;
    };
    if (settings.width <= 0 || settings.height <= 0 || settings.width % 2 || settings.height % 2)
        return fail("the picture size must be even and positive");

    if (int rc = avformat_alloc_output_context2(&d->format, nullptr, "mp4", path.c_str()); rc < 0)
        return fail("cannot create " + path + ": " + avError(rc));

    // Video
    const char* encoderName = settings.codec == VideoCodec::Hevc ? "hevc_videotoolbox" : "h264_videotoolbox";
    const AVCodec* videoCodec = avcodec_find_encoder_by_name(encoderName);
    if (!videoCodec)
        return fail(std::string("no ") + encoderName + " encoder");
    d->video = avcodec_alloc_context3(videoCodec);
    d->video->width = settings.width;
    d->video->height = settings.height;
    d->video->pix_fmt = AV_PIX_FMT_NV12;
    d->video->time_base = AVRational{settings.frameRate.den, settings.frameRate.num};
    d->video->framerate = AVRational{settings.frameRate.num, settings.frameRate.den};
    d->video->bit_rate = settings.videoBitrate > 0 ? settings.videoBitrate : defaultVideoBitrate(settings);
    d->video->gop_size = std::max(1, 2 * settings.frameRate.num / std::max(1, settings.frameRate.den)); // 2 s
    d->video->color_primaries = AVCOL_PRI_BT709;
    d->video->color_trc = AVCOL_TRC_BT709;
    d->video->colorspace = AVCOL_SPC_BT709;
    d->video->color_range = AVCOL_RANGE_MPEG;
    if (d->format->oformat->flags & AVFMT_GLOBALHEADER)
        d->video->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    av_opt_set_int(d->video->priv_data, "allow_sw", 1, 0); // fall back if the hardware is busy
    if (settings.codec == VideoCodec::Hevc)
        d->video->codec_tag = MKTAG('h', 'v', 'c', '1'); // so QuickTime and Finder play it
    if (int rc = avcodec_open2(d->video, videoCodec, nullptr); rc < 0)
        return fail(std::string("cannot open the ") + encoderName + " encoder: " + avError(rc));
    d->videoStream = avformat_new_stream(d->format, nullptr);
    avcodec_parameters_from_context(d->videoStream->codecpar, d->video);
    d->videoStream->time_base = d->video->time_base;
    d->videoStream->avg_frame_rate = d->video->framerate;

    // Audio
    const AVCodec* audioCodec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!audioCodec)
        return fail("no AAC encoder");
    d->audio = avcodec_alloc_context3(audioCodec);
    d->audio->sample_fmt = AV_SAMPLE_FMT_FLTP;
    d->audio->sample_rate = audio::kSampleRate;
    av_channel_layout_default(&d->audio->ch_layout, audio::kChannels);
    d->audio->bit_rate = settings.audioBitrate;
    d->audio->time_base = AVRational{1, audio::kSampleRate};
    if (d->format->oformat->flags & AVFMT_GLOBALHEADER)
        d->audio->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if (int rc = avcodec_open2(d->audio, audioCodec, nullptr); rc < 0)
        return fail("cannot open the AAC encoder: " + avError(rc));
    d->audioStream = avformat_new_stream(d->format, nullptr);
    avcodec_parameters_from_context(d->audioStream->codecpar, d->audio);
    d->audioStream->time_base = d->audio->time_base;

    // Frames
    d->videoFrame = av_frame_alloc();
    d->videoFrame->format = AV_PIX_FMT_NV12;
    d->videoFrame->width = settings.width;
    d->videoFrame->height = settings.height;
    d->videoFrame->color_range = AVCOL_RANGE_MPEG;
    d->videoFrame->colorspace = AVCOL_SPC_BT709;
    d->videoFrame->color_primaries = AVCOL_PRI_BT709;
    d->videoFrame->color_trc = AVCOL_TRC_BT709;
    d->audioFrame = av_frame_alloc();
    d->audioFrame->format = AV_SAMPLE_FMT_FLTP;
    d->audioFrame->sample_rate = audio::kSampleRate;
    d->audioFrame->nb_samples = kAudioFrameSize;
    av_channel_layout_copy(&d->audioFrame->ch_layout, &d->audio->ch_layout);
    if (av_frame_get_buffer(d->videoFrame, 0) < 0 || av_frame_get_buffer(d->audioFrame, 0) < 0)
        return fail("out of memory");
    d->packet = av_packet_alloc();

    if (int rc = avio_open(&d->format->pb, path.c_str(), AVIO_FLAG_WRITE); rc < 0)
        return fail("cannot write " + path + ": " + avError(rc));
    AVDictionary* options = nullptr;
    av_dict_set(&options, "movflags", "+faststart", 0);
    const int rc = avformat_write_header(d->format, &options);
    av_dict_free(&options);
    if (rc < 0)
        return fail("cannot write the file header: " + avError(rc));
    d->headerWritten = true;
    return std::unique_ptr<Mp4Writer>(new Mp4Writer(std::move(d)));
}

const Mp4Settings& Mp4Writer::settings() const
{
    return m_impl->settings;
}

bool Mp4Writer::writeVideo(const std::uint8_t* luma, int lumaStride, const std::uint8_t* chroma, int chromaStride)
{
    Impl& d = *m_impl;
    if (d.finished)
        return d.fail("the file is already finished");
    if (av_frame_make_writable(d.videoFrame) < 0)
        return d.fail("out of memory");
    const int w = d.settings.width;
    const int h = d.settings.height;
    for (int y = 0; y < h; ++y)
        std::memcpy(d.videoFrame->data[0] + std::ptrdiff_t(y) * d.videoFrame->linesize[0], luma + std::ptrdiff_t(y) * lumaStride,
                    std::size_t(w));
    for (int y = 0; y < h / 2; ++y)
        std::memcpy(d.videoFrame->data[1] + std::ptrdiff_t(y) * d.videoFrame->linesize[1],
                    chroma + std::ptrdiff_t(y) * chromaStride, std::size_t(w));
    d.videoFrame->pts = d.videoPts++;
    return d.encode(d.video, d.videoStream, d.videoFrame);
}

bool Mp4Writer::writeAudio(const float* samples, int frames)
{
    Impl& d = *m_impl;
    if (d.finished)
        return d.fail("the file is already finished");
    d.pendingAudio.insert(d.pendingAudio.end(), samples, samples + std::ptrdiff_t(frames) * audio::kChannels);
    std::size_t used = 0;
    const std::size_t frameFloats = std::size_t(kAudioFrameSize) * audio::kChannels;
    while (d.pendingAudio.size() - used >= frameFloats) {
        if (!d.writeAudioFrame(d.pendingAudio.data() + used))
            return false;
        used += frameFloats;
    }
    d.pendingAudio.erase(d.pendingAudio.begin(), d.pendingAudio.begin() + std::ptrdiff_t(used));
    return true;
}

bool Mp4Writer::finish()
{
    Impl& d = *m_impl;
    if (d.finished)
        return true;
    d.finished = true;
    // The last partial AAC frame, padded with silence.
    if (!d.pendingAudio.empty()) {
        d.pendingAudio.resize(std::size_t(kAudioFrameSize) * audio::kChannels, 0.0f);
        if (!d.writeAudioFrame(d.pendingAudio.data()))
            return false;
        d.pendingAudio.clear();
    }
    if (!d.encode(d.video, d.videoStream, nullptr) || !d.encode(d.audio, d.audioStream, nullptr))
        return false;
    if (int rc = av_write_trailer(d.format); rc < 0)
        return d.fail("cannot finish the file: " + avError(rc));
    if (int rc = avio_closep(&d.format->pb); rc < 0)
        return d.fail("cannot close the file: " + avError(rc));
    return true;
}

std::int64_t Mp4Writer::videoFramesWritten() const
{
    return m_impl->videoPts;
}

std::string Mp4Writer::lastError() const
{
    return m_impl->error;
}

} // namespace tve
