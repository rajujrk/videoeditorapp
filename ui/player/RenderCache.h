#pragma once

#include "model/Project.h"
#include "playback/TimelinePlayback.h"

#include <QString>

#include <functional>
#include <memory>
#include <vector>

// Render files: stretches of the timeline rendered once (by Exporter, at the canvas's size)
// and played from there, so heavy stretches play smoothly. Each file is named by a key made
// from all its pictures depend on (renderedContent): an edit that touches the stretch gives
// it another key, so its file no longer applies (undo brings it back); nothing else needs
// tracking. Kept in TVE_RENDER_CACHE_DIR (else app data), the least recently used removed
// past kMaxBytes.
class RenderCache {
public:
    static constexpr qint64 kMaxBytes = 20LL * 1024 * 1024 * 1024;
    // Stretches are rendered in pieces of at most this, so an edit costs only its piece.
    static constexpr tve::Ticks kMaxPiece = 10 * tve::kTicksPerSecond;

    static QString directory();
    static QString keyOf(const tve::Project& project, tve::Ticks from, tve::Ticks to);

    // The rendered stretches that apply to `project` as it is (its files there).
    std::vector<tve::RenderedStretch> validFor(const tve::Project& project) const;

    // Renders [from, to) of `project` (no proxies: the media themselves) to its file.
    // `progress` gets 0–1; returning false cancels. False with `error` on failure (empty
    // when cancelled).
    static bool render(std::shared_ptr<const tve::Project> project, tve::Ticks from, tve::Ticks to,
                       const std::function<bool(double)>& progress, QString* error);

    // Removes every render file.
    static void clear();
    // The size of the render files.
    static qint64 size();
};
