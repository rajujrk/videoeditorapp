#include "commands/CommandStack.h"
#include "commands/EditCommands.h"
#include "io/ProjectSerializer.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

using namespace tve;

namespace {

constexpr Ticks kSec = kTicksPerSecond;

MediaItem recording(const char* path, Ticks duration)
{
    return MediaItem{.path = path, .duration = duration, .hasVideo = false, .hasAudio = true,
                     .audioSampleRate = 48000, .audioChannels = 1};
}

// An audio track (bottom) and a video track.
struct VoiceProject {
    Project project;
    CommandStack stack{project};
    Id audio = kNoId;
    Id video = kNoId;

    VoiceProject()
    {
        audio = push<AddTrackCommand>(TrackKind::Audio, "Audio 1").trackId();
        video = push<AddTrackCommand>(TrackKind::Video, "Video 1").trackId();
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
};

} // namespace

TEST_CASE("a voice-over lands on the given audio track in one undo step", "[voiceover]")
{
    VoiceProject p;
    auto& command = p.push<AddVoiceOverCommand>(recording("/v/Voice-over 1.wav", 3 * kSec), p.audio, 2 * kSec);
    CHECK(command.label() == "Record Voice-over");
    CHECK(command.trackId() == p.audio);
    REQUIRE(p.project.media.size() == 1);
    CHECK(p.project.media[0].id == command.mediaId());
    CHECK(p.project.media[0].path == "/v/Voice-over 1.wav");
    const Track& track = *p.project.findTrack(p.audio);
    REQUIRE(track.clips.size() == 1);
    const Clip& clip = track.clips[0];
    CHECK(clip.id == command.clipId());
    CHECK(clip.mediaId == command.mediaId());
    CHECK(clip.start == 2 * kSec);
    CHECK(clip.duration == 3 * kSec);
    CHECK(clip.sourceIn == 0);
    CHECK(p.project.tracks.size() == 2);

    p.stack.undo();
    CHECK(p.project.media.empty());
    CHECK(p.project.findTrack(p.audio)->clips.empty());

    p.stack.redo(); // the same ids come back
    REQUIRE(p.project.media.size() == 1);
    CHECK(p.project.media[0].id == command.mediaId());
    CHECK(p.project.locateClip(command.clipId()));
}

TEST_CASE("a voice-over without a track gets a new one under the others", "[voiceover]")
{
    VoiceProject p;
    auto& first = p.push<AddVoiceOverCommand>(recording("/v/a.wav", kSec), kNoId, 0);
    REQUIRE(p.project.tracks.size() == 3);
    const Track& track = p.project.tracks.front(); // stored bottom to top
    CHECK(track.id == first.trackId());
    CHECK(track.kind == TrackKind::Audio);
    CHECK(track.name == "Voice-over");
    CHECK(track.clips.size() == 1);

    auto& second = p.push<AddVoiceOverCommand>(recording("/v/b.wav", kSec), kNoId, 0);
    CHECK(p.project.tracks.front().id == second.trackId());
    CHECK(p.project.tracks.front().name == "Voice-over 2");

    p.stack.undo();
    p.stack.undo();
    CHECK(p.project.tracks.size() == 2);
    CHECK(p.project.media.empty());
    p.stack.redo();
    CHECK(p.project.tracks.front().id == first.trackId());
}

TEST_CASE("a voice-over is refused where it cannot go", "[voiceover]")
{
    VoiceProject p;
    p.push<AddVoiceOverCommand>(recording("/v/a.wav", 4 * kSec), p.audio, 0);
    const auto refuse = [&](MediaItem item, Id track, Ticks start) {
        auto error = p.stack.push(std::make_unique<AddVoiceOverCommand>(std::move(item), track, start));
        REQUIRE(error);
        return *error;
    };
    CHECK(refuse(recording("/v/b.wav", kSec), p.audio, 3 * kSec) == "clip overlaps an existing clip");
    CHECK(refuse(recording("/v/b.wav", kSec), p.video, 0) == "voice-overs go on audio tracks");
    CHECK(refuse(recording("/v/b.wav", kSec), 999, 0) == "track not found");
    CHECK(refuse(recording("/v/b.wav", 0), p.audio, 5 * kSec) == "the recording has no sound");
    CHECK(refuse(recording("/v/b.wav", kSec), p.audio, -kSec) == "clip times are out of range");
    MediaItem silent = recording("/v/b.wav", kSec);
    silent.hasAudio = false;
    CHECK(refuse(silent, p.audio, 5 * kSec) == "the recording has no sound");

    p.push<SetTrackFlagCommand>(p.audio, SetTrackFlagCommand::Flag::Locked, true);
    CHECK(refuse(recording("/v/b.wav", kSec), p.audio, 5 * kSec) == "track is locked");
    // Nothing half-done is left behind.
    CHECK(p.project.media.size() == 1);
    CHECK(p.project.tracks.size() == 2);
}

TEST_CASE("a voice-over saves and loads like any audio clip", "[voiceover]")
{
    VoiceProject p;
    auto& command = p.push<AddVoiceOverCommand>(recording("/v/a.wav", 2 * kSec), kNoId, kSec);
    Project loaded = projectFromJson(projectToJson(p.project));
    const auto location = loaded.locateClip(command.clipId());
    REQUIRE(location);
    CHECK(location->track->name == "Voice-over");
    CHECK(location->track->kind == TrackKind::Audio);
    CHECK(location->clip->start == kSec);
    const MediaItem* media = loaded.findMedia(command.mediaId());
    REQUIRE(media);
    CHECK_FALSE(media->hasVideo);
    CHECK(media->hasAudio);
    CHECK(media->duration == 2 * kSec);
}
