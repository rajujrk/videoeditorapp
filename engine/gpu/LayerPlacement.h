#pragma once

#include "model/Project.h"

#include <array>
#include <cmath>
#include <numbers>

namespace tve {

// Maps canvas coordinates to a layer's texture coordinates for a clip's placement. Both
// are normalized (0..1, origin top-left). The compositor evaluates it per canvas pixel and
// draws the layer where the result falls inside `cropRect`. A projective map (a corner pin
// stretches the picture in perspective); without one its last row is 0, 0, 1.
struct LayerPlacement {
    // (u, v, w) = canvasToLayer · (canvas u, canvas v, 1); layer = (u / w, v / w)
    std::array<double, 9> canvasToLayer{1, 0, 0, 0, 1, 0, 0, 0, 1};
    // Its inverse: layer coordinates to canvas ones.
    std::array<double, 9> layerToCanvas{1, 0, 0, 0, 1, 0, 0, 0, 1};
    // Visible part of the layer texture: left, top, right, bottom.
    std::array<double, 4> cropRect{0, 0, 1, 1};

    static std::array<double, 2> map(const std::array<double, 9>& m, double u, double v)
    {
        const double w = m[6] * u + m[7] * v + m[8];
        const double safe = std::abs(w) < 1e-12 ? 1e-12 : w;
        return {(m[0] * u + m[1] * v + m[2]) / safe, (m[3] * u + m[4] * v + m[5]) / safe};
    }
    // Canvas point → layer point (nothing shows there when the picture is behind: w <= 0).
    std::array<double, 2> apply(double u, double v) const { return map(canvasToLayer, u, v); }
    bool inFront(double u, double v) const
    {
        const auto& m = canvasToLayer;
        return m[6] * u + m[7] * v + m[8] > 0.0;
    }
    // Layer point → canvas point.
    std::array<double, 2> toCanvas(double u, double v) const { return map(layerToCanvas, u, v); }
};

namespace placement_detail {

using Matrix = std::array<double, 9>;

inline Matrix multiply(const Matrix& a, const Matrix& b)
{
    Matrix r{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            r[std::size_t(i * 3 + j)] = a[std::size_t(i * 3)] * b[std::size_t(j)] + a[std::size_t(i * 3 + 1)] * b[std::size_t(3 + j)]
                                      + a[std::size_t(i * 3 + 2)] * b[std::size_t(6 + j)];
    return r;
}

inline Matrix inverse(const Matrix& m)
{
    const double a = m[0], b = m[1], c = m[2], d = m[3], e = m[4], f = m[5], g = m[6], h = m[7], i = m[8];
    const double A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g;
    double det = a * A + b * B + c * C;
    if (std::abs(det) < 1e-18)
        det = det < 0 ? -1e-18 : 1e-18;
    Matrix r{A, -(b * i - c * h), b * f - c * e, B, a * i - c * g, -(a * f - c * d), C, -(a * h - b * g), a * e - b * d};
    for (double& x : r)
        x /= det;
    return r;
}

// The unit square's corners (0,0), (1,0), (1,1), (0,1) onto p0..p3 (Heckbert's projective map).
inline Matrix squareToQuad(const std::array<std::array<double, 2>, 4>& p)
{
    const double x0 = p[0][0], y0 = p[0][1], x1 = p[1][0], y1 = p[1][1], x2 = p[2][0], y2 = p[2][1], x3 = p[3][0],
                 y3 = p[3][1];
    const double sx = x0 - x1 + x2 - x3, sy = y0 - y1 + y2 - y3;
    if (std::abs(sx) < 1e-12 && std::abs(sy) < 1e-12)
        return {x1 - x0, x3 - x0, x0, y1 - y0, y3 - y0, y0, 0, 0, 1};
    const double dx1 = x1 - x2, dx2 = x3 - x2, dy1 = y1 - y2, dy2 = y3 - y2;
    double det = dx1 * dy2 - dx2 * dy1;
    if (std::abs(det) < 1e-12)
        det = 1e-12;
    const double g = (sx * dy2 - dx2 * sy) / det;
    const double h = (dx1 * sy - sx * dy1) / det;
    return {x1 - x0 + g * x1, x3 - x0 + h * x3, x0, y1 - y0 + g * y1, y3 - y0 + h * y3, y0, g, h, 1};
}

} // namespace placement_detail

// The cropped picture is fitted inside the canvas keeping its aspect ratio, scaled about its
// anchor point (by default its centre), rotated clockwise about it, and moved by (x, y)
// canvas sizes; then, with a corner pin, each corner is moved and the picture stretched in
// perspective between them. Sizes are in pixels.
inline LayerPlacement placeLayer(const ClipVisual& visual, double canvasWidth, double canvasHeight,
                                 double layerWidth, double layerHeight)
{
    using namespace placement_detail;
    const ClipCrop& crop = visual.crop;
    const ClipTransform& t = visual.transform;
    const double cropW = 1.0 - crop.left - crop.right;
    const double cropH = 1.0 - crop.top - crop.bottom;
    const double visibleW = layerWidth * cropW;
    const double visibleH = layerHeight * cropH;
    const double fit = std::min(canvasWidth / visibleW, canvasHeight / visibleH);
    const double fitW = visibleW * fit, fitH = visibleH * fit;
    const double centerX = canvasWidth * (0.5 + t.x);
    const double centerY = canvasHeight * (0.5 + t.y);
    const double angle = t.rotation * std::numbers::pi / 180.0;
    const double c = std::cos(angle), s = std::sin(angle);
    const double ax = t.anchorX * fitW, ay = t.anchorY * fitH;

    // Layer (u, v) → canvas pixels: q = ((u - left) / cropW - 0.5)·fitW, likewise y;
    // canvas = centre + a + scale·R·(q - a).
    // Flipped, q is mirrored about the picture's centre (inside its frame, which stays put).
    const double k = t.scale;
    const double fx = t.flipH ? -1.0 : 1.0, fy = t.flipV ? -1.0 : 1.0;
    const double qx0 = fx * (-crop.left / cropW - 0.5) * fitW, qy0 = fy * (-crop.top / cropH - 0.5) * fitH; // q at u = v = 0
    const double qxu = fx * fitW / cropW, qyv = fy * fitH / cropH;                                         // dq/du, dq/dv
    const double ox = centerX + ax + k * (c * (qx0 - ax) - s * (qy0 - ay));
    const double oy = centerY + ay + k * (s * (qx0 - ax) + c * (qy0 - ay));
    Matrix forward{k * c * qxu, -k * s * qyv, ox, k * s * qxu, k * c * qyv, oy, 0, 0, 1};

    if (visual.pin.active()) {
        // The visible picture's corners where the transform puts them, each moved by the pin of
        // the frame corner it lands on (a flipped picture's left edge is on the frame's right).
        const ClipCornerPin& pin = visual.pin;
        const double l = crop.left, r = 1.0 - crop.right, top = crop.top, b = 1.0 - crop.bottom;
        const auto at = [&](double u, double v, bool left, bool upper) {
            const bool frameLeft = left != t.flipH, frameTop = upper != t.flipV;
            const double dx = frameTop ? (frameLeft ? pin.topLeftX : pin.topRightX) : (frameLeft ? pin.bottomLeftX : pin.bottomRightX);
            const double dy = frameTop ? (frameLeft ? pin.topLeftY : pin.topRightY) : (frameLeft ? pin.bottomLeftY : pin.bottomRightY);
            const auto p = LayerPlacement::map(forward, u, v);
            return std::array<double, 2>{p[0] + dx * canvasWidth, p[1] + dy * canvasHeight};
        };
        const Matrix quad = squareToQuad({at(l, top, true, true), at(r, top, false, true), at(r, b, false, false),
                                          at(l, b, true, false)});
        // Layer → unit square over the visible part, then onto the quad.
        const Matrix toSquare{1.0 / (r - l), 0, -l / (r - l), 0, 1.0 / (b - top), -top / (b - top), 0, 0, 1};
        forward = multiply(quad, toSquare);
    }
    // In canvas fractions.
    const Matrix toFraction{1.0 / canvasWidth, 0, 0, 0, 1.0 / canvasHeight, 0, 0, 0, 1};
    LayerPlacement p;
    p.layerToCanvas = multiply(toFraction, forward);
    p.canvasToLayer = inverse(p.layerToCanvas);
    // Keep w positive in front of the picture (the inverse's sign is arbitrary).
    const auto& m = p.canvasToLayer;
    const auto centre = LayerPlacement::map(p.layerToCanvas, 0.5, 0.5);
    if (m[6] * centre[0] + m[7] * centre[1] + m[8] < 0)
        for (double& x : p.canvasToLayer)
            x = -x;
    p.cropRect = {crop.left, crop.top, 1.0 - crop.right, 1.0 - crop.bottom};
    return p;
}

} // namespace tve
