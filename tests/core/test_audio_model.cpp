#include "commands/CommandStack.h"
#include "commands/EditCommands.h"
#include "io/ProjectSerializer.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

using namespace tve;

namespace {

constexpr Ticks kSec = kTicksPerSecond;

struct AvProject {
    Project project;
    CommandStack stack{project};
    Id audioTrack = kNoId;
    Id videoTrack = kNoId;
    Id avMedia = kNoId;
    Id wavMedia = kNoId;
    Id silentMedia = kNoId;

    AvProject()
    {
        audioTrack = push<AddTrackCommand>(TrackKind::Audio, "A1").trackId();
        videoTrack = push<AddTrackCommand>(TrackKind::Video, "V1").trackId();
        avMedia = push<ImportMediaCommand>(MediaItem{.path = "/av.mp4", .duration = 20 * kSec, .hasAudio = true}).mediaId();
        wavMedia = push<ImportMediaCommand>(
                       MediaItem{.path = "/music.wav", .duration = 60 * kSec, .hasVideo = false, .hasAudio = true})
                       .mediaId();
        silentMedia = push<ImportMediaCommand>(MediaItem{.path = "/silent.mp4", .duration = 20 * kSec}).mediaId();
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

    template <typename T, typename... Args>
    std::optional<std::string> tryPush(Args&&... args)
    {
        return stack.push(std::make_unique<T>(std::forward<Args>(args)...));
    }

    Clip& clip(Id id) { return *project.locateClip(id)->clip; }
};

} // namespace

TEST_CASE("gainAt combines volume and linear fades")
{
    Clip clip{.start = 10 * kSec, .duration = 10 * kSec, .gainDb = 0.0f, .fadeIn = 2 * kSec, .fadeOut = 4 * kSec};
    CHECK(clip.gainAt(10 * kSec) == 0.0f);
    CHECK(clip.gainAt(11 * kSec) == Catch::Approx(0.5f));
    CHECK(clip.gainAt(14 * kSec) == Catch::Approx(1.0f));
    CHECK(clip.gainAt(18 * kSec) == Catch::Approx(0.5f));
    clip.gainDb = -6.0f;
    CHECK(clip.gainAt(14 * kSec) == Catch::Approx(0.501f).margin(0.001));
    clip.gainDb = kMinGainDb;
    CHECK(clip.gainAt(14 * kSec) == 0.0f);
}

TEST_CASE("media must suit the track it goes on")
{
    AvProject p;
    CHECK(p.tryPush<AddClipCommand>(p.audioTrack, p.silentMedia, 0, kSec) == "this media has no sound for an audio track");
    CHECK(p.tryPush<AddClipCommand>(p.videoTrack, p.wavMedia, 0, kSec) == "audio-only media goes on an audio track");
    CHECK_FALSE(p.tryPush<AddClipCommand>(p.audioTrack, p.wavMedia, 0, kSec));
    CHECK_FALSE(p.tryPush<AddClipCommand>(p.audioTrack, p.avMedia, 2 * kSec, kSec)); // sound of a video file
}

TEST_CASE("clip volume and fades are validated and undoable")
{
    AvProject p;
    const Id clip = p.push<AddClipCommand>(p.videoTrack, p.avMedia, 0, 10 * kSec).clipId();

    p.push<SetClipAudioCommand>(clip, ClipAudio{-6.0f, 2 * kSec, 3 * kSec});
    CHECK(p.clip(clip).gainDb == -6.0f);
    CHECK(p.clip(clip).fadeOut == 3 * kSec);

    CHECK(p.tryPush<SetClipAudioCommand>(clip, ClipAudio{20.0f, 0, 0}));            // too loud
    CHECK(p.tryPush<SetClipAudioCommand>(clip, ClipAudio{0.0f, 6 * kSec, 5 * kSec})); // fades overlap

    p.stack.undo();
    CHECK(p.clip(clip).gainDb == 0.0f);
    CHECK(p.clip(clip).fadeIn == 0);
}

TEST_CASE("split keeps the fade-in on the left and the fade-out on the right")
{
    AvProject p;
    const Id clip = p.push<AddClipCommand>(p.videoTrack, p.avMedia, 0, 10 * kSec).clipId();
    p.push<SetClipAudioCommand>(clip, ClipAudio{-3.0f, 2 * kSec, 2 * kSec});
    const Id right = p.push<SplitClipCommand>(clip, 5 * kSec).rightClipId();

    CHECK(p.clip(clip).fadeIn == 2 * kSec);
    CHECK(p.clip(clip).fadeOut == 0);
    CHECK(p.clip(right).fadeIn == 0);
    CHECK(p.clip(right).fadeOut == 2 * kSec);
    CHECK(p.clip(right).gainDb == -3.0f);
}

TEST_CASE("trimming shortens fades that no longer fit")
{
    AvProject p;
    const Id clip = p.push<AddClipCommand>(p.videoTrack, p.avMedia, 0, 10 * kSec).clipId();
    p.push<SetClipAudioCommand>(clip, ClipAudio{0.0f, 3 * kSec, 4 * kSec});
    p.push<TrimClipCommand>(clip, TrimClipCommand::Edge::End, 5 * kSec);
    CHECK(p.clip(clip).fadeIn == 3 * kSec);
    CHECK(p.clip(clip).fadeOut == 2 * kSec);
}

TEST_CASE("track flags toggle, even on a locked track, and undo")
{
    AvProject p;
    using Flag = SetTrackFlagCommand::Flag;
    p.push<SetTrackFlagCommand>(p.videoTrack, Flag::Locked, true);
    CHECK(p.project.findTrack(p.videoTrack)->locked);
    CHECK(p.tryPush<AddClipCommand>(p.videoTrack, p.avMedia, 0, kSec) == "track is locked");
    CHECK(p.stack.undoLabel() == "Lock Track");
    p.push<SetTrackFlagCommand>(p.videoTrack, Flag::Locked, false); // unlocking a locked track works
    p.push<SetTrackFlagCommand>(p.audioTrack, Flag::Muted, true);
    CHECK(p.project.findTrack(p.audioTrack)->muted);
    CHECK(p.tryPush<SetTrackFlagCommand>(p.audioTrack, Flag::Hidden, true)); // audio can't hide
    p.stack.undo();
    CHECK_FALSE(p.project.findTrack(p.audioTrack)->muted);
}

TEST_CASE("detaching audio moves the sound to an audio track")
{
    AvProject p;
    const Id clip = p.push<AddClipCommand>(p.videoTrack, p.avMedia, 2 * kSec, 5 * kSec, kSec).clipId();
    p.push<SetClipAudioCommand>(clip, ClipAudio{-4.0f, kSec, 0});
    const Id audio = p.push<DetachAudioCommand>(clip, p.audioTrack).audioClipId();

    CHECK_FALSE(p.clip(clip).audioEnabled);
    const Clip& detached = p.clip(audio);
    CHECK(detached.start == 2 * kSec);
    CHECK(detached.duration == 5 * kSec);
    CHECK(detached.sourceIn == kSec);
    CHECK(detached.gainDb == -4.0f);
    CHECK(detached.fadeIn == kSec);
    CHECK(p.project.locateClip(audio)->track->id == p.audioTrack);

    CHECK(p.tryPush<DetachAudioCommand>(clip, p.audioTrack)); // already detached

    p.stack.undo();
    CHECK(p.clip(clip).audioEnabled);
    CHECK_FALSE(p.project.locateClip(audio));
}

TEST_CASE("detach refuses silent media and occupied audio tracks")
{
    AvProject p;
    const Id silent = p.push<AddClipCommand>(p.videoTrack, p.silentMedia, 0, kSec).clipId();
    CHECK(p.tryPush<DetachAudioCommand>(silent, p.audioTrack) == "this clip has no sound");

    const Id av = p.push<AddClipCommand>(p.videoTrack, p.avMedia, 5 * kSec, 5 * kSec).clipId();
    p.push<AddClipCommand>(p.audioTrack, p.wavMedia, 6 * kSec, kSec);
    CHECK(p.tryPush<DetachAudioCommand>(av, p.audioTrack) == "the audio track is not free at this clip's position");
}

TEST_CASE("audio fields survive save and load, and old files default them")
{
    AvProject p;
    const Id clip = p.push<AddClipCommand>(p.videoTrack, p.avMedia, 0, 10 * kSec).clipId();
    p.push<SetClipAudioCommand>(clip, ClipAudio{-9.5f, kSec, 2 * kSec});
    const Project loaded = projectFromJson(projectToJson(p.project));
    CHECK(loaded == p.project);

    auto json = projectToJson(p.project);
    for (auto& track : json["tracks"])
        for (auto& c : track["clips"]) {
            c.erase("gainDb");
            c.erase("fadeIn");
            c.erase("audioEnabled");
        }
    for (auto& m : json["media"])
        m.erase("hasAudio");
    const Project old = projectFromJson(json);
    CHECK(old.media.front().hasAudio == false);
    const Clip& c = old.tracks[1].clips.front();
    CHECK(c.gainDb == 0.0f);
    CHECK(c.audioEnabled);
}

TEST_CASE("audio effects, keep pitch and track mix are commands, kept by splits and saved")
{
    AvProject p;
    const Id clip = p.push<AddClipCommand>(p.audioTrack, p.wavMedia, 0, 4 * kSec, 0).clipId();
    const std::vector<ClipEffect> stack{{"tve.audio.hall", true, {{"mix", {0.5f}}}, {}},
                                        {"tve.audio.limiter", false, {}, {}}};
    p.push<SetClipAudioEffectsCommand>(clip, stack, "Add Audio Effect");
    CHECK(p.clip(clip).audioEffects == stack);
    p.stack.undo();
    CHECK(p.clip(clip).audioEffects.empty());
    p.stack.redo();

    CHECK(p.clip(clip).keepPitch); // the default
    auto& pitch = p.push<SetClipKeepPitchCommand>(clip, false);
    CHECK(pitch.label() == "Pitch Follows Speed");
    CHECK_FALSE(p.clip(clip).keepPitch);

    p.push<SetTrackMixCommand>(p.audioTrack, TrackMix{-3.0f, -0.5f, true});
    const Track& track = *p.project.findTrack(p.audioTrack);
    CHECK(track.gainDb == -3.0f);
    CHECK(track.pan == -0.5f);
    CHECK(track.solo);
    // Mixing is allowed on a locked track, as muting is.
    p.push<SetTrackFlagCommand>(p.audioTrack, SetTrackFlagCommand::Flag::Locked, true);
    p.push<SetTrackMixCommand>(p.audioTrack, TrackMix{-4.0f, -0.5f, true});
    CHECK(p.tryPush<SetClipKeepPitchCommand>(clip, true).value() == "track is locked");
    p.push<SetTrackFlagCommand>(p.audioTrack, SetTrackFlagCommand::Flag::Locked, false);

    // Both halves of a split keep them.
    auto& split = p.push<SplitClipCommand>(clip, 2 * kSec);
    CHECK(p.clip(split.rightClipId()).audioEffects == stack);
    CHECK_FALSE(p.clip(split.rightClipId()).keepPitch);

    // Saved; older projects get the defaults.
    nlohmann::json json = projectToJson(p.project);
    Project loaded = projectFromJson(json);
    CHECK(loaded.locateClip(clip)->clip->audioEffects == stack);
    CHECK_FALSE(loaded.locateClip(clip)->clip->keepPitch);
    CHECK(loaded.findTrack(p.audioTrack)->gainDb == -4.0f);
    CHECK(loaded.findTrack(p.audioTrack)->solo);
    for (auto& t : json["tracks"]) {
        t.erase("gainDb");
        t.erase("pan");
        t.erase("solo");
        for (auto& c : t["clips"]) {
            c.erase("audioEffects");
            c.erase("keepPitch");
        }
    }
    Project old = projectFromJson(json);
    CHECK(old.findTrack(p.audioTrack)->gainDb == 0.0f);
    CHECK_FALSE(old.findTrack(p.audioTrack)->solo);
    CHECK(old.locateClip(clip)->clip->keepPitch);
    CHECK(old.locateClip(clip)->clip->audioEffects.empty());

    // Only clips with sound; only tracks with sound; in range.
    const Id silent = p.push<AddClipCommand>(p.videoTrack, p.silentMedia, 0, kSec, 0).clipId();
    CHECK(p.tryPush<SetClipAudioEffectsCommand>(silent, stack, "x").value() == "this clip has no sound");
    CHECK(p.tryPush<SetTrackMixCommand>(p.audioTrack, TrackMix{20.0f, 0.0f, false}).value()
          == "volume is out of range");
}

TEST_CASE("enhanced sound is a command, kept by splits and saved")
{
    AvProject p;
    const Id clip = p.push<AddClipCommand>(p.audioTrack, p.wavMedia, 0, 4 * kSec, 0).clipId();
    const EnhancedAudio cleaned{"/enhanced/v1_123-dfn3-12dB.wav", 12};
    auto& enhance = p.push<SetClipEnhancedAudioCommand>(clip, cleaned);
    CHECK(enhance.label() == "Enhance Speech");
    CHECK(p.clip(clip).enhancedAudio == cleaned);
    p.stack.undo();
    CHECK_FALSE(p.clip(clip).enhancedAudio);
    p.stack.redo();

    auto& split = p.push<SplitClipCommand>(clip, 2 * kSec);
    CHECK(p.clip(split.rightClipId()).enhancedAudio == cleaned);

    nlohmann::json json = projectToJson(p.project);
    CHECK(projectFromJson(json).locateClip(clip)->clip->enhancedAudio == cleaned);
    for (auto& t : json["tracks"])
        for (auto& c : t["clips"])
            c.erase("enhancedAudio");
    CHECK_FALSE(projectFromJson(json).locateClip(clip)->clip->enhancedAudio);

    CHECK(p.push<SetClipEnhancedAudioCommand>(clip, std::nullopt).label() == "Original Sound");
    CHECK_FALSE(p.clip(clip).enhancedAudio);
    // Only clips with sound.
    const Id silent = p.push<AddClipCommand>(p.videoTrack, p.silentMedia, 0, kSec, 0).clipId();
    CHECK(p.tryPush<SetClipEnhancedAudioCommand>(silent, cleaned).value() == "this clip has no sound");
}
