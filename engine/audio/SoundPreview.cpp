#include "audio/SoundPreview.h"

#include <miniaudio.h>

#include <algorithm>

namespace tve {

struct SoundPreview::Device {
    ma_context context{};
    ma_device device{};
    bool contextReady = false;
    bool deviceReady = false;
};

namespace {

void previewCallback(ma_device* device, void* output, const void*, ma_uint32 frameCount)
{
    static_cast<SoundPreview*>(device->pUserData)->render(static_cast<float*>(output), int(frameCount));
}

} // namespace

SoundPreview::SoundPreview(AudioOutput::Backend backend) : m_device(std::make_unique<Device>()), m_backend(backend) {}

SoundPreview::~SoundPreview()
{
    stop();
}

bool SoundPreview::play(std::vector<float> samples)
{
    stop();
    {
        std::lock_guard lock(m_mutex);
        m_samples = std::move(samples);
        m_position = 0;
    }
    m_playing = !m_samples.empty();
    if (!m_playing || m_backend == AudioOutput::Backend::None)
        return m_playing;
    const bool nullBackend = m_backend == AudioOutput::Backend::Null;
    ma_backend nullOnly[] = {ma_backend_null};
    if (ma_context_init(nullBackend ? nullOnly : nullptr, nullBackend ? 1 : 0, nullptr, &m_device->context) != MA_SUCCESS) {
        m_playing = false;
        return false;
    }
    m_device->contextReady = true;
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = audio::kChannels;
    config.sampleRate = audio::kSampleRate;
    config.dataCallback = previewCallback;
    config.pUserData = this;
    if (ma_device_init(&m_device->context, &config, &m_device->device) != MA_SUCCESS) {
        stop();
        return false;
    }
    m_device->deviceReady = true;
    if (ma_device_start(&m_device->device) != MA_SUCCESS) {
        stop();
        return false;
    }
    return true;
}

void SoundPreview::stop()
{
    if (m_device->deviceReady)
        ma_device_uninit(&m_device->device);
    if (m_device->contextReady)
        ma_context_uninit(&m_device->context);
    m_device->deviceReady = m_device->contextReady = false;
    m_playing = false;
}

double SoundPreview::progress() const
{
    auto* self = const_cast<SoundPreview*>(this);
    std::lock_guard lock(self->m_mutex);
    return m_samples.empty() ? 0.0 : double(m_position) / double(m_samples.size());
}

void SoundPreview::render(float* out, int frames)
{
    std::lock_guard lock(m_mutex);
    const std::size_t wanted = std::size_t(frames) * 2;
    const std::size_t available = m_samples.size() - std::min(m_position, m_samples.size());
    const std::size_t n = std::min(wanted, available);
    std::copy_n(m_samples.begin() + std::ptrdiff_t(m_position), n, out);
    std::fill(out + n, out + wanted, 0.0f);
    m_position += n;
    if (m_position >= m_samples.size())
        m_playing = false;
}

} // namespace tve
