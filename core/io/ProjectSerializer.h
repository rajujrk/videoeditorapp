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

void saveProject(const Project& project, const std::filesystem::path& path);
Project loadProject(const std::filesystem::path& path);

} // namespace tve
