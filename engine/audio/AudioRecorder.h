#pragma once

#include "model/Time.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace tve {

class WavWriter;

// Records sound from an input device (via miniaudio) into WAV files: 48 kHz, mono (the
// device's channels are mixed down), 32-bit float. The device runs for as long as the
// recorder exists, so its level can be shown before and between takes; start() and stop()
// bracket one take. The device callback only copies samples into a lock-free ring and a
// writer thread moves them to the file, so the callback never waits on the disk.
class AudioRecorder {
public:
    enum class Backend {
        System, // a real input device
        Null,   // miniaudio's null device: real-time callbacks of silence (CI)
        None,   // no device at all; call push() yourself (unit tests)
    };
    static constexpr int kChannels = 1;

    // Input devices by name, the system default first. Empty for Backend::None.
    static std::vector<std::string> inputDevices(Backend backend = Backend::System);

    // Opens the input device named `deviceName`, or the default one when that is empty or
    // not found.
    explicit AudioRecorder(Backend backend = Backend::System, const std::string& deviceName = {});
    ~AudioRecorder();

    AudioRecorder(const AudioRecorder&) = delete;
    AudioRecorder& operator=(const AudioRecorder&) = delete;

    bool isOpen() const { return m_open; }
    const std::string& deviceName() const { return m_deviceName; }
    const std::string& errorString() const { return m_error; }
    // How long after it was heard a sample reaches the callback (one device period). A
    // take drops this much from its start, so it lines up with when recording began.
    Ticks latency() const { return m_latency; }

    // Peak level (0..1) of the latest block of input, recording or not.
    float peak() const { return m_peak.load(); }

    // Begins a take written to `path` (replacing any file there).
    bool start(const std::string& path, std::string* error = nullptr);
    bool recording() const { return m_recording.load(); }
    // Length of the current take so far, or of the last one after stop().
    Ticks recordedDuration() const;
    // Samples the take lost because the writer could not keep up.
    std::int64_t droppedFrames() const { return m_dropped.load(); }

    struct Take {
        std::string path;
        Ticks duration = 0;
    };
    // Ends the take and closes its file. Fails if no take is running, the file could not be
    // written, or nothing was recorded.
    std::optional<Take> stop(std::string* error = nullptr);

    // Feeds `frames` mono samples as the device callback does (tests use it with Backend::None).
    void push(const float* samples, int frames);

private:
    struct Device;
    void writerLoop();
    void drain(); // moves ring contents to the file; call with m_fileMutex held

    std::unique_ptr<Device> m_device;
    bool m_open = false;
    bool m_hasCallbacks = false; // a device thread calls push()
    std::string m_deviceName;
    std::string m_error;
    Ticks m_latency = 0;
    std::int64_t m_skipFrames = 0;

    std::atomic<float> m_peak{0.0f};
    std::atomic<bool> m_recording{false};
    std::atomic<std::uint32_t> m_take{0};     // bumped by start()
    std::atomic<bool> m_callbackIdle{true};   // the callback is not writing a take
    std::atomic<std::int64_t> m_takeFrames{0};
    std::atomic<std::int64_t> m_dropped{0};
    // Callback-thread state
    std::uint32_t m_callbackTake = 0;
    std::int64_t m_skipRemaining = 0;

    std::mutex m_fileMutex;
    std::unique_ptr<WavWriter> m_file;
    std::string m_path;
    bool m_writeFailed = false;

    std::mutex m_wakeMutex;
    std::condition_variable m_wake;
    bool m_quit = false;
    std::thread m_writer;
};

} // namespace tve
