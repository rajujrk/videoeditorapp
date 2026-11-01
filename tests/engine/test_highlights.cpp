#include "analysis/Highlights.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace tve;

namespace {

Ticks at(double seconds)
{
    return Ticks(std::llround(seconds * double(kTicksPerSecond)));
}

// 30 s: quiet except a loud stretch from 12 to 18 s; a picture that barely moves, with a
// cut at 20 s.
std::pair<AudioFeatures, VideoFeatures> recording()
{
    AudioFeatures audio;
    audio.duration = at(30);
    for (int i = 0; i < 3000; ++i) {
        const double t = i * AudioFeatures::kHop;
        audio.loudness.push_back(t >= 12 && t < 18 ? -10.0f : -38.0f + float(std::sin(t * 7.0)));
        audio.onset.push_back(t >= 12 && t < 18 ? 2.0f : 0.2f);
    }
    VideoFeatures video;
    for (int f = 0; f < 900; ++f) {
        video.times.push_back(at(f / 30.0));
        video.change.push_back(f == 600 ? 0.9f : f == 0 ? 0.0f : 0.02f + 0.01f * float(f % 3));
    }
    return {audio, video};
}

} // namespace

TEST_CASE("the loud stretch is the best moment, and moments keep their lengths and don't overlap")
{
    const auto [audio, video] = recording();
    const std::vector<TimedWord> words{{at(2.0), at(2.4), "We"}, {at(2.4), at(2.8), "start"}, {at(2.8), at(3.4), "here."}};
    const auto moments = findMoments(&audio, &video, words, at(30), {.minLength = 2.0, .maxLength = 6.0});
    REQUIRE(moments.size() >= 5);
    // Best first; the best is inside the loud stretch.
    for (std::size_t i = 1; i < moments.size(); ++i)
        CHECK(moments[i - 1].score >= moments[i].score);
    CHECK(moments[0].start >= at(11.5));
    CHECK(moments[0].end <= at(18.5));
    CHECK(moments[0].why == "loud");
    CHECK(moments[0].score > moments.back().score + 0.3);
    // Lengths within bounds; nothing overlaps; together they cover the recording.
    auto ordered = moments;
    std::ranges::sort(ordered, {}, &Moment::start);
    Ticks covered = 0;
    for (std::size_t i = 0; i < ordered.size(); ++i) {
        CHECK(ordered[i].length() >= at(2.0));
        CHECK(ordered[i].length() <= at(6.0));
        if (i > 0)
            CHECK(ordered[i].start >= ordered[i - 1].end);
        covered += ordered[i].length();
    }
    CHECK(covered >= at(28));
    // Cut at the scene change, and where the sentence ends.
    CHECK(std::ranges::any_of(ordered, [](const Moment& m) { return m.start == at(20); }));
    CHECK(std::ranges::any_of(ordered, [](const Moment& m) { return m.end == at(3.5); }));
    // What is said comes with its moment.
    CHECK(std::ranges::any_of(ordered, [](const Moment& m) { return m.text == "We start here."; }));
}

TEST_CASE("moments from sound alone, from pictures alone, and picked to a total length")
{
    const auto [audio, video] = recording();
    const auto heard = findMoments(&audio, nullptr, {}, at(30));
    REQUIRE(!heard.empty());
    CHECK(heard[0].start >= at(11.5));
    CHECK(heard[0].end <= at(18.5));
    const auto seen = findMoments(nullptr, &video, {}, at(30));
    REQUIRE(!seen.empty());
    CHECK(findMoments(&audio, &video, {}, 0).empty());

    const auto picked = pickMoments(findMoments(&audio, &video, {}, at(30)), at(10));
    Ticks sum = 0;
    for (std::size_t i = 0; i < picked.size(); ++i) {
        sum += picked[i].length();
        if (i > 0)
            CHECK(picked[i].start > picked[i - 1].start); // in time order
    }
    CHECK(sum >= at(10));
    CHECK(sum <= at(16));
    CHECK(std::ranges::any_of(picked, [](const Moment& m) { return m.start >= at(11.5) && m.end <= at(18.5); }));
}
