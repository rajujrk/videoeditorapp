#pragma once

#include <cstdint>

class QWindow;

// The macOS window number of a Qt window (what ScreenCaptureKit calls its window id), or 0
// when it has none yet (not shown).
std::uint32_t nativeWindowNumber(QWindow* window);

// A light tap on a Force Touch trackpad (the system's alignment feedback), felt as a picture
// meets an edge while it is dragged; nothing on a mouse.
void performAlignmentFeedback();
