#pragma once

#include <cmath>
#include <cstdint>

namespace tve {

// Timeline time is an integer count of "flicks" (1/705,600,000 s). Every common frame
// rate, including NTSC rates such as 24000/1001 and 60000/1001, has a whole-number frame
// duration in flicks, so frame-accurate edits never accumulate rounding error.
using Ticks = std::int64_t;

inline constexpr Ticks kTicksPerSecond = 705'600'000;

struct FrameRate {
    std::int32_t num = 30;
    std::int32_t den = 1;

    friend bool operator==(const FrameRate&, const FrameRate&) = default;
};

constexpr Ticks frameDuration(FrameRate rate)
{
    return kTicksPerSecond * rate.den / rate.num;
}

constexpr double toSeconds(Ticks ticks)
{
    return static_cast<double>(ticks) / static_cast<double>(kTicksPerSecond);
}

// Exact: 705.6 ticks per microsecond. (Dividing kTicksPerSecond by 1'000'000 first would
// truncate to 705 and drift 0.6 ms per second.) Safe for spans up to ~40 years.
constexpr Ticks ticksFromMicros(std::int64_t micros)
{
    return micros * 7056 / 10;
}

constexpr std::int64_t microsFromTicks(Ticks ticks)
{
    return ticks * 10 / 7056;
}

inline Ticks fromSeconds(double seconds)
{
    return static_cast<Ticks>(std::llround(seconds * static_cast<double>(kTicksPerSecond)));
}

} // namespace tve
