#include "capture/CaptureRecorder.h"

#include "encode/MediaWriter.h"
#include "model/Translate.h"

#import <Foundation/Foundation.h>
#include <ApplicationServices/ApplicationServices.h>
#include <VideoToolbox/VideoToolbox.h>

#include <cmath>
#include <filesystem>
#include <numbers>

namespace tve {

namespace {

constexpr int kRate = 48000;

} // namespace

// What the writer thread works with.
struct RecorderState {
    std::unique_ptr<MediaWriter> writer;
    CVPixelBufferRef nv12 = nullptr; // the writer's size
    VTPixelTransferSessionRef transfer = nullptr;
    std::int64_t soundWritten = 0;
};

CaptureRecorder::CaptureRecorder(std::unique_ptr<CaptureSource> source, int frameRate)
    : m_source(std::move(source)), m_rate(std::clamp(frameRate, 1, 60))
{
}

CaptureRecorder::~CaptureRecorder()
{
    if (m_recording)
        stop();
    if (m_latest)
        CVPixelBufferRelease(m_latest);
}

Ticks CaptureRecorder::elapsed() const
{
    if (!m_recording)
        return Ticks(double(m_framesWritten) / m_rate * double(kTicksPerSecond));
    std::lock_guard lock(m_mutex);
    const auto until = m_paused ? m_pausedAt : std::chrono::steady_clock::now();
    return Ticks(std::chrono::duration<double>(until - m_begin).count() * double(kTicksPerSecond));
}

void CaptureRecorder::pause()
{
    std::lock_guard lock(m_mutex);
    if (!m_recording || m_paused)
        return;
    m_pausedAt = std::chrono::steady_clock::now();
    m_paused = true;
}

void CaptureRecorder::resume()
{
    std::lock_guard lock(m_mutex);
    if (!m_paused)
        return;
    // The time paused never happened, as far as the file goes.
    m_begin += std::chrono::steady_clock::now() - m_pausedAt;
    m_sound.clear(); // (what came while paused)
    m_paused = false;
}

bool CaptureRecorder::start(const std::string& path, std::string* error)
{
    if (m_recording)
        return false;
    m_path = path;
    m_failed = false;
    m_error.clear();
    m_framesWritten = 0;
    m_paused = false;
    {
        std::lock_guard lock(m_mutex);
        m_sound.clear();
        if (m_latest)
            CVPixelBufferRelease(m_latest);
        m_latest = nullptr;
    }
    CaptureCallbacks callbacks;
    callbacks.frame = [this](CVPixelBufferRef frame, Ticks) {
        std::lock_guard lock(m_mutex);
        if (m_latest)
            CVPixelBufferRelease(m_latest);
        m_latest = CVPixelBufferRetain(frame);
    };
    callbacks.sound = [this](const float* samples, int frames) {
        std::lock_guard lock(m_mutex);
        if (m_paused)
            return;
        m_sound.insert(m_sound.end(), samples, samples + std::size_t(frames) * 2);
    };
    if (!m_source->start(std::move(callbacks), error))
        return false;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    {
        std::lock_guard lock(m_mutex);
        m_begin = std::chrono::steady_clock::now();
    }
    m_recording = true;
    m_writer = std::thread([this] { writerLoop(); });
    return true;
}

void CaptureRecorder::writerLoop()
{
    RecorderState s;
    MediaSettings settings;
    settings.container = Container::Mov;
    settings.codec = VideoCodec::H264;
    settings.width = std::max(16, m_source->width()) & ~1;
    settings.height = std::max(16, m_source->height()) & ~1;
    settings.frameRate = {m_rate, 1};
    settings.audio = m_source->hasSound();
    settings.keyframeInterval = m_rate;
    s.writer = MediaWriter::open(m_path, settings, &m_error);
    NSDictionary* attributes = @{(id)kCVPixelBufferIOSurfacePropertiesKey: @{}};
    CVPixelBufferCreate(kCFAllocatorDefault, std::size_t(settings.width), std::size_t(settings.height),
                        kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange, (CFDictionaryRef)attributes, &s.nv12);
    VTPixelTransferSessionCreate(kCFAllocatorDefault, &s.transfer);
    if (!s.writer || !s.nv12 || !s.transfer) {
        m_failed = true;
        if (m_error.empty())
            m_error = TVE_TR("core", "The recording file could not be made.");
    } else {
        // Black until the first picture comes.
        if (CVPixelBufferLockBaseAddress(s.nv12, 0) == kCVReturnSuccess) {
            auto* y = static_cast<std::uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(s.nv12, 0));
            std::fill_n(y, CVPixelBufferGetBytesPerRowOfPlane(s.nv12, 0) * std::size_t(settings.height), std::uint8_t(16));
            auto* uv = static_cast<std::uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(s.nv12, 1));
            std::fill_n(uv, CVPixelBufferGetBytesPerRowOfPlane(s.nv12, 1) * std::size_t(settings.height / 2), std::uint8_t(128));
            CVPixelBufferUnlockBaseAddress(s.nv12, 0);
        }
    }
    std::vector<float> sound;
    while (!m_failed) {
        // Paused: nothing written until resumed (or stopped), the time moved on by resume().
        if (m_paused) {
            if (!m_recording)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        std::chrono::steady_clock::time_point due;
        {
            std::lock_guard lock(m_mutex);
            due = m_begin + std::chrono::microseconds(std::int64_t(1e6 * double(m_framesWritten + 1) / m_rate));
        }
        std::this_thread::sleep_until(due);
        CVPixelBufferRef latest = nullptr;
        {
            std::lock_guard lock(m_mutex);
            if (m_paused)
                continue; // paused while waiting: this frame is not written
            latest = m_latest ? CVPixelBufferRetain(m_latest) : nullptr;
            sound.swap(m_sound);
            m_sound.clear();
        }
        if (latest) {
            VTPixelTransferSessionTransferImage(s.transfer, latest, s.nv12);
            CVPixelBufferRelease(latest);
        }
        if (settings.audio) {
            // The sound so far, and silence where the system sent none (keeping it in step).
            if (!sound.empty() && s.writer->writeAudio(sound.data(), int(sound.size() / 2)))
                s.soundWritten += std::int64_t(sound.size() / 2);
            const std::int64_t expected = std::int64_t(double(m_framesWritten + 1) / m_rate * kRate);
            if (s.soundWritten < expected - kRate / 5) {
                std::vector<float> silence(std::size_t(expected - kRate / 10 - s.soundWritten) * 2, 0.0f);
                s.writer->writeAudio(silence.data(), int(silence.size() / 2));
                s.soundWritten += std::int64_t(silence.size() / 2);
            }
        }
        if (CVPixelBufferLockBaseAddress(s.nv12, kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess) {
            m_failed = true;
            break;
        }
        const bool ok = s.writer->writeVideo(static_cast<const std::uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(s.nv12, 0)),
                                             int(CVPixelBufferGetBytesPerRowOfPlane(s.nv12, 0)),
                                             static_cast<const std::uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(s.nv12, 1)),
                                             int(CVPixelBufferGetBytesPerRowOfPlane(s.nv12, 1)));
        CVPixelBufferUnlockBaseAddress(s.nv12, kCVPixelBufferLock_ReadOnly);
        if (!ok) {
            m_failed = true;
            m_error = TVE_TR("core", "The recording could not be written.");
            break;
        }
        ++m_framesWritten;
        if (!m_recording)
            break;
    }
    if (s.writer && !m_failed && !s.writer->finish()) {
        m_failed = true;
        m_error = TVE_TR("core", "The recording could not be finished.");
    }
    if (s.nv12)
        CVPixelBufferRelease(s.nv12);
    if (s.transfer) {
        VTPixelTransferSessionInvalidate(s.transfer);
        CFRelease(s.transfer);
    }
}

std::optional<CaptureRecorder::Take> CaptureRecorder::stop(std::string* error)
{
    if (!m_recording) {
        if (error)
            *error = TVE_TR("core", "Nothing is being recorded.");
        return std::nullopt;
    }
    m_recording = false;
    if (m_writer.joinable())
        m_writer.join();
    m_source->stop();
    if (m_failed || m_framesWritten == 0) {
        if (error)
            *error = m_error.empty() ? std::string(TVE_TR("core", "Nothing was recorded.")) : m_error;
        std::error_code ec;
        std::filesystem::remove(m_path, ec);
        return std::nullopt;
    }
    return Take{m_path, Ticks(double(m_framesWritten) / m_rate * double(kTicksPerSecond)), m_source->width() & ~1,
                m_source->height() & ~1, m_source->hasSound()};
}

// --- the cursor log ---

CursorLog::CursorLog(std::array<double, 4> area, bool test) : m_area(area), m_test(test) {}

CursorLog::~CursorLog()
{
    m_running = false;
    if (m_thread.joinable())
        m_thread.join();
}

void CursorLog::start()
{
    m_log = {};
    m_paused = false;
    m_running = true;
    m_thread = std::thread([this] { run(); });
}

void CursorLog::pause()
{
    m_paused = true;
}

void CursorLog::resume()
{
    m_paused = false;
}

CursorEffects CursorLog::stop()
{
    m_running = false;
    if (m_thread.joinable())
        m_thread.join();
    m_log.path = thinCursorPath(m_log.path);
    return m_log;
}

void CursorLog::run()
{
    auto begin = std::chrono::steady_clock::now();
    bool down = false;
    for (int n = 0; m_running; ++n) {
        std::this_thread::sleep_until(begin + std::chrono::microseconds(std::int64_t(1e6 * n / 60)));
        if (m_paused) {
            // The time paused is left out: the log's clock waits with the recording's.
            const auto pausedAt = std::chrono::steady_clock::now();
            while (m_paused && m_running)
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            begin += std::chrono::steady_clock::now() - pausedAt;
            continue;
        }
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
        const Ticks time = Ticks(seconds * double(kTicksPerSecond));
        double x = 0.5, y = 0.5;
        bool pressed = false;
        if (m_test) {
            // A circle every four seconds; a click at each whole second.
            x = 0.5 + 0.3 * std::cos(seconds * std::numbers::pi / 2);
            y = 0.5 + 0.3 * std::sin(seconds * std::numbers::pi / 2);
            pressed = seconds - std::floor(seconds) < 0.1 && seconds >= 1.0;
        } else {
            CGEventRef event = CGEventCreate(nullptr);
            const CGPoint at = CGEventGetLocation(event);
            CFRelease(event);
            x = (at.x - m_area[0]) / std::max(1.0, m_area[2]);
            y = (at.y - m_area[1]) / std::max(1.0, m_area[3]);
            pressed = CGEventSourceButtonState(kCGEventSourceStateCombinedSessionState, kCGMouseButtonLeft);
        }
        m_log.path.push_back({time, x, y});
        if (pressed && !down)
            m_log.clicks.push_back(time);
        down = pressed;
    }
}

} // namespace tve
