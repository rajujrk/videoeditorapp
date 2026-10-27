#include "audio/AudioOutput.h"

#include "playback/PlaybackClock.h"

#include <miniaudio.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace tve {

namespace {

// Drift beyond this between the audio cursor and the clock causes a realignment (a
// small jump); below it, audio plays continuously.
constexpr Ticks kResyncThreshold = kTicksPerSecond / 25; // 40 ms

Ticks alignToSample(Ticks time)
{
    return audio::ticksFromSamples(audio::samplesFromTicks(time));
}

} // namespace

struct AudioOutput::Device {
    ma_context context{};
    ma_device device{};
    bool contextReady = false;
    bool deviceReady = false;
};

namespace {

void dataCallback(ma_device* device, void* output, const void*, ma_uint32 frameCount)
{
    static_cast<AudioOutput*>(device->pUserData)->render(static_cast<float*>(output), int(frameCount));
}

} // namespace

AudioOutput::AudioOutput(Backend backend) : m_device(std::make_unique<Device>())
{
    if (backend == Backend::None)
        return;
    const bool nullBackend = backend == Backend::Null;
    ma_backend nullOnly[] = {ma_backend_null};
    if (ma_context_init(nullBackend ? nullOnly : nullptr, nullBackend ? 1 : 0, nullptr, &m_device->context)
        != MA_SUCCESS) {
        m_error = "cannot initialise audio";
        return;
    }
    m_device->contextReady = true;

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = audio::kChannels;
    config.sampleRate = audio::kSampleRate;
    config.performanceProfile = ma_performance_profile_low_latency;
    config.dataCallback = dataCallback;
    config.pUserData = this;
    if (ma_device_init(&m_device->context, &config, &m_device->device) != MA_SUCCESS) {
        m_error = "cannot open the audio output device";
        return;
    }
    m_device->deviceReady = true;

    const ma_device& device = m_device->device;
    m_deviceName = device.playback.name;
    if (device.playback.internalSampleRate > 0) {
        const std::int64_t bufferedFrames =
            std::int64_t(device.playback.internalPeriodSizeInFrames) * device.playback.internalPeriods;
        m_latency = bufferedFrames * kTicksPerSecond / device.playback.internalSampleRate;
    }
    if (ma_device_start(&m_device->device) == MA_SUCCESS)
        m_running = true;
    else
        m_error = "cannot start the audio output device";
}

AudioOutput::~AudioOutput()
{
    if (m_device->deviceReady)
        ma_device_uninit(&m_device->device); // stops the callback before members go away
    if (m_device->contextReady)
        ma_context_uninit(&m_device->context);
}

void AudioOutput::setSource(std::shared_ptr<TimelineAudio> audio, PlaybackClock* clock)
{
    std::lock_guard lock(m_sourceMutex);
    m_audio = std::move(audio);
    m_clock = clock;
    m_current.reset();
    m_cursor.reset();
}

void AudioOutput::render(float* out, int frames)
{
    std::memset(out, 0, std::size_t(frames) * audio::kChannels * sizeof(float));

    std::unique_lock lock(m_sourceMutex, std::try_to_lock);
    if (!lock.owns_lock() || !m_audio || !m_clock || !m_clock->playing()) {
        m_cursor.reset(); // realign when playback resumes
        m_peakLeft = 0.0f;
        m_peakRight = 0.0f;
        return;
    }

    const std::uint64_t generation = m_audio->generation();
    if (m_current && m_current->generation != generation)
        m_current.reset();

    // The samples written now are heard `latency` from now.
    const Ticks target = alignToSample(m_clock->now() + m_latency);
    if (!m_cursor || std::llabs(*m_cursor - target) > kResyncThreshold)
        m_cursor = target;
    Ticks cursor = *m_cursor;

    int written = 0;
    while (written < frames) {
        if (!m_current) {
            auto chunk = m_audio->queue().tryPop();
            if (!chunk) {
                if (!m_audio->producerFinished())
                    ++m_underruns;
                break; // silence for the rest of this callback
            }
            if (chunk->generation != generation)
                continue;
            m_current = std::move(chunk);
        }
        if (m_current->end() <= cursor) {
            m_current.reset(); // already past it
            continue;
        }
        if (m_current->pts > cursor) {
            // Nothing plays until this chunk starts (a gap, or just after a seek).
            const int gap = int(std::min<std::int64_t>(frames - written, audio::samplesFromTicks(m_current->pts - cursor)));
            written += gap;
            cursor += audio::ticksFromSamples(gap);
            continue;
        }
        const int offset = int(audio::samplesFromTicks(cursor - m_current->pts));
        const int n = std::min(AudioChunk::kFrames - offset, frames - written);
        std::memcpy(out + written * audio::kChannels, m_current->samples.data() + offset * audio::kChannels,
                    std::size_t(n) * audio::kChannels * sizeof(float));
        written += n;
        cursor += audio::ticksFromSamples(n);
    }
    cursor += audio::ticksFromSamples(frames - written);
    m_cursor = cursor;

    if (m_muted) {
        std::memset(out, 0, std::size_t(frames) * audio::kChannels * sizeof(float));
        m_peakLeft = 0.0f;
        m_peakRight = 0.0f;
        return;
    }
    float peakL = 0.0f;
    float peakR = 0.0f;
    for (int i = 0; i < frames; ++i) {
        float& l = out[i * 2];
        float& r = out[i * 2 + 1];
        l = std::clamp(l, -1.0f, 1.0f);
        r = std::clamp(r, -1.0f, 1.0f);
        peakL = std::max(peakL, std::abs(l));
        peakR = std::max(peakR, std::abs(r));
    }
    m_peakLeft = peakL;
    m_peakRight = peakR;
}

} // namespace tve
