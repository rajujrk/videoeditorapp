#pragma once

#include <cstdint>

class QWindow;

// The macOS window number of a Qt window (what ScreenCaptureKit calls its window id), or 0
// when it has none yet (not shown).
std::uint32_t nativeWindowNumber(QWindow* window);
