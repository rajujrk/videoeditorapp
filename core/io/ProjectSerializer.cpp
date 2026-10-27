#include "io/ProjectSerializer.h"

#include <fstream>
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

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ProjectSettings, width, height, frameRate, sampleRate)
// Fields added after schema 1 shipped default when absent, so older files still load.
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(MediaItem, id, path, duration, hasVideo, hasAudio, width, height,
                                                frameRate, audioSampleRate, audioChannels)
NLOHMANN_JSON_SERIALIZE_ENUM(BlendMode, {
    {BlendMode::Normal, "normal"},
    {BlendMode::Multiply, "multiply"},
    {BlendMode::Screen, "screen"},
    {BlendMode::Overlay, "overlay"},
    {BlendMode::Add, "add"},
    {BlendMode::Darken, "darken"},
    {BlendMode::Lighten, "lighten"},
})
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ClipTransform, x, y, scale, rotation)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ClipCrop, left, top, right, bottom)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ClipVisual, transform, crop, opacity, blend)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ClipEffect, effectId, enabled, values)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Clip, id, mediaId, start, duration, sourceIn, audioEnabled, gainDb,
                                                fadeIn, fadeOut, visual, effects, speed, reversed, freeze)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Track, id, kind, name, locked, hidden, muted, clips)

nlohmann::json projectToJson(const Project& project)
{
    return {
        {"schema", kProjectSchemaName},
        {"version", kProjectSchemaVersion},
        {"nextId", project.nextId()},
        {"settings", project.settings},
        {"media", project.media},
        {"tracks", project.tracks},
    };
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
    std::filesystem::rename(temp, path);
}

Project loadProject(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("cannot open " + path.string());
    return projectFromJson(nlohmann::json::parse(in));
}

} // namespace tve
