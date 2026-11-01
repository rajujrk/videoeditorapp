#pragma once

#include "model/Project.h"

#include <QImage>
#include <QSize>

// Draws a drawing as it looks on a canvas of `canvas` pixels: each path stroked with round
// ends (rectangles and ellipses filled too, arrows with a head), as much of each as is drawn
// `seconds` into a clip of `duration` seconds (with `seconds` < 0, all of it). The image is
// canvas-sized with straight (not premultiplied) alpha, transparent around the paths.
QImage renderDrawing(const tve::ClipDrawing& drawing, QSize canvas, double seconds = -1.0, double duration = 0.0);
