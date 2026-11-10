#include "encode/MediaWriter.h"

#include "decode/AudioDecoder.h"
#include "encode/GifPalette.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/mastering_display_metadata.h>
#include <libavutil/opt.h>
#include <libavutil/spherical.h>
#include <libavutil/stereo3d.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <cmath>
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

const char* muxerName(Container container)
{
    switch (container) {
    case Container::Mp4: return "mp4";
    case Container::Mov: return "mov";
    case Container::Gif: return "gif";
    case Container::M4a: return "ipod";
    case Container::Wav: return "wav";
    case Container::Flac: return "flac";
    case Container::Mp3: return "mp3";
    }
    return "mp4";
}

bool takesChapters(Container container)
{
    return container == Container::Mp4 || container == Container::Mov || container == Container::M4a
        || container == Container::Mp3;
}

const char* proResName(ProResProfile profile)
{
    switch (profile) {
    case ProResProfile::Proxy: return "proxy";
    case ProResProfile::Lt: return "lt";
    case ProResProfile::Standard: return "standard";
    case ProResProfile::Hq: return "hq";
    case ProResProfile::P4444: return "4444";
    }
    return "hq";
}

// Published ProRes rates at 1920 × 1080, 29.97 fps (Mbit/s).
double proResMegabits(ProResProfile profile)
{
    switch (profile) {
    case ProResProfile::Proxy: return 45;
    case ProResProfile::Lt: return 102;
    case ProResProfile::Standard: return 147;
    case ProResProfile::Hq: return 220;
    case ProResProfile::P4444: return 330;
    }
    return 220;
}

} // namespace

const char* fileExtension(Container container)
{
    switch (container) {
    case Container::Mp4: return "mp4";
    case Container::Mov: return "mov";
    case Container::Gif: return "gif";
    case Container::M4a: return "m4a";
    case Container::Wav: return "wav";
    case Container::Flac: return "flac";
    case Container::Mp3: return "mp3";
    }
    return "mp4";
}

PictureInput pictureInput(const MediaSettings& settings)
{
    if (!settings.hasVideo())
        return PictureInput::None;
    if (settings.codec == VideoCodec::Hevc && isHdr(settings.colour))
        return PictureInput::P010;
    return settings.codec == VideoCodec::H264 || settings.codec == VideoCodec::Hevc ? PictureInput::Nv12
                                                                                    : PictureInput::Rgba;
}

std::optional<std::string> validateSettings(const MediaSettings& s)
{
    if (s.hasVideo()) {
        if (s.width <= 0 || s.height <= 0)
            return "the picture size must be positive";
        if (s.container == Container::Gif ? s.codec != VideoCodec::Gif
            : s.container == Container::Mp4 ? s.codec != VideoCodec::H264 && s.codec != VideoCodec::Hevc
                                            : s.codec == VideoCodec::Gif)
            return "that picture format does not go in that file type";
        if (s.codec != VideoCodec::Gif && (s.width % 2 || s.height % 2))
            return "the picture size must be even and positive";
        if (s.codec == VideoCodec::Gif && (s.width > 4096 || s.height > 4096))
            return "a GIF can be at most 4096 pixels across";
        if (s.frameRate.num <= 0 || s.frameRate.den <= 0)
            return "the frame rate must be positive";
        if (isHdr(s.colour) && s.codec != VideoCodec::Hevc)
            return "HDR goes out as 10-bit HEVC";
        if (s.spherical && s.container != Container::Mp4)
            return "360° video goes out as MP4";
    }
    if (!s.hasVideo() && !s.audio)
        return "a sound file needs its sound";
    if (s.hasAudio() || !s.hasVideo()) {
        const AudioCodec a = s.audioCodec;
        const bool fits = s.container == Container::Mp4 || s.container == Container::M4a ? a == AudioCodec::Aac
                        : s.container == Container::Mov ? a != AudioCodec::Flac
                        : s.container == Container::Wav ? a == AudioCodec::Pcm16 || a == AudioCodec::Pcm24
                        : s.container == Container::Flac ? a == AudioCodec::Flac
                        : s.container == Container::Mp3 ? a == AudioCodec::Mp3
                                                         : true;
        if (a == AudioCodec::Mp3 && s.container != Container::Mp3)
            return "MP3 sound goes in an MP3 file";
        if (!fits)
            return "that sound format does not go in that file type";
    }
    return std::nullopt;
}

std::int64_t defaultVideoBitrate(const MediaSettings& settings)
{
    const double fps = double(settings.frameRate.num) / double(settings.frameRate.den);
    const double pixels = double(settings.width) * settings.height;
    switch (settings.codec) {
    case VideoCodec::H264: return std::int64_t(0.15 * pixels * fps);
    case VideoCodec::Hevc: // (HDR's 10-bit PQ or HLG a quarter more)
        return std::int64_t(0.15 * pixels * fps * 2.0 / 3.0 * (isHdr(settings.colour) ? 1.25 : 1.0));
    case VideoCodec::ProRes:
        return std::int64_t(proResMegabits(settings.proRes) * 1e6 * (pixels / (1920.0 * 1080.0)) * (fps / 29.97));
    case VideoCodec::Gif: return std::int64_t(1.2 * pixels * fps);
    }
    return 0;
}

std::int64_t audioBitrate(const MediaSettings& settings)
{
    if (!settings.hasAudio() && settings.hasVideo())
        return 0;
    switch (settings.audioCodec) {
    case AudioCodec::Aac:
    case AudioCodec::Mp3: return settings.audioBitrate;
    case AudioCodec::Pcm16: return std::int64_t(audio::kSampleRate) * audio::kChannels * 16;
    case AudioCodec::Pcm24: return std::int64_t(audio::kSampleRate) * audio::kChannels * 24;
    case AudioCodec::Flac: return std::int64_t(audio::kSampleRate) * audio::kChannels * 16 * 6 / 10;
    }
    return 0;
}

struct MediaWriter::Impl {
    MediaSettings settings;
    std::string path;
    AVFormatContext* format = nullptr;
    AVCodecContext* video = nullptr;
    AVCodecContext* audio = nullptr;
    AVStream* videoStream = nullptr;
    AVStream* audioStream = nullptr;
    AVFrame* videoFrame = nullptr;
    AVFrame* audioFrame = nullptr;
    AVPacket* packet = nullptr;
    SwsContext* converter = nullptr; // RGBA to the software ProRes encoder's YUV
    std::int64_t videoPts = 0;
    std::int64_t audioPts = 0;
    double maxCll = 0.0; // PQ: the content light levels so far (nits)
    double maxFall = 0.0;
    int audioFrameSize = 1024;
    bool shortLastFrame = false;      // the encoder takes a smaller last frame (else: padded)
    std::vector<float> pendingAudio;  // interleaved, less than one encoder frame
    bool headerWritten = false;
    bool finished = false;
    std::string error;

    ~Impl()
    {
        sws_freeContext(converter);
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

    // `frames` interleaved float frames (at most one encoder frame) in the encoder's format.
    bool writeAudioFrame(const float* interleaved, int frames)
    {
        if (av_frame_make_writable(audioFrame) < 0)
            return fail("out of memory");
        audioFrame->nb_samples = frames;
        const int count = frames * audio::kChannels;
        switch (audio->sample_fmt) {
        case AV_SAMPLE_FMT_FLTP: {
            auto* left = reinterpret_cast<float*>(audioFrame->data[0]);
            auto* right = reinterpret_cast<float*>(audioFrame->data[1]);
            for (int i = 0; i < frames; ++i) {
                left[i] = interleaved[i * 2];
                right[i] = interleaved[i * 2 + 1];
            }
            break;
        }
        case AV_SAMPLE_FMT_S16: {
            auto* out = reinterpret_cast<std::int16_t*>(audioFrame->data[0]);
            for (int i = 0; i < count; ++i)
                out[i] = std::int16_t(std::lrint(std::clamp(interleaved[i], -1.0f, 1.0f) * 32767.0f));
            break;
        }
        default: { // S32 holding 24 bits in its top bytes
            auto* out = reinterpret_cast<std::int32_t*>(audioFrame->data[0]);
            for (int i = 0; i < count; ++i)
                out[i] = std::int32_t(std::lrint(double(std::clamp(interleaved[i], -1.0f, 1.0f)) * 8388607.0)) * 256;
            break;
        }
        }
        audioFrame->pts = audioPts;
        audioPts += frames;
        return encode(audio, audioStream, audioFrame);
    }

    bool openVideo()
    {
        const MediaSettings& s = settings;
        const AVCodec* codec = nullptr;
        const bool hdr = s.codec == VideoCodec::Hevc && isHdr(s.colour);
        AVPixelFormat pixels = hdr ? AV_PIX_FMT_P010LE : AV_PIX_FMT_NV12;
        const AVColorPrimaries primaries = hdr ? AVCOL_PRI_BT2020 : AVCOL_PRI_BT709;
        const AVColorTransferCharacteristic transfer = !hdr ? AVCOL_TRC_BT709
                                                     : s.colour == ProjectColour::HdrPq ? AVCOL_TRC_SMPTE2084
                                                                                        : AVCOL_TRC_ARIB_STD_B67;
        const AVColorSpace matrix = hdr ? AVCOL_SPC_BT2020_NCL : AVCOL_SPC_BT709;
        switch (s.codec) {
        case VideoCodec::H264: codec = avcodec_find_encoder_by_name("h264_videotoolbox"); break;
        case VideoCodec::Hevc: codec = avcodec_find_encoder_by_name("hevc_videotoolbox"); break;
        case VideoCodec::ProRes:
            codec = avcodec_find_encoder_by_name("prores_videotoolbox");
            pixels = AV_PIX_FMT_BGRA;
            break;
        case VideoCodec::Gif:
            codec = avcodec_find_encoder(AV_CODEC_ID_GIF);
            pixels = AV_PIX_FMT_PAL8;
            break;
        }
        // ProRes: VideoToolbox's encoder where the system has one, else FFmpeg's own.
        // With transparency kept, FFmpeg's own (its 4444 has an alpha channel).
        const bool withAlpha = s.codec == VideoCodec::ProRes && s.proRes == ProResProfile::P4444 && s.alpha;
        for (int attempt = withAlpha ? 1 : 0; attempt < 2; ++attempt) {
            if (attempt == 1) {
                if (s.codec != VideoCodec::ProRes)
                    break;
                avcodec_free_context(&video);
                codec = avcodec_find_encoder_by_name("prores_ks");
                pixels = withAlpha ? AV_PIX_FMT_YUVA444P10LE
                       : s.proRes == ProResProfile::P4444 ? AV_PIX_FMT_YUV444P10LE : AV_PIX_FMT_YUV422P10LE;
            }
            if (!codec)
                continue;
            video = avcodec_alloc_context3(codec);
            video->width = s.width;
            video->height = s.height;
            video->pix_fmt = pixels;
            video->time_base = AVRational{s.frameRate.den, s.frameRate.num};
            video->framerate = AVRational{s.frameRate.num, s.frameRate.den};
            video->color_primaries = primaries;
            video->color_trc = transfer;
            video->colorspace = matrix;
            // RGB frames are full range (VideoToolbox refuses BGRA said to be video range).
            video->color_range = pixels == AV_PIX_FMT_BGRA ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;
            if (format->oformat->flags & AVFMT_GLOBALHEADER)
                video->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
            if (s.codec == VideoCodec::H264 || s.codec == VideoCodec::Hevc) {
                video->bit_rate = s.videoBitrate > 0 ? s.videoBitrate : defaultVideoBitrate(s);
                video->gop_size = s.keyframeInterval > 0
                                      ? s.keyframeInterval
                                      : std::max(1, 2 * s.frameRate.num / std::max(1, s.frameRate.den)); // 2 s
                av_opt_set_int(video->priv_data, "allow_sw", 1, 0); // fall back if the hardware is busy
                if (s.codec == VideoCodec::Hevc)
                    video->codec_tag = MKTAG('h', 'v', 'c', '1'); // so QuickTime and Finder play it
                if (hdr)
                    av_opt_set(video->priv_data, "profile", "main10", 0);
            }
            if (s.codec == VideoCodec::ProRes)
                av_opt_set(video->priv_data, "profile", proResName(s.proRes), 0);
            if (withAlpha)
                av_opt_set_int(video->priv_data, "alpha_bits", 16, 0);
            if (avcodec_open2(video, codec, nullptr) >= 0)
                break;
            avcodec_free_context(&video);
        }
        if (!video)
            return fail(s.codec == VideoCodec::ProRes ? "no ProRes encoder" : "cannot open the picture encoder");
        videoStream = avformat_new_stream(format, nullptr);
        avcodec_parameters_from_context(videoStream->codecpar, video);
        // 360°: an equirectangular sphere, one picture for both eyes.
        if (s.spherical) {
            AVCodecParameters* par = videoStream->codecpar;
            std::size_t size = 0;
            AVSphericalMapping* mapping = av_spherical_alloc(&size);
            AVPacketSideData* data = mapping ? av_packet_side_data_add(&par->coded_side_data, &par->nb_coded_side_data,
                                                                       AV_PKT_DATA_SPHERICAL, mapping, size, 0)
                                             : nullptr;
            if (!data) {
                av_free(mapping);
                return fail("out of memory");
            }
            mapping->projection = AV_SPHERICAL_EQUIRECTANGULAR;
            AVStereo3D* stereo = av_stereo3d_alloc_size(&size);
            if (!stereo || !av_packet_side_data_add(&par->coded_side_data, &par->nb_coded_side_data, AV_PKT_DATA_STEREO3D,
                                                    stereo, size, 0)) {
                av_free(stereo);
                return fail("out of memory");
            }
            stereo->type = AV_STEREO3D_2D;
        }
        // HDR: the mastering display, in the file (and with each frame, for the encoder).
        AVMasteringDisplayMetadata* mastering = nullptr;
        if (hdr) {
            AVCodecParameters* par = videoStream->codecpar;
            AVPacketSideData* data = av_packet_side_data_new(&par->coded_side_data, &par->nb_coded_side_data,
                                                             AV_PKT_DATA_MASTERING_DISPLAY_METADATA,
                                                             sizeof(AVMasteringDisplayMetadata), 0);
            if (!data)
                return fail("out of memory");
            mastering = reinterpret_cast<AVMasteringDisplayMetadata*>(data->data);
            std::memset(mastering, 0, sizeof *mastering);
            const auto q = [](double v) { return AVRational{int(std::lround(v * 50000)), 50000}; };
            const double xy[3][2] = {{0.708, 0.292}, {0.170, 0.797}, {0.131, 0.046}}; // Rec.2020 R, G, B
            for (int i = 0; i < 3; ++i) {
                mastering->display_primaries[i][0] = q(xy[i][0]);
                mastering->display_primaries[i][1] = q(xy[i][1]);
            }
            mastering->white_point[0] = q(0.3127);
            mastering->white_point[1] = q(0.3290);
            mastering->min_luminance = AVRational{1, 10000};
            mastering->max_luminance = AVRational{int(kHdrPeakNits), 1};
            mastering->has_primaries = 1;
            mastering->has_luminance = 1;
        }
        videoStream->time_base = video->time_base;
        videoStream->avg_frame_rate = video->framerate;

        videoFrame = av_frame_alloc();
        videoFrame->format = video->pix_fmt;
        videoFrame->width = s.width;
        videoFrame->height = s.height;
        videoFrame->color_range = video->color_range;
        videoFrame->colorspace = matrix;
        videoFrame->color_primaries = primaries;
        videoFrame->color_trc = transfer;
        if (av_frame_get_buffer(videoFrame, 0) < 0)
            return fail("out of memory");
        if (mastering) {
            AVMasteringDisplayMetadata* own = av_mastering_display_metadata_create_side_data(videoFrame);
            if (!own)
                return fail("out of memory");
            *own = *mastering;
        }
        if (video->pix_fmt == AV_PIX_FMT_YUV422P10LE || video->pix_fmt == AV_PIX_FMT_YUV444P10LE
            || video->pix_fmt == AV_PIX_FMT_YUVA444P10LE) {
            converter = sws_getContext(s.width, s.height, AV_PIX_FMT_RGBA, s.width, s.height, video->pix_fmt,
                                       SWS_BICUBIC, nullptr, nullptr, nullptr);
            if (!converter)
                return fail("cannot convert pictures for ProRes");
            const int* bt709 = sws_getCoefficients(SWS_CS_ITU709);
            sws_setColorspaceDetails(converter, bt709, 1, bt709, 0, 0, 1 << 16, 1 << 16);
        }
        return true;
    }

    bool openAudio()
    {
        const MediaSettings& s = settings;
        AVCodecID id = AV_CODEC_ID_AAC;
        AVSampleFormat sampleFormat = AV_SAMPLE_FMT_FLTP;
        switch (s.audioCodec) {
        case AudioCodec::Aac: break;
        case AudioCodec::Pcm16: id = AV_CODEC_ID_PCM_S16LE; sampleFormat = AV_SAMPLE_FMT_S16; break;
        case AudioCodec::Pcm24: id = AV_CODEC_ID_PCM_S24LE; sampleFormat = AV_SAMPLE_FMT_S32; break;
        case AudioCodec::Flac: id = AV_CODEC_ID_FLAC; sampleFormat = AV_SAMPLE_FMT_S32; break;
        case AudioCodec::Mp3: id = AV_CODEC_ID_MP3; break; // LAME (libmp3lame), planar float
        }
        const AVCodec* codec = avcodec_find_encoder(id);
        if (!codec)
            return fail("no encoder for that sound format");
        audio = avcodec_alloc_context3(codec);
        audio->sample_fmt = sampleFormat;
        audio->sample_rate = audio::kSampleRate;
        av_channel_layout_default(&audio->ch_layout, audio::kChannels);
        audio->time_base = AVRational{1, audio::kSampleRate};
        if (s.audioCodec == AudioCodec::Aac || s.audioCodec == AudioCodec::Mp3)
            audio->bit_rate = s.audioBitrate;
        if (s.audioCodec == AudioCodec::Flac)
            audio->bits_per_raw_sample = 24;
        if (format->oformat->flags & AVFMT_GLOBALHEADER)
            audio->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        if (int rc = avcodec_open2(audio, codec, nullptr); rc < 0)
            return fail("cannot open the sound encoder: " + avError(rc));
        audioFrameSize = audio->frame_size > 0 ? audio->frame_size : 1024;
        shortLastFrame = (codec->capabilities & (AV_CODEC_CAP_SMALL_LAST_FRAME | AV_CODEC_CAP_VARIABLE_FRAME_SIZE)) != 0;
        audioStream = avformat_new_stream(format, nullptr);
        avcodec_parameters_from_context(audioStream->codecpar, audio);
        audioStream->time_base = audio->time_base;

        audioFrame = av_frame_alloc();
        audioFrame->format = audio->sample_fmt;
        audioFrame->sample_rate = audio::kSampleRate;
        audioFrame->nb_samples = audioFrameSize;
        av_channel_layout_copy(&audioFrame->ch_layout, &audio->ch_layout);
        if (av_frame_get_buffer(audioFrame, 0) < 0)
            return fail("out of memory");
        return true;
    }

    // Writes whole encoder frames from the pending sound (and, at the end, what is left).
    bool drainAudio(bool last)
    {
        const std::size_t frameFloats = std::size_t(audioFrameSize) * audio::kChannels;
        std::size_t used = 0;
        while (pendingAudio.size() - used >= frameFloats) {
            if (!writeAudioFrame(pendingAudio.data() + used, audioFrameSize))
                return false;
            used += frameFloats;
        }
        pendingAudio.erase(pendingAudio.begin(), pendingAudio.begin() + std::ptrdiff_t(used));
        if (last && !pendingAudio.empty()) {
            const int frames = int(pendingAudio.size() / audio::kChannels);
            if (!shortLastFrame)
                pendingAudio.resize(frameFloats, 0.0f); // padded with silence
            if (!writeAudioFrame(pendingAudio.data(), shortLastFrame ? frames : audioFrameSize))
                return false;
            pendingAudio.clear();
        }
        return true;
    }
};

MediaWriter::MediaWriter(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}

MediaWriter::~MediaWriter() = default;

std::unique_ptr<MediaWriter> MediaWriter::open(const std::string& path, const MediaSettings& settings, std::string* error)
{
    auto d = std::make_unique<Impl>();
    d->settings = settings;
    d->path = path;
    const auto fail = [&](const std::string& message) -> std::unique_ptr<MediaWriter> {
        if (error)
            *error = message;
        return nullptr;
    };
    if (auto invalid = validateSettings(settings))
        return fail(*invalid);

    if (int rc = avformat_alloc_output_context2(&d->format, nullptr, muxerName(settings.container), path.c_str()); rc < 0)
        return fail("cannot create " + path + ": " + avError(rc));
    // FFmpeg writes MP4's spherical boxes (Google's, not ISO's) only when told it may.
    if (settings.spherical)
        d->format->strict_std_compliance = FF_COMPLIANCE_UNOFFICIAL;
    if (settings.hasVideo() && !d->openVideo())
        return fail(d->error);
    if ((settings.hasAudio() || !settings.hasVideo()) && !d->openAudio())
        return fail(d->error);
    d->packet = av_packet_alloc();

    if (int rc = avio_open(&d->format->pb, path.c_str(), AVIO_FLAG_WRITE); rc < 0)
        return fail("cannot write " + path + ": " + avError(rc));
    // Chapters go in before the header (the muxer makes its chapter track from them).
    if (takesChapters(settings.container)) {
        for (const Chapter& chapter : settings.chapters) {
            auto* added = static_cast<AVChapter*>(av_mallocz(sizeof(AVChapter)));
            if (!added)
                return fail("out of memory");
            added->id = std::int64_t(d->format->nb_chapters);
            added->time_base = AVRational{1, int(kTicksPerSecond)};
            added->start = chapter.start;
            added->end = chapter.end;
            av_dict_set(&added->metadata, "title", chapter.title.c_str(), 0);
            int count = int(d->format->nb_chapters);
            av_dynarray_add(&d->format->chapters, &count, added);
            if (!d->format->chapters)
                return fail("out of memory");
            d->format->nb_chapters = unsigned(count);
        }
    }
    AVDictionary* options = nullptr;
    if (takesChapters(settings.container))
        av_dict_set(&options, "movflags", "+faststart", 0);
    if (settings.container == Container::Gif)
        av_dict_set(&options, "loop", "0", 0); // forever
    const int rc = avformat_write_header(d->format, &options);
    av_dict_free(&options);
    if (rc < 0)
        return fail("cannot write the file header: " + avError(rc));
    d->headerWritten = true;
    return std::unique_ptr<MediaWriter>(new MediaWriter(std::move(d)));
}

const MediaSettings& MediaWriter::settings() const
{
    return m_impl->settings;
}

bool MediaWriter::writeVideo(const std::uint8_t* luma, int lumaStride, const std::uint8_t* chroma, int chromaStride)
{
    Impl& d = *m_impl;
    if (d.finished)
        return d.fail("the file is already finished");
    if (pictureInput(d.settings) != PictureInput::Nv12)
        return d.fail("this file takes pictures as RGBA");
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

bool MediaWriter::writeVideoP010(const std::uint16_t* luma, int lumaStride, const std::uint16_t* chroma, int chromaStride)
{
    Impl& d = *m_impl;
    if (d.finished)
        return d.fail("the file is already finished");
    if (pictureInput(d.settings) != PictureInput::P010)
        return d.fail("this file does not take HDR pictures");
    if (av_frame_make_writable(d.videoFrame) < 0)
        return d.fail("out of memory");
    const int w = d.settings.width;
    const int h = d.settings.height;
    AVFrame* frame = d.videoFrame;
    for (int y = 0; y < h; ++y)
        std::memcpy(frame->data[0] + std::ptrdiff_t(y) * frame->linesize[0], luma + std::ptrdiff_t(y) * lumaStride,
                    std::size_t(w) * 2);
    for (int y = 0; y < h / 2; ++y)
        std::memcpy(frame->data[1] + std::ptrdiff_t(y) * frame->linesize[1], chroma + std::ptrdiff_t(y) * chromaStride,
                    std::size_t(w) * 2);
    // PQ: how bright it gets, from every fourth pixel each way (each 2 × 2 block's chroma).
    if (d.settings.colour == ProjectColour::HdrPq) {
        double sum = 0.0;
        int count = 0;
        for (int y = 0; y + 1 < h; y += 4) {
            for (int x = 0; x + 1 < w; x += 4) {
                const double yy = (double(luma[std::ptrdiff_t(y) * lumaStride + x] >> 6) - 64.0) / 876.0;
                const std::uint16_t* c = chroma + std::ptrdiff_t(y / 2) * chromaStride + (x / 2) * 2;
                const double cb = (double(c[0] >> 6) - 512.0) / 896.0;
                const double cr = (double(c[1] >> 6) - 512.0) / 896.0;
                const double r = yy + 1.4746 * cr;
                const double g = yy - 0.16455 * cb - 0.57135 * cr;
                const double b = yy + 1.8814 * cb;
                const double nits = nitsFromPq(std::clamp(std::max({r, g, b}), 0.0, 1.0));
                d.maxCll = std::max(d.maxCll, nits);
                sum += nits;
                ++count;
            }
        }
        if (count > 0)
            d.maxFall = std::max(d.maxFall, sum / count);
    }
    frame->pts = d.videoPts++;
    return d.encode(d.video, d.videoStream, frame);
}

bool MediaWriter::writeVideoRgba(const std::uint8_t* rgba, int stride)
{
    Impl& d = *m_impl;
    if (d.finished)
        return d.fail("the file is already finished");
    if (pictureInput(d.settings) != PictureInput::Rgba)
        return d.fail("this file takes pictures as NV12");
    if (av_frame_make_writable(d.videoFrame) < 0)
        return d.fail("out of memory");
    const int w = d.settings.width;
    const int h = d.settings.height;
    AVFrame* frame = d.videoFrame;
    switch (d.video->pix_fmt) {
    case AV_PIX_FMT_BGRA:
        for (int y = 0; y < h; ++y) {
            const std::uint8_t* in = rgba + std::ptrdiff_t(y) * stride;
            std::uint8_t* out = frame->data[0] + std::ptrdiff_t(y) * frame->linesize[0];
            for (int x = 0; x < w; ++x, in += 4, out += 4) {
                out[0] = in[2];
                out[1] = in[1];
                out[2] = in[0];
                out[3] = 255;
            }
        }
        break;
    case AV_PIX_FMT_PAL8: {
        GifPalette palette;
        quantizeFrame(rgba, w, h, stride, frame->data[0], frame->linesize[0], palette);
        std::memcpy(frame->data[1], palette.data(), sizeof palette);
        break;
    }
    default: { // software ProRes
        const std::uint8_t* source[1] = {rgba};
        const int strides[1] = {stride};
        sws_scale(d.converter, source, strides, 0, h, frame->data, frame->linesize);
        break;
    }
    }
    frame->pts = d.videoPts++;
    return d.encode(d.video, d.videoStream, frame);
}

bool MediaWriter::writeAudio(const float* samples, int frames)
{
    Impl& d = *m_impl;
    if (d.finished)
        return d.fail("the file is already finished");
    if (!d.audio)
        return d.fail("this file has no sound track");
    d.pendingAudio.insert(d.pendingAudio.end(), samples, samples + std::ptrdiff_t(frames) * audio::kChannels);
    return d.drainAudio(false);
}

bool MediaWriter::finish()
{
    Impl& d = *m_impl;
    if (d.finished)
        return true;
    d.finished = true;
    if (d.audio && !d.drainAudio(true))
        return false;
    if ((d.video && !d.encode(d.video, d.videoStream, nullptr)) || (d.audio && !d.encode(d.audio, d.audioStream, nullptr)))
        return false;
    // The content light levels, now known, go in the file's index (written below).
    if (d.video && d.settings.codec == VideoCodec::Hevc && d.settings.colour == ProjectColour::HdrPq) {
        AVCodecParameters* par = d.videoStream->codecpar;
        AVPacketSideData* data = av_packet_side_data_new(&par->coded_side_data, &par->nb_coded_side_data,
                                                         AV_PKT_DATA_CONTENT_LIGHT_LEVEL, sizeof(AVContentLightMetadata), 0);
        if (!data)
            return d.fail("out of memory");
        auto* levels = reinterpret_cast<AVContentLightMetadata*>(data->data);
        levels->MaxCLL = unsigned(std::lround(std::min(d.maxCll, 10000.0)));
        levels->MaxFALL = unsigned(std::lround(std::min(d.maxFall, 10000.0)));
    }
    if (int rc = av_write_trailer(d.format); rc < 0)
        return d.fail("cannot finish the file: " + avError(rc));
    if (int rc = avio_closep(&d.format->pb); rc < 0)
        return d.fail("cannot close the file: " + avError(rc));
    return true;
}

std::int64_t MediaWriter::videoFramesWritten() const
{
    return m_impl->videoPts;
}

std::pair<double, double> MediaWriter::contentLightLevels() const
{
    return {m_impl->maxCll, m_impl->maxFall};
}

std::string MediaWriter::videoEncoder() const
{
    return m_impl->video ? m_impl->video->codec->name : "";
}

std::string MediaWriter::lastError() const
{
    return m_impl->error;
}

} // namespace tve
