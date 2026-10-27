#pragma once

#include "model/Time.h"

#include <CoreVideo/CoreVideo.h>

#include <utility>

namespace tve {

// A decoded picture held as an IOSurface-backed CVPixelBuffer (NV12 or P010), so the GPU
// can sample it without a copy. Owns one retain on the buffer.
class DecodedFrame {
public:
    DecodedFrame() = default;
    DecodedFrame(CVPixelBufferRef buffer, Ticks pts) : m_buffer(buffer), m_pts(pts) {}
    ~DecodedFrame() { reset(); }

    DecodedFrame(DecodedFrame&& other) noexcept
        : m_buffer(std::exchange(other.m_buffer, nullptr)), m_pts(other.m_pts) {}
    DecodedFrame& operator=(DecodedFrame&& other) noexcept
    {
        if (this != &other) {
            reset();
            m_buffer = std::exchange(other.m_buffer, nullptr);
            m_pts = other.m_pts;
        }
        return *this;
    }
    DecodedFrame(const DecodedFrame&) = delete;
    DecodedFrame& operator=(const DecodedFrame&) = delete;

    // Another reference to the same pixel buffer (no copy), e.g. to show one decoded
    // picture in several composite frames.
    DecodedFrame share() const
    {
        if (m_buffer)
            CVPixelBufferRetain(m_buffer);
        return DecodedFrame(m_buffer, m_pts);
    }

    CVPixelBufferRef pixelBuffer() const { return m_buffer; }
    Ticks pts() const { return m_pts; }
    void setPts(Ticks pts) { m_pts = pts; }
    explicit operator bool() const { return m_buffer != nullptr; }

    void reset()
    {
        if (m_buffer)
            CVPixelBufferRelease(m_buffer);
        m_buffer = nullptr;
    }

private:
    CVPixelBufferRef m_buffer = nullptr;
    Ticks m_pts = 0;
};

} // namespace tve
