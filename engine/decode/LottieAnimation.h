#pragma once

#include "model/Time.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace tve {

// A Lottie (Bodymovin JSON) animation, drawn by ThorVG's software renderer. Loops: any time
// maps to a frame of its cycle. Not thread-safe: one thread at a time per animation.
class LottieAnimation {
public:
    // Nullptr if the text is not a Lottie animation ThorVG can play.
    static std::unique_ptr<LottieAnimation> fromData(const std::string& json);
    static std::unique_ptr<LottieAnimation> open(const std::string& path);
    ~LottieAnimation();

    LottieAnimation(const LottieAnimation&) = delete;
    LottieAnimation& operator=(const LottieAnimation&) = delete;

    int width() const { return m_width; }
    int height() const { return m_height; }
    double frameCount() const { return m_frames; }
    double duration() const { return m_duration; } // seconds, one cycle
    Ticks durationTicks() const { return Ticks(m_duration * double(kTicksPerSecond)); }

    // The frame `seconds` into the animation (looping), as straight-alpha RGBA bytes of
    // `width` × `height` (row after row, no padding).
    std::vector<std::uint8_t> render(double seconds, int width, int height);

private:
    struct Impl;
    explicit LottieAnimation(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_impl;
    int m_width = 0;
    int m_height = 0;
    double m_frames = 0.0;
    double m_duration = 0.0;
};

// Whether a file looks like a Lottie animation by its name (.json, .lottie.json).
bool isLottieFile(const std::string& path);

} // namespace tve
