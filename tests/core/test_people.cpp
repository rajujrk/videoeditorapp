#include "commands/CommandStack.h"
#include "commands/EditCommands.h"
#include "io/ProjectSerializer.h"
#include "model/People.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <cmath>

using namespace tve;
using Catch::Approx;

namespace {

constexpr Ticks kSec = kTicksPerSecond;
constexpr Ticks kFrame = kSec / 30;

// Two faces walking across 3 s at 30 fps: one left to right, one standing still on the
// right for the first 2 s; a missed detection now and then; a one-frame false detection.
std::vector<FaceFrame> walkingFaces()
{
    std::vector<FaceFrame> frames;
    for (int i = 0; i < 90; ++i) {
        FaceFrame f{i * kFrame, {}};
        if (i % 17 != 5) // missed now and then
            f.faces.push_back({0.1 + 0.6 * i / 89.0, 0.4, 0.1, 0.15});
        if (i < 60)
            f.faces.insert(f.faces.begin(), {0.85, 0.5, 0.08, 0.12}); // listed first: order must not matter
        if (i == 40)
            f.faces.push_back({0.3, 0.9, 0.05, 0.05});
        frames.push_back(std::move(f));
    }
    return frames;
}

struct PeopleProject {
    Project project;
    CommandStack stack{project};
    Id track = kNoId;
    Id video = kNoId;
    Id still = kNoId;

    PeopleProject()
    {
        track = push<AddTrackCommand>(TrackKind::Video, "V1").trackId();
        video = push<ImportMediaCommand>(MediaItem{.path = "/talk.mp4", .duration = 20 * kSec, .width = 1920, .height = 1080})
                    .mediaId();
        still = push<ImportMediaCommand>(MediaItem{.path = "/photo.jpg", .duration = 5 * kSec, .width = 800, .height = 600,
                                                   .still = true})
                    .mediaId();
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

TEST_CASE("detections become face tracks: linked, gaps bridged, flukes dropped, samples thinned")
{
    const auto tracks = linkFaces(walkingFaces());
    REQUIRE(tracks.size() == 2);
    // Numbered in order of first appearance (both at 0: the still one was listed first).
    CHECK(tracks[0].id == 1);
    CHECK(tracks[1].id == 2);
    const FaceTrack& still = tracks[0].boxAt(kSec)->x > 0.8 ? tracks[0] : tracks[1];
    const FaceTrack& walker = &still == &tracks[0] ? tracks[1] : tracks[0];
    CHECK(still.start() == 0);
    CHECK(still.end() == 59 * kFrame);
    CHECK(walker.end() == 89 * kFrame);
    // A straight walk and a still face need only their ends (and the missed frames are bridged).
    CHECK(walker.samples.size() <= 4);
    CHECK(still.samples.size() == 2);
    CHECK(walker.boxAt(45 * kFrame)->x == Approx(0.1 + 0.6 * 45 / 89.0).margin(0.01));
    CHECK(walker.boxAt(5 * kFrame)->x == Approx(0.1 + 0.6 * 5 / 89.0).margin(0.01)); // a missed frame
    // Gone after its last frame (with a little slack), and before its first.
    CHECK(still.boxAt(59 * kFrame + kSec / 20));
    CHECK_FALSE(still.boxAt(2 * kSec + kSec / 2));
    CHECK_FALSE(walker.boxAt(-kSec));

    // A face lost for longer than maxGap comes back as a new track.
    std::vector<FaceFrame> frames;
    for (int i = 0; i < 90; ++i)
        frames.push_back({i * kFrame, i < 20 || i > 60 ? std::vector<FaceBox>{{0.5, 0.5, 0.2, 0.2}} : std::vector<FaceBox>{}});
    CHECK(linkFaces(frames).size() == 2);
    // A fast move (little overlap between frames) still follows the same face.
    frames.clear();
    for (int i = 0; i < 30; ++i)
        frames.push_back({i * kFrame, {{0.1 + 0.025 * i, 0.5, 0.03, 0.03}}});
    CHECK(linkFaces(frames).size() == 1);
}

TEST_CASE("covered faces are their tracks' boxes, grown, at most 16 at once")
{
    FaceMosaic mosaic;
    CHECK_FALSE(mosaic.active());
    mosaic.expand = 0.5;
    mosaic.faces = linkFaces(walkingFaces());
    CHECK(mosaic.active());
    const auto boxes = mosaic.boxesAt(kSec);
    REQUIRE(boxes.size() == 2);
    for (const FaceBox& b : boxes)
        CHECK((b.width == Approx(0.15).margin(1e-6) || b.width == Approx(0.12).margin(1e-6)));
    CHECK(mosaic.boxesAt(5 * kSec).empty());

    for (int i = 0; i < 20; ++i)
        mosaic.faces.push_back({100 + i, {{0, {0.5, 0.5, 0.1, 0.1}}, {kSec * 3, {0.5, 0.5, 0.1, 0.1}}}});
    CHECK(mosaic.boxesAt(kSec).size() == kMaxCoveredFaces);
}

TEST_CASE("reframing fills the canvas and keeps the subject in view, smoothly")
{
    // A landscape picture onto a portrait canvas: 1920 × 1080 → 1080 × 1920.
    std::vector<SubjectSample> subject;
    for (int i = 0; i <= 60; ++i) // 6 s at 10 Hz: the subject waits at 0.3 for 1 s, then walks to the right edge
        subject.push_back({i * kSec / 10, {i < 10 ? 0.3 : 0.3 + 0.7 * (i - 10) / 50.0, 0.5, 0.1, 0.2}});
    const ReframePath path = reframe(subject, 1920, 1080, 1080, 1920);
    // fit = 1080/1920 (width), fill = 1920/1080 (height): scale = fill / fit.
    CHECK(path.scale == Approx((1920.0 / 1080.0) / (1080.0 / 1920.0)));
    REQUIRE(path.positions.size() >= 2);
    const double shownW = 1920.0 * 1920.0 / 1080.0;
    const double room = (shownW - 1080.0) / (2.0 * 1080.0);
    for (const auto& [time, xy] : path.positions) {
        CHECK(std::abs(xy[0]) <= room + 1e-9); // never shows an edge
        CHECK(xy[1] == Approx(0.0));            // no room up and down
    }
    // Early on the subject (at 0.3) is centred: the picture moves right by 0.2 of its width.
    CHECK(path.positions.front().second[0] == Approx(0.2 * shownW / 1080.0).margin(0.02));
    // At the end it is at the right edge: the picture goes as far left as it can.
    CHECK(path.positions.back().second[0] == Approx(-room).margin(1e-6));
    // A steady walk needs few keyframes.
    CHECK(path.positions.size() < 15);

    // Jitter is smoothed away: a subject shaking ±0.05 every sample barely moves the frame.
    subject.clear();
    for (int i = 0; i <= 50; ++i)
        subject.push_back({i * kSec / 10, {0.5 + (i % 2 ? 0.05 : -0.05), 0.5, 0.1, 0.2}});
    for (const auto& [time, xy] : reframe(subject, 1920, 1080, 1080, 1920, {.smoothing = 0.8}).positions)
        CHECK(std::abs(xy[0]) < 0.02);

    // The same shape: nothing to fill, nowhere to move.
    const ReframePath same = reframe(subject, 1920, 1080, 1920, 1080);
    CHECK(same.scale == Approx(1.0));
    for (const auto& [time, xy] : same.positions)
        CHECK(xy[0] == Approx(0.0));
    // No subject found: centred.
    CHECK(reframe({}, 1920, 1080, 1080, 1920).positions.empty());
}

TEST_CASE("people settings are one command, kept by splits and saved")
{
    PeopleProject p;
    const Id clip = p.push<AddClipCommand>(p.track, p.video, 0, 10 * kSec, 0).clipId();
    PortraitCutout cutout{true, false, 0.4, 0.1};
    FaceMosaic faces;
    faces.style = FaceCover::Blur;
    faces.faces = linkFaces(walkingFaces());
    p.push<SetClipPeopleCommand>(clip, cutout, faces, "Portrait Cutout");
    CHECK(p.clip(clip).cutout == cutout);
    CHECK(p.clip(clip).faceMosaic == faces);
    CHECK(p.stack.undoLabel() == "Portrait Cutout");
    p.stack.undo();
    CHECK_FALSE(p.clip(clip).cutout.enabled);
    CHECK_FALSE(p.clip(clip).faceMosaic.active());
    p.stack.redo();

    // Only what is given changes.
    p.push<SetClipPeopleCommand>(clip, std::nullopt, FaceMosaic{}, "Uncover Faces");
    CHECK(p.clip(clip).cutout == cutout);
    CHECK_FALSE(p.clip(clip).faceMosaic.active());
    p.stack.undo();

    auto& split = p.push<SplitClipCommand>(clip, 4 * kSec);
    CHECK(p.clip(split.rightClipId()).cutout == cutout);
    CHECK(p.clip(split.rightClipId()).faceMosaic == faces);

    nlohmann::json json = projectToJson(p.project);
    const Project loaded = projectFromJson(json);
    const Clip& again = *const_cast<Project&>(loaded).locateClip(clip)->clip;
    CHECK(again.cutout == cutout);
    CHECK(again.faceMosaic == faces);
    CHECK(json.dump().find("\"blur\"") != std::string::npos);
    for (auto& t : json["tracks"])
        for (auto& c : t["clips"]) {
            c.erase("cutout");
            c.erase("faceMosaic");
        }
    Project old = projectFromJson(json);
    CHECK_FALSE(old.locateClip(clip)->clip->cutout.enabled);
    CHECK_FALSE(old.locateClip(clip)->clip->faceMosaic.active());

    // Only video files; settings in range.
    const Id photo = p.push<AddClipCommand>(p.track, p.still, 20 * kSec, 2 * kSec, 0).clipId();
    CHECK(p.tryPush<SetClipPeopleCommand>(photo, cutout, std::nullopt, "Portrait Cutout").value()
          == "only clips of video files can find people");
    CHECK(p.tryPush<SetClipPeopleCommand>(clip, PortraitCutout{true, false, 2.0, 0.0}, std::nullopt, "x").value()
          == "cutout setting out of range");
    FaceMosaic tooMuch;
    tooMuch.amount = 1.5;
    CHECK(p.tryPush<SetClipPeopleCommand>(clip, std::nullopt, tooMuch, "x").value() == "face cover setting out of range");
}

TEST_CASE("a processed copy and a smart cutout's subject are commands, kept by splits and saved")
{
    PeopleProject p;
    const Id clip = p.push<AddClipCommand>(p.track, p.video, 0, 10 * kSec, 2 * kSec).clipId();
    const ProcessedVideo copy{"/processed/x-smooth4.mov", 2 * kSec, 12 * kSec, {"remove", "smooth4"}};
    p.push<SetClipProcessedVideoCommand>(clip, copy, "Smooth Slow Motion");
    CHECK(p.clip(clip).processedVideo == copy);
    auto& split = p.push<SplitClipCommand>(clip, 4 * kSec);
    CHECK(p.clip(split.rightClipId()).processedVideo == copy);
    p.stack.undo();
    p.stack.undo();
    CHECK_FALSE(p.clip(clip).processedVideo);
    p.stack.redo();
    CHECK(p.tryPush<SetClipProcessedVideoCommand>(clip, ProcessedVideo{"/x.mov", kSec, kSec, {}}, "x").value()
          == "a processed copy must cover some time");

    PortraitCutout subject{true, false, 0.3, 0.0, {{0, 0.3, 0.5, 0.2, 0.4, 0.0}, {2 * kSec, 0.7, 0.5, 0.2, 0.4, 0.0}}};
    p.push<SetClipPeopleCommand>(clip, subject, std::nullopt, "Cut Out Subject");
    // Between its samples the subject's box moves; before and after, it is held.
    CHECK(p.clip(clip).cutout.subjectAt(kSec)->x == Approx(0.5));
    CHECK(p.clip(clip).cutout.subjectAt(5 * kSec)->x == Approx(0.7));
    CHECK_FALSE(PortraitCutout{}.subjectAt(kSec));

    const Project loaded = projectFromJson(projectToJson(p.project));
    const Clip& again = *const_cast<Project&>(loaded).locateClip(clip)->clip;
    CHECK(again.processedVideo == copy);
    CHECK(again.cutout == subject);
    p.push<SetClipProcessedVideoCommand>(clip, std::nullopt, "Original Picture");
    CHECK_FALSE(p.clip(clip).processedVideo);
}
