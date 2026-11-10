#pragma once

#include "model/Project.h"

#include <filesystem>

#include <nlohmann/json_fwd.hpp>

namespace tve {

// project.json schema identity. Bump kProjectSchemaVersion on any incompatible change
// and add a migration in projectFromJson().
inline constexpr const char* kProjectSchemaName = "tve.project";
inline constexpr int kProjectSchemaVersion = 1;

nlohmann::json projectToJson(const Project& project);

// Throws std::runtime_error if the JSON is not a supported TVE project.
Project projectFromJson(const nlohmann::json& json);

// A colour grade on its own (colour presets).
nlohmann::json gradeToJson(const ColorGrade& grade);
ColorGrade gradeFromJson(const nlohmann::json& json);

// Everything the pictures of [from, to) depend on, as text (a render cache's key is made
// from it): the project's settings; the picture tracks with the clips over the stretch and
// those touching them (a transition reaches across a cut), without what doesn't show (names,
// mute, volume); the media those clips and the compound clips use, with each file's size
// and modification time; and the sequences.
std::string renderedContent(const Project& project, Ticks from, Ticks to);

void saveProject(const Project& project, const std::filesystem::path& path);
Project loadProject(const std::filesystem::path& path);

} // namespace tve
