#include "commands/MediaEdits.h"
#include "io/ProjectSerializer.h"

#include <nlohmann/json.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>

using namespace tve;

namespace {

constexpr Ticks kSec = kTicksPerSecond;

// Three videos, a song and a photo; the first video and the song on the timeline, the photo
// inside a compound clip's sequence.
struct Fixture {
    Project project;
    Id a = 0, b = 0, c = 0, song = 0, photo = 0;
    Id main = 0, sound = 0;
    Id clipA = 0, clipSong = 0;

    Fixture()
    {
        const auto media = [&](const char* path, bool video, bool audio, bool still = false) {
            MediaItem m{.id = project.allocateId(), .path = path, .duration = 4 * kSec, .hasVideo = video, .hasAudio = audio,
                        .still = still};
            project.media.push_back(m);
            return m.id;
        };
        a = media("/m/a.mov", true, true);
        b = media("/m/b.mov", true, false);
        c = media("/m/c.mov", true, true);
        song = media("/m/song.wav", false, true);
        photo = media("/m/photo.png", true, false, true);
        sound = project.allocateId();
        main = project.allocateId();
        project.tracks.push_back({.id = sound, .kind = TrackKind::Audio, .name = "Audio 1"});
        project.tracks.push_back({.id = main, .kind = TrackKind::Video, .name = "Video 1"});
        clipA = project.allocateId();
        project.findTrack(main)->insertClip({.id = clipA, .mediaId = a, .duration = 2 * kSec});
        clipSong = project.allocateId();
        project.findTrack(sound)->insertClip({.id = clipSong, .mediaId = song, .duration = 2 * kSec});
        Sequence inside{.id = project.allocateId(), .name = "Inside"};
        inside.tracks.push_back({.id = project.allocateId(), .kind = TrackKind::Video});
        inside.tracks[0].clips.push_back({.id = project.allocateId(), .mediaId = photo, .duration = kSec});
        project.sequences.push_back(inside);
    }
};

} // namespace

TEST_CASE("Folders hold media and other folders", "[media-edits]")
{
    Fixture f;
    Project& p = f.project;
    Id shoot = 0, day = 0, other = 0;
    REQUIRE_FALSE(addBin(p, "  Shoot ", kNoId, &shoot));
    CHECK(p.bins.back().name == "Shoot");
    REQUIRE_FALSE(addBin(p, "Day 2", shoot, &day));
    REQUIRE_FALSE(addBin(p, "Shoot", kNoId, &other));
    CHECK(p.bins.back().name == "Shoot 2"); // the same name in one place is made unique
    CHECK(addBin(p, "   ", kNoId, nullptr));
    CHECK(addBin(p, "x", 999, nullptr));
    CHECK(binPath(p, day) == std::vector<std::string>{"Shoot", "Day 2"});
    CHECK(binWithin(p, day, shoot));
    CHECK_FALSE(binWithin(p, shoot, day));

    // Media in, and a folder can't go inside itself.
    REQUIRE_FALSE(moveToBin(p, {f.a, f.b}, {}, day));
    CHECK(p.findMedia(f.a)->binId == day);
    CHECK(moveToBin(p, {}, {shoot}, day));
    CHECK(moveToBin(p, {}, {shoot}, shoot));
    REQUIRE_FALSE(moveToBin(p, {}, {other}, shoot)); // "Shoot 2" into Shoot
    CHECK(binPath(p, other) == std::vector<std::string>{"Shoot", "Shoot 2"});
    CHECK(moveToBin(p, {}, {}, shoot));
    CHECK(moveToBin(p, {12345}, {}, shoot));

    // Renamed: unique among its neighbours.
    REQUIRE_FALSE(renameBin(p, other, "Day 2"));
    CHECK(std::ranges::find(p.bins, other, &MediaBin::id)->name == "Day 2 2");
    CHECK(renameBin(p, other, ""));

    // Deleted: what was in it moves up.
    REQUIRE_FALSE(removeBin(p, shoot));
    CHECK(std::ranges::find(p.bins, day, &MediaBin::id)->parent == kNoId);
    CHECK(p.findMedia(f.a)->binId == day); // still in Day 2, now at the top
    REQUIRE_FALSE(removeBin(p, day));
    CHECK(p.findMedia(f.a)->binId == kNoId);
    CHECK(removeBin(p, day));

    // Saved and opened again.
    Id kept = 0;
    REQUIRE_FALSE(addBin(p, "Kept", kNoId, &kept));
    REQUIRE_FALSE(moveToBin(p, {f.song}, {}, kept));
    const Project reopened = projectFromJson(projectToJson(p));
    CHECK(reopened.bins == p.bins);
    CHECK(reopened.findMedia(f.song)->binId == kept);
    // A project without folders writes none (older versions read it as before).
    CHECK_FALSE(projectToJson(Fixture().project).contains("bins"));
}

TEST_CASE("Media takes a colour label", "[media-edits]")
{
    Fixture f;
    REQUIRE_FALSE(setMediaLabel(f.project, {f.a, f.c}, MediaLabel::Green));
    CHECK(f.project.findMedia(f.a)->label == MediaLabel::Green);
    CHECK(f.project.findMedia(f.b)->label == MediaLabel::None);
    CHECK(setMediaLabel(f.project, {}, MediaLabel::Red));
    CHECK(setMediaLabel(f.project, {999}, MediaLabel::Red));
    CHECK(projectFromJson(projectToJson(f.project)).findMedia(f.c)->label == MediaLabel::Green);
}

TEST_CASE("Media nobody uses is found and taken out", "[media-edits]")
{
    Fixture f;
    // a and the song on the timeline, the photo inside a compound clip: b and c are unused.
    CHECK(unusedMedia(f.project) == std::vector<Id>{f.b, f.c});
    CHECK(removeUnusedMedia(f.project) == 2);
    CHECK(f.project.media.size() == 3);
    CHECK(unusedMedia(f.project).empty());
    CHECK(removeUnusedMedia(f.project) == 0);
}

TEST_CASE("A clip's sound channels", "[media-edits]")
{
    Fixture f;
    REQUIRE_FALSE(setClipChannels(f.project, {f.clipSong, f.clipA}, AudioChannels::Left));
    CHECK(f.project.locateClip(f.clipSong)->clip->channels == AudioChannels::Left);
    CHECK(f.project.locateClip(f.clipA)->clip->channels == AudioChannels::Left);
    CHECK(projectFromJson(projectToJson(f.project)).tracks[0].clips[0].channels == AudioChannels::Left);
    // A clip with no sound can't.
    const Id silent = f.project.allocateId();
    f.project.findTrack(f.main)->insertClip({.id = silent, .mediaId = f.b, .start = 3 * kSec, .duration = kSec});
    CHECK(setClipChannels(f.project, {silent}, AudioChannels::Mono));
    f.project.findTrack(f.sound)->locked = true;
    CHECK(setClipChannels(f.project, {f.clipSong}, AudioChannels::Mono));
    CHECK(setClipChannels(f.project, {}, AudioChannels::Mono));
}

TEST_CASE("An archive takes every file the project needs, and points the project at the copies", "[media-edits]")
{
    Fixture f;
    Project& p = f.project;
    // A sequence of three frames on disk, cleaned sound, a LUT (twice) and a title's picture.
    const auto dir = std::filesystem::temp_directory_path() / "tve-archive-files-test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    for (int n = 1; n <= 3; ++n)
        std::ofstream(dir / ("shot_000" + std::to_string(n) + ".png")) << "x";
    MediaItem sequence{.id = p.allocateId(), .path = (dir / "shot_0001.png").string(), .duration = kSec, .hasVideo = true};
    sequence.sequence = ImageSequence{"shot_%04d.png", 1, 3};
    p.media.push_back(sequence);
    Clip& a = *p.locateClip(f.clipA)->clip;
    a.enhancedAudio = EnhancedAudio{"/app/enhanced.wav"};
    a.color.lutPath = "/luts/film.cube";
    Clip& song = *p.locateClip(f.clipSong)->clip;
    song.look = ClipLook{.id = "look"};
    song.look->grade.lutPath = "/luts/film.cube";
    const Id title = p.allocateId();
    p.findTrack(f.main)->insertClip({.id = title, .start = 3 * kSec, .duration = kSec,
                                     .text = ClipText{.fill = TextFill{.kind = TextFillKind::Image, .imagePath = "/pics/fill.png"}}});

    auto files = archiveFiles(p, false);
    const auto has = [&](const std::string& path) {
        return std::ranges::any_of(files, [&](const ArchiveFile& af) { return af.path == path; });
    };
    CHECK(has("/m/a.mov"));
    CHECK(has("/m/b.mov"));
    CHECK(has("/app/enhanced.wav"));
    CHECK(has("/pics/fill.png"));
    CHECK(std::ranges::count(files, std::string("/luts/film.cube"), &ArchiveFile::path) == 1); // once
    const auto frames = std::ranges::count_if(files, [](const ArchiveFile& af) { return af.folder == "shot_NNNN.png"; });
    CHECK(frames == 3);

    // Only what is used: b, c and the sequence (unused) stay out.
    files = archiveFiles(p, true);
    CHECK_FALSE(has("/m/b.mov"));
    CHECK_FALSE(has("/m/c.mov"));
    CHECK(has("/m/photo.png")); // inside the compound clip
    CHECK(std::ranges::none_of(files, [](const ArchiveFile& af) { return !af.folder.empty(); }));

    // Pointed at the copies.
    const Project moved = relocated(p, {{"/m/a.mov", "/archive/Media/a.mov"}, {"/luts/film.cube", "/archive/Media/film.cube"},
                                        {"/app/enhanced.wav", "/archive/Media/enhanced.wav"}},
                                    true);
    CHECK(moved.findMedia(f.a)->path == "/archive/Media/a.mov");
    CHECK_FALSE(moved.findMedia(f.b)); // unused, left out
    Project copy = moved;
    CHECK(copy.locateClip(f.clipA)->clip->color.lutPath == "/archive/Media/film.cube");
    CHECK(copy.locateClip(f.clipA)->clip->enhancedAudio->path == "/archive/Media/enhanced.wav");
    CHECK(copy.locateClip(f.clipSong)->clip->look->grade.lutPath == "/archive/Media/film.cube");
    CHECK(copy.findMedia(f.song)->path == "/m/song.wav"); // not named: stays
    std::filesystem::remove_all(dir);
}
