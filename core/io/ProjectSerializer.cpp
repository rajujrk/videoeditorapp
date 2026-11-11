#include "io/ProjectSerializer.h"

#include <filesystem>
#include <functional>
#include <fstream>
#include <set>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace tve {

NLOHMANN_JSON_SERIALIZE_ENUM(TrackKind, {
    {TrackKind::Video, "video"},
    {TrackKind::Audio, "audio"},
    {TrackKind::Title, "title"},
})

void to_json(nlohmann::json& j, const FrameRate& r) { j = {{"num", r.num}, {"den", r.den}}; }
void from_json(const nlohmann::json& j, FrameRate& r)
{
    j.at("num").get_to(r.num);
    j.at("den").get_to(r.den);
}

NLOHMANN_JSON_SERIALIZE_ENUM(ProjectColour, {
    {ProjectColour::Sdr, "sdr"},
    {ProjectColour::HdrPq, "hdr-pq"},
    {ProjectColour::HdrHlg, "hdr-hlg"},
})
NLOHMANN_JSON_SERIALIZE_ENUM(Primaries, {
    {Primaries::Rec709, "709"},
    {Primaries::DisplayP3, "p3"},
    {Primaries::Rec2020, "2020"},
})
NLOHMANN_JSON_SERIALIZE_ENUM(Transfer, {
    {Transfer::Sdr, "sdr"},
    {Transfer::Linear, "linear"},
    {Transfer::Pq, "pq"},
    {Transfer::Hlg, "hlg"},
})
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ColourSpace, primaries, transfer)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ProjectSettings, width, height, frameRate, sampleRate, colour, spherical)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(GeneratedBy, provider, model, kind, prompt, reference, source, created)
NLOHMANN_JSON_SERIALIZE_ENUM(ColourFillKind, {
    {ColourFillKind::Solid, "solid"},
    {ColourFillKind::Linear, "linear"},
    {ColourFillKind::Radial, "radial"},
})
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ColourFill, kind, from, to, angle)
// Fields added after schema 1 shipped default when absent, so older files still load.
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(MediaItem, id, path, duration, hasVideo, hasAudio, width, height,
                                                frameRate, audioSampleRate, audioChannels, still, alpha, lottie,
                                                fingerprint, generated, placeholder, colour, spherical, colourFill)
NLOHMANN_JSON_SERIALIZE_ENUM(BlendMode, {
    {BlendMode::Normal, "normal"},
    {BlendMode::Multiply, "multiply"},
    {BlendMode::Screen, "screen"},
    {BlendMode::Overlay, "overlay"},
    {BlendMode::Add, "add"},
    {BlendMode::Darken, "darken"},
    {BlendMode::Lighten, "lighten"},
})
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ClipTransform, x, y, scale, rotation, anchorX, anchorY)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ClipCrop, left, top, right, bottom)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ClipSphereView, yaw, pitch, roll, fov)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ClipCornerPin, topLeftX, topLeftY, topRightX, topRightY, bottomRightX,
                                                bottomRightY, bottomLeftX, bottomLeftY)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ClipVisual, transform, crop, opacity, blend, sphere, pin)

NLOHMANN_JSON_SERIALIZE_ENUM(TransitionAlignment, {
    {TransitionAlignment::Center, "center"},
    {TransitionAlignment::EndsAtCut, "endsAtCut"},
    {TransitionAlignment::StartsAtCut, "startsAtCut"},
})
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ClipTransition, transitionId, duration, alignment, values)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ColorRgba, r, g, b, a)
NLOHMANN_JSON_SERIALIZE_ENUM(TextAlignment, {
    {TextAlignment::Left, "left"},
    {TextAlignment::Center, "center"},
    {TextAlignment::Right, "right"},
})
NLOHMANN_JSON_SERIALIZE_ENUM(TitleShapeKind, {
    {TitleShapeKind::None, "none"},
    {TitleShapeKind::Box, "box"},
    {TitleShapeKind::Rounded, "rounded"},
    {TitleShapeKind::Pill, "pill"},
    {TitleShapeKind::Speech, "speech"},
    {TitleShapeKind::Thought, "thought"},
    {TitleShapeKind::Banner, "banner"},
    {TitleShapeKind::Underline, "underline"},
    {TitleShapeKind::Circle, "circle"},
    {TitleShapeKind::Arrow, "arrow"},
    {TitleShapeKind::Tag, "tag"},
})
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(TitleShape, kind, color, padding, tail)
NLOHMANN_JSON_SERIALIZE_ENUM(TextUnit, {
    {TextUnit::Whole, "whole"},
    {TextUnit::Line, "line"},
    {TextUnit::Word, "word"},
    {TextUnit::Character, "character"},
})
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(TextAnimation, preset, duration, unit, stagger)
NLOHMANN_JSON_SERIALIZE_ENUM(TextScroll, {
    {TextScroll::None, "none"},
    {TextScroll::RollUp, "rollUp"},
    {TextScroll::CrawlLeft, "crawlLeft"},
})
NLOHMANN_JSON_SERIALIZE_ENUM(CaptionMotion, {
    {CaptionMotion::None, "none"},
    {CaptionMotion::Highlight, "highlight"},
    {CaptionMotion::Pop, "pop"},
    {CaptionMotion::Karaoke, "karaoke"},
    {CaptionMotion::Reveal, "reveal"},
})
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(CaptionWord, start, end, text)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ClipText, text, fontFamily, size, bold, italic, color, alignment,
                                                lineSpacing, letterSpacing, outlineColor, outlineWidth, shadow, shape,
                                                animationIn, animationOut, animationLoop, scroll, words,
                                                captionMotion, highlightColor, path)
NLOHMANN_JSON_SERIALIZE_ENUM(PathKind, {
    {PathKind::Brush, "brush"},
    {PathKind::Line, "line"},
    {PathKind::Arrow, "arrow"},
    {PathKind::Rectangle, "rectangle"},
    {PathKind::Ellipse, "ellipse"},
})
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(DrawnPath, kind, points, stroke, width, fill)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ClipDrawing, paths, drawIn, drawOut, oneByOne)
NLOHMANN_JSON_SERIALIZE_ENUM(Easing, {
    {Easing::Linear, "linear"},
    {Easing::EaseIn, "easeIn"},
    {Easing::EaseOut, "easeOut"},
    {Easing::EaseInOut, "easeInOut"},
    {Easing::Hold, "hold"},
    {Easing::Curve, "curve"},
})
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(EasingCurve, x1, y1, x2, y2)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Keyframe, time, value, easing, curve)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ClipEffect, effectId, enabled, values, keyframes, state)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SpeedPoint, at, speed)
NLOHMANN_JSON_SERIALIZE_ENUM(MaskShape, {
    {MaskShape::Rectangle, "rectangle"},
    {MaskShape::Ellipse, "ellipse"},
    {MaskShape::Line, "line"},
    {MaskShape::Star, "star"},
    {MaskShape::Heart, "heart"},
    {MaskShape::Pen, "pen"},
})
NLOHMANN_JSON_SERIALIZE_ENUM(MaskTarget, {
    {MaskTarget::Picture, "picture"},
    {MaskTarget::Effects, "effects"},
    {MaskTarget::Colour, "colour"},
})
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ClipMask, shape, x, y, width, height, rotation, roundness, feather,
                                                invert, points, target)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ChromaKey, enabled, color, tolerance, softness, spill)
NLOHMANN_JSON_SERIALIZE_ENUM(MatteMode, {
    {MatteMode::None, "none"},
    {MatteMode::Alpha, "alpha"},
    {MatteMode::InvertedAlpha, "invertedAlpha"},
    {MatteMode::Luma, "luma"},
    {MatteMode::InvertedLuma, "invertedLuma"},
})
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(CurvePoint, x, y)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ColorWheel, x, y, master)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(HslBand, hue, saturation, luminance)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ColorGrade, exposure, contrast, saturation, temperature, tint, lift,
                                                gamma, gain, offset, master, red, green, blue, hsl, hueVsSat, hueVsHue,
                                                lumaVsSat, lutPath, lutIntensity)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ClipLook, id, title, intensity, grade)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(EnhancedAudio, path, limitDb)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ProcessedVideo, path, from, to, steps)
void to_json(nlohmann::json& j, const CursorSample& c)
{
    j = nlohmann::json::array({c.time, c.x, c.y});
}
void from_json(const nlohmann::json& j, CursorSample& c)
{
    c = {j.at(0).get<Ticks>(), j.at(1).get<double>(), j.at(2).get<double>()};
}
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(CursorEffects, path, clicks, rings, highlight, spotlight, size)
void to_json(nlohmann::json& j, const TrackSample& s)
{
    j = nlohmann::json::array({s.time, s.x, s.y, s.width, s.height, s.angle});
}
void from_json(const nlohmann::json& j, TrackSample& s)
{
    s = {j.at(0).get<Ticks>(), j.at(1).get<double>(), j.at(2).get<double>(), j.at(3).get<double>(),
         j.at(4).get<double>(), j.at(5).get<double>()};
}
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PortraitCutout, enabled, invert, softness, shift, subject)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(FaceBox, x, y, width, height)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(FaceSample, time, box)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(FaceTrack, id, samples)
NLOHMANN_JSON_SERIALIZE_ENUM(FaceCover, {
    {FaceCover::Mosaic, "mosaic"},
    {FaceCover::Blur, "blur"},
    {FaceCover::Fill, "fill"},
})
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(FaceMosaic, style, amount, expand, faces)
// A correction or gain per frame: kept as short arrays, [time, dx, dy, angle, scale] and
// [time, gain], since a clip has one for every frame.
void to_json(nlohmann::json& j, const SteadySample& s)
{
    j = nlohmann::json::array({s.time, s.dx, s.dy, s.angle, s.scale});
}
void from_json(const nlohmann::json& j, SteadySample& s)
{
    s = {j.at(0).get<Ticks>(), j.at(1).get<double>(), j.at(2).get<double>(), j.at(3).get<double>(), j.at(4).get<double>()};
}
void to_json(nlohmann::json& j, const GainSample& g)
{
    j = nlohmann::json::array({g.time, g.gain});
}
void from_json(const nlohmann::json& j, GainSample& g)
{
    g = {j.at(0).get<Ticks>(), j.at(1).get<float>()};
}
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Stabilization, enabled, smoothness, lock, maxZoom, zoom, samples)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(MotionTrack, id, surface, samples)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Deflicker, enabled, strength, samples)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Clip, id, mediaId, start, duration, sourceIn, audioEnabled, gainDb,
                                                fadeIn, fadeOut, visual, effects, speed, reversed, freeze,
                                                transitionIn, text, keyframes, speedRamp, color, look, adjustment,
                                                chromaKey, masks, matte, cutout, faceMosaic, stabilization, deflicker,
                                                denoise, motionTracks, processedVideo, cursor, audioEffects,
                                                enhancedAudio, keepPitch, sequenceId, groupId, drawing, angles, motionBlur)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Track, id, kind, name, locked, hidden, muted, gainDb, pan, solo, captions,
                                                clips, audioEffects)
NLOHMANN_JSON_SERIALIZE_ENUM(MarkerColor, {
    {MarkerColor::Red, "red"},
    {MarkerColor::Orange, "orange"},
    {MarkerColor::Yellow, "yellow"},
    {MarkerColor::Green, "green"},
    {MarkerColor::Blue, "blue"},
    {MarkerColor::Purple, "purple"},
})
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Marker, id, time, duration, color, note, chapter)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Sequence, id, name, tracks)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(TemplateSlot, clipId, label, kind, filled)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(TemplateText, clipId, label)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(TemplateInfo, id, name, mediaSlots, texts, musicClipId)

nlohmann::json projectToJson(const Project& project)
{
    nlohmann::json json{
        {"schema", kProjectSchemaName},
        {"version", kProjectSchemaVersion},
        {"nextId", project.nextId()},
        {"settings", project.settings},
        {"media", project.media},
        {"tracks", project.tracks},
        {"markers", project.markers},
        {"sequences", project.sequences},
    };
    if (project.templateInfo)
        json["template"] = *project.templateInfo;
    return json;
}

Project projectFromJson(const nlohmann::json& json)
{
    if (!json.is_object() || json.value("schema", "") != kProjectSchemaName)
        throw std::runtime_error("not a TechieVideoEditor project");
    const int version = json.at("version").get<int>();
    if (version > kProjectSchemaVersion)
        throw std::runtime_error("project was saved by a newer version (schema " + std::to_string(version) + ")");

    Project project;
    json.at("settings").get_to(project.settings);
    json.at("media").get_to(project.media);
    json.at("tracks").get_to(project.tracks);
    // Added after schema 1 shipped: absent in older projects.
    if (json.contains("markers"))
        json.at("markers").get_to(project.markers);
    if (json.contains("sequences"))
        json.at("sequences").get_to(project.sequences);
    if (json.contains("template"))
        project.templateInfo = json.at("template").get<TemplateInfo>();
    project.setNextId(json.at("nextId").get<Id>());
    return project;
}

void saveProject(const Project& project, const std::filesystem::path& path)
{
    // Write to a temporary file and rename so a crash never leaves a half-written project.
    const auto temp = std::filesystem::path(path).concat(".tmp");
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out)
            throw std::runtime_error("cannot write " + temp.string());
        out << projectToJson(project).dump(2);
        if (!out)
            throw std::runtime_error("failed writing " + temp.string());
    }
    std::error_code error;
    std::filesystem::rename(temp, path, error);
    if (error) {
        std::filesystem::remove(temp, error);
        throw std::runtime_error("cannot replace " + path.string());
    }
}

Project loadProject(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("cannot open " + path.string());
    return projectFromJson(nlohmann::json::parse(in));
}

nlohmann::json gradeToJson(const ColorGrade& grade)
{
    return grade;
}

ColorGrade gradeFromJson(const nlohmann::json& json)
{
    return json.get<ColorGrade>();
}

std::string renderedContent(const Project& project, Ticks from, Ticks to)
{
    std::set<Id> mediaUsed;
    const std::function<void(const std::vector<Track>&)> useMedia = [&](const std::vector<Track>& tracks) {
        for (const Track& track : tracks)
            for (const Clip& clip : track.clips)
                mediaUsed.insert(clip.mediaId);
    };
    nlohmann::json tracks = nlohmann::json::array();
    for (const Track& track : project.tracks) {
        if (track.kind == TrackKind::Audio)
            continue;
        Track shown = track;
        shown.name.clear();
        shown.locked = shown.muted = shown.solo = false;
        shown.gainDb = shown.pan = 0.0f;
        shown.audioEffects.clear();
        shown.clips.clear();
        const auto over = [&](std::size_t i) {
            const Clip& c = track.clips[i];
            return c.start < to && c.start + c.duration > from;
        };
        // A neighbour counts when it touches one over the stretch (a transition between them
        // reaches across the cut).
        const auto touches = [&](std::size_t a, std::size_t b) {
            return track.clips[a].start + track.clips[a].duration == track.clips[b].start;
        };
        for (std::size_t i = 0; i < track.clips.size(); ++i) {
            if (over(i) || (i > 0 && over(i - 1) && touches(i - 1, i))
                || (i + 1 < track.clips.size() && over(i + 1) && touches(i, i + 1))) {
                Clip clip = track.clips[i];
                clip.audioEffects.clear(); // what is heard doesn't show
                clip.gainDb = 0.0f;
                clip.fadeIn = clip.fadeOut = 0;
                clip.enhancedAudio.reset();
                shown.clips.push_back(std::move(clip));
                mediaUsed.insert(track.clips[i].mediaId);
            }
        }
        tracks.push_back(shown);
    }
    for (const Sequence& sequence : project.sequences)
        useMedia(sequence.tracks);
    nlohmann::json media = nlohmann::json::array();
    for (const MediaItem& item : project.media) {
        if (!mediaUsed.contains(item.id))
            continue;
        nlohmann::json entry = item;
        std::error_code error;
        const auto size = std::filesystem::file_size(item.path, error);
        entry["fileSize"] = error ? 0 : size;
        const auto modified = std::filesystem::last_write_time(item.path, error);
        entry["fileTime"] = error ? 0 : modified.time_since_epoch().count();
        media.push_back(std::move(entry));
    }
    return nlohmann::json{{"from", from},
                          {"to", to},
                          {"settings", project.settings},
                          {"tracks", tracks},
                          {"media", media},
                          {"sequences", project.sequences}}
        .dump();
}

} // namespace tve
