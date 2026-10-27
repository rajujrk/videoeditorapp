#include "audio/AudioRecorder.h"

#include "decode/AudioDecoder.h"
#include "encode/WavWriter.h"

#include <miniaudio.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace tve {

namespace {

// Four seconds of input: the writer drains it every few tens of milliseconds, so it only
// fills if the disk stalls for that long.
constexpr ma_uint32 kRingFrames = audio::kSampleRate * 4;
constexpr auto kWriterInterval = std::chrono::milliseconds(20);
constexpr auto kFlushInterval = std::chrono::milliseconds(500);

bool initContext(AudioRecorder::Backend backend, ma_context& context)
{
    ma_backend nullOnly[] = {ma_backend_null};
    const bool nullBackend = backend == AudioRecorder::Backend::Null;
    return ma_context_init(nullBackend ? nullOnly : nullptr, nullBackend ? 1 : 0, nullptr, &context) == MA_SUCCESS;
}

} // namespace

struct AudioRecorder::Device {
    ma_context context{};
    ma_device device{};
    ma_pcm_rb ring{};
    bool contextReady = false;
    bool deviceReady = false;
    bool ringReady = false;
};

namespace {

void captureCallback(ma_device* device, void*, const void* input, ma_uint32 frameCount)
{
    if (input)
        static_cast<AudioRecorder*>(device->pUserData)->push(static_cast<const float*>(input), int(frameCount));
}

} // namespace

std::vector<std::string> AudioRecorder::inputDevices(Backend backend)
{
    std::vector<std::string> names;
    if (backend == Backend::None)
        return names;
    ma_context context;
    if (!initContext(backend, context))
        return names;
    ma_device_info* devices = nullptr;
    ma_uint32 count = 0;
    if (ma_context_get_devices(&context, nullptr, nullptr, &devices, &count) == MA_SUCCESS) {
        for (ma_uint32 i = 0; i < count; ++i) {
            if (devices[i].isDefault)
                names.insert(names.begin(), devices[i].name);
            else
                names.emplace_back(devices[i].name);
        }
    }
    ma_context_uninit(&context);
    return names;
}

AudioRecorder::AudioRecorder(Backend backend, const std::string& deviceName) : m_device(std::make_unique<Device>())
{
    if (ma_pcm_rb_init(ma_format_f32, kChannels, kRingFrames, nullptr, nullptr, &m_device->ring) != MA_SUCCESS) {
        m_error = "cannot allocate the recording buffer";
        return;
    }
    m_device->ringReady = true;
    m_writer = std::thread([this] { writerLoop(); });

    if (backend == Backend::None) {
        m_open = true;
        m_deviceName = "None";
        return;
    }
    if (!initContext(backend, m_device->context)) {
        m_error = "cannot initialise audio";
        return;
    }
    m_device->contextReady = true;

    ma_device_config config = ma_device_config_init(ma_device_type_capture);
    config.capture.format = ma_format_f32;
    config.capture.channels = kChannels;
    config.sampleRate = audio::kSampleRate;
    config.performanceProfile = ma_performance_profile_low_latency;
    config.dataCallback = captureCallback;
    config.pUserData = this;

    ma_device_info* devices = nullptr;
    ma_uint32 count = 0;
    ma_device_id chosen{};
    if (!deviceName.empty()
        && ma_context_get_devices(&m_device->context, nullptr, nullptr, &devices, &count) == MA_SUCCESS) {
        for (ma_uint32 i = 0; i < count; ++i) {
            if (deviceName == devices[i].name) {
                chosen = devices[i].id;
                config.capture.pDeviceID = &chosen;
                break;
            }
        }
    }
    if (ma_device_init(&m_device->context, &config, &m_device->device) != MA_SUCCESS) {
        m_error = "cannot open the input device";
        return;
    }
    m_device->deviceReady = true;

    const ma_device& device = m_device->device;
    m_deviceName = device.capture.name;
    if (device.capture.internalSampleRate > 0) {
        m_latency = Ticks(device.capture.internalPeriodSizeInFrames) * kTicksPerSecond / device.capture.internalSampleRate;
        m_skipFrames = audio::samplesFromTicks(m_latency);
    }
    if (ma_device_start(&m_device->device) != MA_SUCCESS) {
        m_error = "cannot start the input device";
        return;
    }
    m_open = true;
    m_hasCallbacks = true;
}

AudioRecorder::~AudioRecorder()
{
    if (m_recording)
        stop(); // keeps what was recorded
    if (m_device->deviceReady)
        ma_device_uninit(&m_device->device); // no callbacks after this
    {
        std::lock_guard lock(m_wakeMutex);
        m_quit = true;
    }
    m_wake.notify_all();
    if (m_writer.joinable())
        m_writer.join();
    if (m_device->ringReady)
        ma_pcm_rb_uninit(&m_device->ring);
    if (m_device->contextReady)
        ma_context_uninit(&m_device->context);
}

void AudioRecorder::push(const float* samples, int frames)
{
    float peak = 0.0f;
    for (int i = 0; i < frames; ++i)
        peak = std::max(peak, std::abs(samples[i]));
    m_peak = std::min(peak, 1.0f);

    // stop() waits for m_callbackIdle after clearing m_recording, so a take never gets
    // samples once stop() has drained it. (Both are sequentially consistent.)
    m_callbackIdle = false;
    if (m_recording) {
        const std::uint32_t take = m_take;
        if (take != m_callbackTake) {
            m_callbackTake = take;
            m_skipRemaining = m_skipFrames;
        }
        const int skip = int(std::min<std::int64_t>(m_skipRemaining, frames));
        m_skipRemaining -= skip;
        const float* source = samples + skip;
        int remaining = frames - skip;
        while (remaining > 0) {
            ma_uint32 n = ma_uint32(remaining);
            void* destination = nullptr;
            if (ma_pcm_rb_acquire_write(&m_device->ring, &n, &destination) != MA_SUCCESS || n == 0)
                break;
            std::memcpy(destination, source, n * sizeof(float) * kChannels);
            ma_pcm_rb_commit_write(&m_device->ring, n);
            source += n * kChannels;
            remaining -= int(n);
        }
        m_takeFrames += frames - skip - remaining;
        m_dropped += remaining;
    }
    m_callbackIdle = true;
}

bool AudioRecorder::start(const std::string& path, std::string* error)
{
    auto fail = [&](std::string message) {
        if (error)
            *error = std::move(message);
        return false;
    };
    if (!m_open)
        return fail(m_error.empty() ? "the input device is not open" : m_error);
    if (m_recording)
        return fail("already recording");

    std::lock_guard lock(m_fileMutex);
    std::string openError;
    m_file = WavWriter::open(path, audio::kSampleRate, kChannels, &openError);
    if (!m_file)
        return fail("cannot create " + path + ": " + openError);
    m_path = path;
    m_writeFailed = false;
    ma_pcm_rb_reset(&m_device->ring); // the callback is not writing: no take is running
    m_takeFrames = 0;
    m_dropped = 0;
    ++m_take;
    m_recording = true;
    return true;
}

Ticks AudioRecorder::recordedDuration() const
{
    return audio::ticksFromSamples(m_takeFrames.load());
}

std::optional<AudioRecorder::Take> AudioRecorder::stop(std::string* error)
{
    auto fail = [&](std::string message) -> std::optional<Take> {
        if (error)
            *error = std::move(message);
        return std::nullopt;
    };
    if (!m_recording)
        return fail("not recording");
    m_recording = false;
    if (m_hasCallbacks) {
        // Let a callback that saw the take still running finish writing it.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        while (!m_callbackIdle && std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
    }

    std::lock_guard lock(m_fileMutex);
    drain();
    const bool written = m_file->finish() && !m_writeFailed;
    const std::int64_t frames = m_file->frames();
    m_file.reset();
    if (!written)
        return fail("cannot write " + m_path);
    if (frames == 0) {
        std::remove(m_path.c_str());
        return fail("nothing was recorded");
    }
    return Take{.path = m_path, .duration = audio::ticksFromSamples(frames)};
}

void AudioRecorder::drain()
{
    if (!m_file)
        return;
    for (;;) {
        ma_uint32 n = kRingFrames;
        void* source = nullptr;
        if (ma_pcm_rb_acquire_read(&m_device->ring, &n, &source) != MA_SUCCESS || n == 0)
            break;
        if (!m_file->write(static_cast<const float*>(source), n))
            m_writeFailed = true;
        ma_pcm_rb_commit_read(&m_device->ring, n);
    }
}

void AudioRecorder::writerLoop()
{
    auto lastFlush = std::chrono::steady_clock::now();
    for (;;) {
        {
            std::unique_lock lock(m_wakeMutex);
            if (m_wake.wait_for(lock, kWriterInterval, [this] { return m_quit; }))
                return;
        }
        std::lock_guard lock(m_fileMutex);
        if (!m_file)
            continue;
        drain();
        // Keep the header current, so a crash mid-take leaves a playable file.
        const auto now = std::chrono::steady_clock::now();
        if (now - lastFlush >= kFlushInterval) {
            if (!m_file->flush())
                m_writeFailed = true;
            lastFlush = now;
        }
    }
}

} // namespace tve
