#pragma once

#include "model/Project.h"

#include <array>
#include <cmath>
#include <numbers>

namespace tve {

// Maps canvas coordinates to a layer's texture coordinates for a clip's placement. Both
// are normalized (0..1, origin top-left). The compositor evaluates it per canvas pixel and
// draws the layer where the result falls inside `cropRect`.
struct LayerPlacement {
    // layer.u = m[0]*u + m[1]*v + m[2];  layer.v = m[3]*u + m[4]*v + m[5]
    std::array<double, 6> canvasToLayer{};
    // Visible part of the layer texture: left, top, right, bottom.
    std::array<double, 4> cropRect{0, 0, 1, 1};

    std::array<double, 2> apply(double u, double v) const
    {
        const auto& m = canvasToLayer;
        return {m[0] * u + m[1] * v + m[2], m[3] * u + m[4] * v + m[5]};
    }
};

// The cropped picture is fitted inside the canvas keeping its aspect ratio, scaled about its
// center, rotated clockwise, and moved by (x, y) canvas sizes. Sizes are in pixels.
inline LayerPlacement placeLayer(const ClipVisual& visual, double canvasWidth, double canvasHeight,
                                 double layerWidth, double layerHeight)
{
    const ClipCrop& crop = visual.crop;
    const double cropW = 1.0 - crop.left - crop.right;
    const double cropH = 1.0 - crop.top - crop.bottom;
    const double visibleW = layerWidth * cropW;
    const double visibleH = layerHeight * cropH;
    const double fit = std::min(canvasWidth / visibleW, canvasHeight / visibleH);
    const double shownW = visibleW * fit * visual.transform.scale;
    const double shownH = visibleH * fit * visual.transform.scale;
    const double centerX = canvasWidth * (0.5 + visual.transform.x);
    const double centerY = canvasHeight * (0.5 + visual.transform.y);
    const double angle = visual.transform.rotation * std::numbers::pi / 180.0;
    const double c = std::cos(angle);
    const double s = std::sin(angle);

    // Inverse of: canvas = center + R(angle) · ((u' - 0.5)·shownW, (v' - 0.5)·shownH),
    // then u = crop.left + u'·cropW (likewise v).
    LayerPlacement p;
    p.canvasToLayer = {
        cropW * c * canvasWidth / shownW,
        cropW * s * canvasHeight / shownW,
        crop.left + cropW * (0.5 - (c * centerX + s * centerY) / shownW),
        -cropH * s * canvasWidth / shownH,
        cropH * c * canvasHeight / shownH,
        crop.top + cropH * (0.5 - (-s * centerX + c * centerY) / shownH),
    };
    p.cropRect = {crop.left, crop.top, 1.0 - crop.right, 1.0 - crop.bottom};
    return p;
}

} // namespace tve
