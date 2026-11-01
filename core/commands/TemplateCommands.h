#pragma once

#include "commands/Command.h"
#include "model/Project.h"

#include <optional>

namespace tve {

// Gives a clip other media to play, keeping its place and length on the timeline: from
// `sourceIn`, at `speed` (so a short video can still fill it), cropped by `crop` when given.
// What belonged to the old media (its stabilization, cut-outs, cleaned sound, processed
// copy, tracks) is dropped; its look, colour, effects, keyframes and transition stay. With
// `slot`, that template slot is marked filled.
class ReplaceClipSourceCommand final : public Command {
public:
    ReplaceClipSourceCommand(Id clipId, Id mediaId, Ticks sourceIn, double speed, std::optional<ClipCrop> crop,
                             std::optional<std::size_t> slot, std::string label)
        : m_clipId(clipId), m_mediaId(mediaId), m_sourceIn(sourceIn), m_speed(speed), m_crop(crop), m_slot(slot),
          m_label(std::move(label)) {}

    std::string label() const override { return m_label; }
    std::optional<std::string> redo(Project& project) override;
    void undo(Project& project) override;

private:
    Id m_clipId;
    Id m_mediaId;
    Ticks m_sourceIn;
    double m_speed;
    std::optional<ClipCrop> m_crop;
    std::optional<std::size_t> m_slot;
    std::string m_label;
    std::optional<Clip> m_before;
    bool m_slotWasFilled = false;
};

} // namespace tve
