#pragma once

#include "capture/Capture.h"
#include "model/Cursor.h"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace tve {

// Records a capture source into a MOV (H.264, with the source's sound as AAC) at a steady
// frame rate: a writer thread takes the latest picture every frame period (repeating it when
// none came, skipping extras), so the file's time is the wall clock's from start().
class CaptureRecorder {
public:
    CaptureRecorder(std::unique_ptr<CaptureSource> source, int frameRate = 30);
    ~CaptureRecorder();
    CaptureRecorder(const CaptureRecorder&) = delete;
    CaptureRecorder& operator=(const CaptureRecorder&) = delete;

    bool start(const std::string& path, std::string* error = nullptr);
    bool recording() const { return m_recording; }
    Ticks elapsed() const;

    struct Take {
        std::string path;
        Ticks duration = 0;
        int width = 0;
        int height = 0;
        bool hasSound = false;
    };
    std::optional<Take> stop(std::string* error = nullptr);

    const CaptureSource& source() const { return *m_source; }

private:
    void writerLoop();

    std::unique_ptr<CaptureSource> m_source;
    int m_rate;
    std::string m_path;
    std::atomic<bool> m_recording{false};
    std::chrono::steady_clock::time_point m_begin;
    std::thread m_writer;
    std::mutex m_mutex;
    CVPixelBufferRef m_latest = nullptr; // retained, under m_mutex
    std::vector<float> m_sound;          // waiting to be written, under m_mutex
    std::int64_t m_framesWritten = 0;
    bool m_failed = false;
    std::string m_error;
};

// The mouse over a screen recording: its position and left-button clicks, 60 times a second,
// relative to an area of the screen (global points, origin top left). Polls the system
// (no event taps: no extra permission). Test mode draws a circle and clicks every second.
class CursorLog {
public:
    explicit CursorLog(std::array<double, 4> area, bool test = false);
    ~CursorLog();
    void start();
    // The log, with times from start() and positions as fractions of the area.
    CursorEffects stop();

private:
    void run();

    std::array<double, 4> m_area;
    bool m_test;
    std::atomic<bool> m_running{false};
    std::thread m_thread;
    CursorEffects m_log;
};

} // namespace tve
