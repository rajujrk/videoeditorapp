#include "model/Time.h"

#include <catch2/catch_test_macros.hpp>

using namespace tve;

TEST_CASE("common frame rates have whole-tick frame durations")
{
    const FrameRate rates[] = {{24000, 1001}, {24, 1}, {25, 1}, {30000, 1001}, {30, 1},
                               {48, 1},       {50, 1}, {60000, 1001}, {60, 1}, {120, 1}};
    for (FrameRate rate : rates) {
        INFO(rate.num << "/" << rate.den);
        CHECK((kTicksPerSecond * rate.den) % rate.num == 0);
    }
}

TEST_CASE("seconds round-trip through ticks")
{
    CHECK(fromSeconds(1.0) == kTicksPerSecond);
    CHECK(fromSeconds(toSeconds(123'456'789)) == 123'456'789);
    CHECK(frameDuration({30, 1}) * 30 == kTicksPerSecond);
}
