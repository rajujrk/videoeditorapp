#include "NativeWindow.h"

#include <QWindow>

#import <AppKit/AppKit.h>

std::uint32_t nativeWindowNumber(QWindow* window)
{
    if (!window || !window->handle())
        return 0;
    NSView* view = reinterpret_cast<NSView*>(window->winId());
    return view.window ? std::uint32_t(view.window.windowNumber) : 0;
}

void performAlignmentFeedback()
{
    [[NSHapticFeedbackManager defaultPerformer] performFeedbackPattern:NSHapticFeedbackPatternAlignment
                                                       performanceTime:NSHapticFeedbackPerformanceTimeNow];
}
