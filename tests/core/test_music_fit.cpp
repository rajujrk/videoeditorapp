#include "commands/CommandStack.h"
#include "commands/EditCommands.h"
#include "commands/SequenceEdits.h"
#include "commands/TrackCommands.h"
#include "model/MusicFit.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace tve;
using Catch::Approx;

namespace {

constexpr Ticks kSec = kTicksPerSecond;

// A minute of music at 120 BPM: a beat every half second from 0.5 s, bars of two seconds.
MusicFitInput minute(Ticks target)
{
    MusicFitInput input;
    input.in = 0;
    input.out = 60 * kSec;
    input.target = target;
    for (int k = 1; k < 120; ++k)
        input.beats.push_back(k * kSec / 2);
    return input;
}

Ticks played(const MusicFit& fit)
{
    Ticks sum = 0;
    for (const auto& [a, b] : fit.pieces)
        sum += b - a;
    return sum;
}

// Whether a time is the music's start or end, or the first beat of a bar.
bool onBar(Ticks t)
{
    return t == 0 || t == 60 * kSec || (t - kSec / 2) % (2 * kSec) == 0;
}

} // namespace

TEST_CASE("Music is shortened by taking whole bars out of the middle", "[musicfit]")
{
    const MusicFit fit = planMusicFit(minute(40 * kSec));
    REQUIRE(fit.pieces.size() == 2);
    CHECK(fit.pieces.front().first == 0);      // the start kept
    CHECK(fit.pieces.back().second == 60 * kSec); // the end kept
    CHECK(onBar(fit.pieces[0].second));
    CHECK(onBar(fit.pieces[1].first));
    CHECK(fit.barsChanged == -10);
    CHECK(played(fit) == 40 * kSec); // ten bars of two seconds: exact
    CHECK(fit.speed == Approx(1.0));
    CHECK(fit.length == 40 * kSec);
    CHECK(fit.fadeOut == 0);
}

TEST_CASE("Music is lengthened by playing a run of bars again", "[musicfit]")
{
    const MusicFit fit = planMusicFit(minute(95 * kSec));
    REQUIRE(fit.pieces.size() >= 2);
    CHECK(fit.pieces.front().first == 0);
    CHECK(fit.pieces.back().second == 60 * kSec);
    for (const auto& [a, b] : fit.pieces) {
        CHECK(onBar(a));
        CHECK(onBar(b));
    }
    // The repeated run: what the first piece ends on, the last starts before.
    CHECK(fit.pieces.back().first < fit.pieces.front().second);
    CHECK(fit.barsChanged > 0);
    CHECK(std::abs(fit.speed - 1.0) <= kMostFitStretch + 1e-9);
    CHECK(std::llabs(Ticks(std::llround(double(played(fit)) / fit.speed)) - 95 * kSec) < kSec / 100);
    CHECK(fit.length == 95 * kSec);
}

TEST_CASE("A small difference is only the speed; a tiny target is the start faded", "[musicfit]")
{
    const MusicFit close = planMusicFit(minute(59 * kSec));
    REQUIRE(close.pieces.size() == 1);
    CHECK(close.speed == Approx(60.0 / 59.0));
    CHECK(close.length == 59 * kSec);
    CHECK(close.barsChanged == 0);

    const MusicFit tiny = planMusicFit(minute(3 * kSec));
    REQUIRE(tiny.pieces.size() == 1);
    CHECK(tiny.pieces[0] == std::pair<Ticks, Ticks>{0, 3 * kSec});
    CHECK(tiny.fadeOut == 3 * kSec / 4);
    CHECK(tiny.length == 3 * kSec);

    CHECK(planMusicFit(minute(0)).pieces.empty());
}

TEST_CASE("Without a beat the music is cut in blocks", "[musicfit]")
{
    MusicFitInput input = minute(30 * kSec);
    input.beats.clear();
    const MusicFit fit = planMusicFit(input);
    REQUIRE(fit.pieces.size() == 2);
    CHECK(fit.pieces[0].second % (2 * kSec) == 0);
    CHECK(fit.pieces[1].first % (2 * kSec) == 0);
    CHECK(played(fit) == 30 * kSec);
}

TEST_CASE("Music lands on the length: over and faded rather than short", "[musicfit]")
{
    // 32 s cut by two-second bars can make 8 s or 10 s, not 9: 10, trimmed and faded.
    MusicFitInput input = minute(9 * kSec);
    input.out = 32 * kSec;
    std::erase_if(input.beats, [](Ticks t) { return t >= 32 * kSec; });
    const MusicFit fit = planMusicFit(input);
    CHECK(fit.length == 9 * kSec);
    CHECK(played(fit) == 9 * kSec);
    CHECK(fit.speed == Approx(1.0));
    CHECK(fit.fadeOut > 0);
    CHECK(fit.pieces.front().first == 0);
}

TEST_CASE("Joins are made where the music sounds alike", "[musicfit]")
{
    // Quiet from 20 s to 40 s, loud elsewhere.
    MusicFitInput input = minute(50 * kSec);
    for (int i = 0; i < 6000; ++i)
        input.loudness.push_back(i >= 2000 && i < 4000 ? -40.0f : -10.0f);
    const MusicFit fit = planMusicFit(input);
    REQUIRE(fit.pieces.size() == 2);
    const Ticks leave = fit.pieces[0].second, arrive = fit.pieces[1].first;
    const auto quiet = [](Ticks t) { return t > 20 * kSec && t < 40 * kSec; };
    CHECK(quiet(leave) == quiet(arrive));
    CHECK(played(fit) == 50 * kSec);
}

namespace {

struct Mix {
    Project project;
    CommandStack stack{project};
    Id music = kNoId, video = kNoId, upper = kNoId, song = kNoId, cam = kNoId;
    Mix()
    {
        music = push<AddTrackCommand>(TrackKind::Audio, "Music").trackId();
        video = push<AddTrackCommand>(TrackKind::Video, "Video 1").trackId();
        upper = push<AddTrackCommand>(TrackKind::Video, "Video 2").trackId();
        song = push<ImportMediaCommand>(MediaItem{.path = "/music/Song.wav", .duration = 60 * kSec, .hasVideo = false, .hasAudio = true}).mediaId();
        cam = push<ImportMediaCommand>(MediaItem{.path = "/shoot/Cam.mp4", .duration = 60 * kSec, .hasVideo = true, .hasAudio = true}).mediaId();
    }
    template <typename T, typename... Args>
    T& push(Args&&... args)
    {
        auto command = std::make_unique<T>(std::forward<Args>(args)...);
        T& ref = *command;
        auto error = stack.push(std::move(command));
        INFO(error.value_or(""));
        REQUIRE_FALSE(error);
        return ref;
    }
    Id clip(Id track, Ticks start, Ticks duration, Id media) { return push<AddClipCommand>(track, media, start, duration, 0).clipId(); }
    const Track& track(Id id) { return *project.findTrack(id); }
};

} // namespace

TEST_CASE("A music clip becomes its fitted pieces, grouped, one after another", "[musicfit]")
{
    Mix m;
    const Id song = m.clip(m.music, 5 * kSec, 60 * kSec, m.song);
    Project p = m.project;
    std::vector<Id> made;
    const MusicFit fit = planMusicFit(minute(40 * kSec));
    REQUIRE_FALSE(fitClipToPieces(p, song, fit, &made));
    REQUIRE(made.size() == 2);
    CHECK(made[0] == song); // the first piece keeps the clip's id
    const auto& clips = p.findTrack(m.music)->clips;
    REQUIRE(clips.size() == 2);
    CHECK(clips[0].start == 5 * kSec);
    CHECK(clips[1].start == clips[0].end());
    CHECK(clips[1].end() == 45 * kSec);
    CHECK(clips[0].sourceIn == 0);
    CHECK(clips[1].sourceIn == fit.pieces[1].first);
    CHECK(clips[0].groupId != kNoId);
    CHECK(clips[0].groupId == clips[1].groupId);
    CHECK(clips[0].fadeOut == kSec / 100); // no click at the join
    CHECK(clips[1].fadeIn == kSec / 100);
    CHECK(clips[0].keepPitch);

    // Longer, at a touch of speed.
    Project q = m.project;
    const MusicFit slow = planMusicFit(minute(61 * kSec));
    REQUIRE_FALSE(fitClipToPieces(q, song, slow, nullptr));
    const auto& one = q.findTrack(m.music)->clips;
    REQUIRE(one.size() == 1);
    CHECK(one[0].speed == Approx(60.0 / 61.0));
    CHECK(std::llabs(one[0].duration - 61 * kSec) <= 1);
}

TEST_CASE("Fitting music refuses what it cannot do", "[musicfit]")
{
    Mix m;
    const Id song = m.clip(m.music, 0, 60 * kSec, m.song);
    m.clip(m.music, 70 * kSec, 5 * kSec, m.song);
    Project p = m.project;
    // No room for a longer song before the next clip.
    CHECK(fitClipToPieces(p, song, planMusicFit(minute(90 * kSec)), nullptr));
    CHECK(p == m.project);
    // Not at normal speed.
    m.push<SetClipSpeedCommand>(song, 2.0, false);
    Project q = m.project;
    CHECK(fitClipToPieces(q, song, planMusicFit(minute(20 * kSec)), nullptr));
    CHECK(fitClipToPieces(q, 999, planMusicFit(minute(20 * kSec)), nullptr));
}

TEST_CASE("Time is made at a cut, later clips and markers moving with it", "[musicfit]")
{
    Mix m;
    m.clip(m.video, 0, 4 * kSec, m.cam);
    const Id second = m.clip(m.video, 4 * kSec, 4 * kSec, m.cam);
    const Id song = m.clip(m.music, 0, 20 * kSec, m.song);
    const Id over = m.clip(m.upper, 6 * kSec, 2 * kSec, m.cam);
    m.project.markers.push_back(Marker{.id = 50, .time = 5 * kSec});
    m.project.markers.push_back(Marker{.id = 51, .time = 1 * kSec});

    Project p = m.project;
    REQUIRE_FALSE(insertTime(p, 4 * kSec, 3 * kSec));
    CHECK(p.locateClip(second)->clip->start == 7 * kSec);
    CHECK(p.locateClip(song)->clip->start == 0); // running across: stays
    CHECK(p.locateClip(over)->clip->start == 9 * kSec);
    CHECK(p.markers[0].time == 8 * kSec);
    CHECK(p.markers[1].time == 1 * kSec);

    // A locked track with clips to move refuses.
    m.project.findTrack(m.upper)->locked = true;
    Project q = m.project;
    CHECK(insertTime(q, 4 * kSec, 3 * kSec));
    CHECK(q == m.project);
}
