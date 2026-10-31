#pragma once

#include "audio/AudioOutput.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

namespace tve {

// Plays a short sound once on its own output device (the library's previews), beside the
// timeline's output. The device is opened while something plays.
class SoundPreview {
public:
    explicit SoundPreview(AudioOutput::Backend backend = AudioOutput::Backend::System);
    ~SoundPreview();
    SoundPreview(const SoundPreview&) = delete;
    SoundPreview& operator=(const SoundPreview&) = delete;

    // Interleaved stereo at 48 kHz; replaces what was playing.
    bool play(std::vector<float> samples);
    void stop();
    bool playing() const { return m_playing.load(); }
    // How far it has got, 0..1.
    double progress() const;

    // What the device callback does (also for tests with Backend::None).
    void render(float* out, int frames);

private:
    struct Device;
    std::unique_ptr<Device> m_device;
    AudioOutput::Backend m_backend;
    std::mutex m_mutex;
    std::vector<float> m_samples;
    std::size_t m_position = 0;
    std::atomic<bool> m_playing{false};
};

} // namespace tve
