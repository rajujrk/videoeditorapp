#include "commands/CommandStack.h"
#include "commands/EditCommands.h"
#include "io/ProjectSerializer.h"
#include "model/RenderCost.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace tve;

namespace {

constexpr Ticks kSec = kTicksPerSecond;

struct Timeline {
    Project project;
    CommandStack stack{project};
    std::vector<Id> tracks;
    Id uhd = kNoId; // 4K30
    Id hd = kNoId;  // 1080p30

    explicit Timeline(int videoTracks)
    {
        for (int i = 0; i < videoTracks; ++i)
            tracks.push_back(add<AddTrackCommand>(TrackKind::Video, "V").trackId());
        tracks.push_back(add<AddTrackCommand>(TrackKind::Audio, "A").trackId());
        uhd = add<ImportMediaCommand>(MediaItem{.path = "/uhd.mp4", .duration = 100 * kSec, .width = 3840, .height = 2160}).mediaId();
        hd = add<ImportMediaCommand>(MediaItem{.path = "/hd.mp4", .duration = 100 * kSec, .hasAudio = true, .width = 1920, .height = 1080}).mediaId();
    }
    template <typename T, typename... Args> T& add(Args&&... args)
    {
        auto command = std::make_unique<T>(std::forward<Args>(args)...);
        T& raw = *command;
        REQUIRE_FALSE(stack.push(std::move(command)));
        return raw;
    }
    Id clip(std::size_t track, Id media, Ticks start, Ticks duration)
    {
        return add<AddClipCommand>(tracks[track], media, start, duration).clipId();
    }
};

} // namespace

TEST_CASE("the load of a stretch counts its layers' decoding against 4K30, and its effects")
{
    Timeline t(3);
    t.clip(0, t.uhd, 0, 10 * kSec);
    t.clip(1, t.hd, 2 * kSec, 4 * kSec);
    const Id top = t.clip(2, t.uhd, 4 * kSec, 2 * kSec);
    const auto load = playbackLoad(t.project);
    // Cut where what plays changes: 0–2, 2–4, 4–6, 6–10 s. A 4K30 layer is 0.07, a 1080p30
    // one a quarter of that.
    REQUIRE(load.size() == 4);
    CHECK(load[0].from == 0);
    CHECK(load[0].to == 2 * kSec);
    CHECK(load[0].load == Catch::Approx(0.07));
    CHECK(load[1].load == Catch::Approx(0.07 * 1.25));
    CHECK(load[2].load == Catch::Approx(0.07 * 2.25));
    CHECK(load[3].load == Catch::Approx(0.07));

    // Effects weigh what the app says (here: a blur's two wide passes, 0.11 each, on 4K),
    // a plug-in by default more than a built-in effect.
    t.add<SetClipEffectsCommand>(top, std::vector<ClipEffect>{{"tve.blur.gaussian", true, {}}, {"ofx:com.example.glow", true, {}}}, "Effects");
    CHECK(playbackLoad(t.project)[2].load == Catch::Approx(0.07 * 2.25 + 0.02 + 0.15));
    const auto weight = [](const ClipEffect& e) { return e.effectId == "tve.blur.gaussian" ? 0.22 : defaultEffectWeight(e); };
    CHECK(playbackLoad(t.project, weight)[2].load == Catch::Approx(0.07 * 2.25 + 0.22 + 0.15));

    // A proxy counts at its size: a 4K clip's at 720p.
    t.project.media[0].proxyPath = "/uhd-proxy.mov";
    CHECK(playbackLoad(t.project)[0].load == Catch::Approx(0.07 * 1280.0 * 720.0 / (3840.0 * 2160.0)));
}

TEST_CASE("heavy stretches are those over the limit, neighbours joined")
{
    // Five 4K layers each with a blur (0.29 each) from 0 to 10 s, a sixth from 3 to 5 s.
    const auto weight = [](const ClipEffect&) { return 0.22; };
    Timeline t(6);
    for (std::size_t i = 0; i < 6; ++i) {
        const Id clip = t.clip(i, t.uhd, i < 5 ? 0 : 3 * kSec, i < 5 ? 10 * kSec : 2 * kSec);
        t.add<SetClipEffectsCommand>(clip, std::vector<ClipEffect>{{"tve.blur.gaussian", true, {}}}, "Effects");
    }
    const auto heavy = heavyStretches(t.project, kRealTimeLoad, weight);
    REQUIRE(heavy.size() == 1); // over 1 all along: one stretch
    CHECK(heavy[0].from == 0);
    CHECK(heavy[0].to == 10 * kSec);
    CHECK(heavy[0].load == Catch::Approx(6 * 0.29));
    // Over a higher limit only where the sixth plays.
    const auto heavier = heavyStretches(t.project, 1.6, weight);
    REQUIRE(heavier.size() == 1);
    CHECK(heavier[0].from == 3 * kSec);
    CHECK(heavier[0].to == 5 * kSec);
    // Twelve 4K30 layers without effects play in real time; three with a blur too.
    Timeline plain(12);
    for (std::size_t i = 0; i < 12; ++i)
        plain.clip(i, plain.uhd, 0, 10 * kSec);
    CHECK(heavyStretches(plain.project).empty());
    CHECK(heavyStretches(t.project, kRealTimeLoad, [](const ClipEffect&) { return 0.0; }).empty());
}

TEST_CASE("a stretch's rendered content changes only with what it shows")
{
    Timeline t(2);
    const Id first = t.clip(0, t.hd, 0, 4 * kSec);
    const Id second = t.clip(0, t.hd, 4 * kSec, 4 * kSec);
    const Id far = t.clip(0, t.hd, 20 * kSec, 4 * kSec);
    const Id sound = t.add<ImportMediaCommand>(MediaItem{.path = "/a.wav", .duration = 100 * kSec, .hasVideo = false, .hasAudio = true}).mediaId();
    t.add<AddClipCommand>(t.tracks[2], sound, 0, 8 * kSec);
    const std::string before = renderedContent(t.project, 0, 4 * kSec);

    // A clip far away (the next on the track, not touching), the sound, a track's name: the same.
    t.add<MoveClipCommand>(far, t.tracks[0], 30 * kSec);
    t.add<SetClipVisualCommand>(far, ClipVisual{.opacity = 0.3});
    t.project.tracks[2].clips[0].gainDb = -6.0f;
    t.project.tracks[0].name = "Renamed";
    CHECK(renderedContent(t.project, 0, 4 * kSec) == before);

    // The neighbour (a transition into it would reach back), and the clip itself: different.
    ClipVisual faded;
    faded.opacity = 0.5;
    t.add<SetClipVisualCommand>(second, faded);
    const std::string afterNeighbour = renderedContent(t.project, 0, 4 * kSec);
    CHECK(afterNeighbour != before);
    t.add<SetClipVisualCommand>(first, faded);
    CHECK(renderedContent(t.project, 0, 4 * kSec) != afterNeighbour);
    // And undone, the same again.
    t.stack.undo();
    t.stack.undo();
    CHECK(renderedContent(t.project, 0, 4 * kSec) == before);
    // Another range is another content.
    CHECK(renderedContent(t.project, 0, 2 * kSec) != before);
}
