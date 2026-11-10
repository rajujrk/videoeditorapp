#pragma once

#include "playback/FrameSource.h"

#include <memory>
#include <mutex>
#include <vector>

namespace tve {

// A second view of another source's frames (the HDR viewer beside the player): the leading
// view offers each frame it takes to its mirrors, which show it by the leader's own clock.
// Pixel buffers are shared, not copied.
class MirrorSource final : public FrameSource {
public:
    explicit MirrorSource(std::shared_ptr<FrameSource> leader) : m_leader(std::move(leader)) {}

    FrameQueue& queue() override { return m_queue; }
    PlaybackClock& clock() override { return m_leader->clock(); }
    PlaybackCounters& counters() override { return m_counters; }
    bool hardwareAccelerated() const override { return m_leader->hardwareAccelerated(); }
    Size canvasSize() const override { return m_leader->canvasSize(); }
    const std::shared_ptr<FrameSource>& leader() const { return m_leader; }

private:
    std::shared_ptr<FrameSource> m_leader;
    FrameQueue m_queue{3};
    PlaybackCounters m_counters;
};

// The mirrors of one view (any thread).
class FrameMirrors {
public:
    void add(const std::shared_ptr<MirrorSource>& mirror)
    {
        std::lock_guard lock(m_mutex);
        m_mirrors.push_back(mirror);
    }
    // A copy of `frame` to every mirror still alive that follows `leader`.
    void offer(const FrameSource* leader, const CompositeFrame& frame)
    {
        std::lock_guard lock(m_mutex);
        std::erase_if(m_mirrors, [](const auto& weak) { return weak.expired(); });
        for (const auto& weak : m_mirrors) {
            if (auto mirror = weak.lock(); mirror && mirror->leader().get() == leader)
                mirror->queue().offer(frame);
        }
    }

private:
    std::mutex m_mutex;
    std::vector<std::weak_ptr<MirrorSource>> m_mirrors;
};

} // namespace tve
