#pragma once

#include "model/Project.h"

#include <optional>
#include <string>
#include <vector>

namespace tve {

// The shapes a template or montage can be made in, and its canvas size for each.
struct TemplateShape {
    std::string id; // "16:9", "9:16", "1:1", "4:3", "3:4", "21:9"
    std::int32_t width = 0;
    std::int32_t height = 0;
};
const std::vector<TemplateShape>& templateShapes();
std::optional<TemplateShape> templateShape(const std::string& id);
// The shape a canvas is nearest to.
const TemplateShape& nearestShape(std::int32_t width, std::int32_t height);

// The crop that makes a picture of mediaWidth × mediaHeight fill a canvas (cut evenly from
// the edges that stick out), rather than fit inside it.
ClipCrop fillCrop(std::int32_t mediaWidth, std::int32_t mediaHeight, std::int32_t canvasWidth, std::int32_t canvasHeight);

// The speed that makes `available` source fill a slot `length` long: 1 when there is enough,
// slower (never below 0.25) when there is not.
double fillSpeed(Ticks available, Ticks length);

} // namespace tve
