#pragma once

#include "model/Time.h"
#include "playback/TimelineAudio.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace tve {

class PlaybackClock;

// Plays mixed timeline audio on the system output device (via miniaudio), slaved to the
// playback clock: the device callback plays the chunk samples for the clock time at
// which they will be heard (clock + output latency) and realigns when it drifts more than
// a threshold, so audio and video stay in step across seeks and edits.
class AudioOutput {
public:
    enum class Backend {
        System, // the default output device
        Null,   // miniaudio's null device: real-time callbacks, no sound card (CI)
        None,   // no device at all; call render() yourself (unit tests)
    };
    explicit AudioOutput(Backend backend = Backend::System);
    ~AudioOutput();

    AudioOutput(const AudioOutput&) = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;

    bool isRunning() const { return m_running; }
    const std::string& deviceName() const { return m_deviceName; }
    std::string errorString() const { return m_error; }

    void setSource(std::shared_ptr<TimelineAudio> audio, PlaybackClock* clock);
    // Muted output keeps following the clock but plays silence (e.g. while a microphone
    // records, so the speakers are not recorded too).
    void setMuted(bool muted) { m_muted = muted; }
    bool muted() const { return m_muted; }

    // Peak levels (0..1) of the most recent callback, for meters.
    float peakLeft() const { return m_peakLeft.load(); }
    float peakRight() const { return m_peakRight.load(); }
    // Callbacks that found no chunk ready (decoding fell behind).
    std::int64_t underruns() const { return m_underruns.load(); }
    Ticks latency() const { return m_latency; }

    // Renders `frames` stereo frames as the device callback would (also used by tests).
    void render(float* out, int frames);

private:
    struct Device;
    std::unique_ptr<Device> m_device;
    bool m_running = false;
    std::string m_deviceName;
    std::string m_error;
    Ticks m_latency = 0;

    std::mutex m_sourceMutex; // the callback only try_locks it
    std::shared_ptr<TimelineAudio> m_audio;
    PlaybackClock* m_clock = nullptr;

    // Callback-thread state
    std::optional<AudioChunk> m_current;
    std::optional<Ticks> m_cursor;

    std::atomic<float> m_peakLeft{0.0f};
    std::atomic<float> m_peakRight{0.0f};
    std::atomic<std::int64_t> m_underruns{0};
    std::atomic<bool> m_muted{false};
};

} // namespace tve
