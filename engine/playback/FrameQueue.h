#pragma once

#include "model/Time.h"

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>

namespace tve {

// Bounded hand-off between the decode thread (producer) and the render thread (consumer).
// The producer blocks when the queue is full; the consumer never blocks. `Frame` is any
// movable type with `Ticks pts() const`.
template <typename Frame>
class BasicFrameQueue {
public:
    explicit BasicFrameQueue(std::size_t capacity) : m_capacity(capacity) {}

    // Blocks while full. Returns false if the queue was stopped.
    bool push(Frame frame)
    {
        std::unique_lock lock(m_mutex);
        m_notFull.wait(lock, [&] { return m_stopped || m_frames.size() < m_capacity; });
        if (m_stopped)
            return false;
        m_frames.push_back(std::move(frame));
        return true;
    }

    std::optional<Ticks> frontPts() const
    {
        std::lock_guard lock(m_mutex);
        if (m_frames.empty())
            return std::nullopt;
        return m_frames.front().pts();
    }

    // Removes every frame due at or before `now` and returns the newest of them.
    // `skipped` receives how many older due frames were discarded unseen.
    std::optional<Frame> takeLatestDue(Ticks now, int& skipped)
    {
        std::optional<Frame> latest;
        skipped = 0;
        {
            std::lock_guard lock(m_mutex);
            while (!m_frames.empty() && m_frames.front().pts() <= now) {
                if (latest)
                    ++skipped;
                latest = std::move(m_frames.front());
                m_frames.pop_front();
            }
        }
        m_notFull.notify_one();
        return latest;
    }

    void stop()
    {
        {
            std::lock_guard lock(m_mutex);
            m_stopped = true;
            m_frames.clear();
        }
        m_notFull.notify_all();
    }

    // Empties a stopped queue and accepts frames again (used when playback seeks).
    void restart()
    {
        std::lock_guard lock(m_mutex);
        m_frames.clear();
        m_stopped = false;
    }

private:
    const std::size_t m_capacity;
    mutable std::mutex m_mutex;
    std::condition_variable m_notFull;
    std::deque<Frame> m_frames;
    bool m_stopped = false;
};

} // namespace tve
