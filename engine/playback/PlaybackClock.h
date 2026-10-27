#pragma once

#include "model/Time.h"

#include <chrono>
#include <mutex>

namespace tve {

// The presentation clock shared by the UI (play/pause/seek) and the renderer (which frame
// is due). While playing, time advances with the system's steady clock from an anchor.
class PlaybackClock {
public:
    using SteadyClock = std::chrono::steady_clock;

    // Current presentation time. `at` is injectable for tests.
    Ticks now(SteadyClock::time_point at = SteadyClock::now()) const
    {
        std::lock_guard lock(m_mutex);
        return currentLocked(at);
    }

    bool playing() const
    {
        std::lock_guard lock(m_mutex);
        return m_playing;
    }

    bool started() const
    {
        std::lock_guard lock(m_mutex);
        return m_started;
    }

    void play(SteadyClock::time_point at = SteadyClock::now())
    {
        std::lock_guard lock(m_mutex);
        if (m_playing)
            return;
        m_anchorTime = at;
        m_playing = true;
        m_started = true;
    }

    void pause(SteadyClock::time_point at = SteadyClock::now())
    {
        std::lock_guard lock(m_mutex);
        m_anchor = currentLocked(at);
        m_playing = false;
        m_started = true;
    }

    // Jumps to `time`, keeping the play/pause state.
    void seek(Ticks time, SteadyClock::time_point at = SteadyClock::now())
    {
        std::lock_guard lock(m_mutex);
        m_anchor = time;
        m_anchorTime = at;
        m_started = true;
    }

private:
    Ticks currentLocked(SteadyClock::time_point at) const
    {
        if (!m_playing)
            return m_anchor;
        const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(at - m_anchorTime).count();
        return m_anchor + ticksFromMicros(micros);
    }

    mutable std::mutex m_mutex;
    bool m_started = false;
    bool m_playing = false;
    Ticks m_anchor = 0;
    SteadyClock::time_point m_anchorTime{};
};

} // namespace tve
