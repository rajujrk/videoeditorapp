#include "commands/TemplateCommands.h"

#include <cmath>

namespace tve {

std::optional<std::string> ReplaceClipSourceCommand::redo(Project& project)
{
    auto location = project.locateClip(m_clipId);
    if (!location)
        return "clip not found";
    if (location->track->locked)
        return "track is locked";
    const MediaItem* media = project.findMedia(m_mediaId);
    if (!media)
        return "media not found";
    if (location->clip->generated() || location->clip->compound())
        return "only clips of media can be given other media";
    if (m_speed <= 0 || m_sourceIn < 0)
        return "speed and start are out of range";
    if (m_slot && (!project.templateInfo || *m_slot >= project.templateInfo->mediaSlots.size()
                   || project.templateInfo->mediaSlots[*m_slot].clipId != m_clipId))
        return "not a slot of this template";
    Clip& clip = *location->clip;
    // Enough source for the clip's length at that speed (stills last as long as asked).
    if (!media->still) {
        const Ticks needed = Ticks(std::llround(double(clip.duration) * m_speed));
        if (m_sourceIn + needed > media->duration + kTicksPerSecond / 100)
            return "the media is too short for this clip";
    }
    m_before = clip;
    clip.mediaId = m_mediaId;
    clip.sourceIn = m_sourceIn;
    clip.speed = m_speed;
    clip.speedRamp = {};
    clip.reversed = false;
    clip.freeze = false;
    if (m_crop)
        clip.visual.crop = *m_crop;
    // What was measured or made from the old media no longer applies.
    clip.enhancedAudio.reset();
    clip.processedVideo.reset();
    clip.cutout = {};
    clip.faceMosaic = {};
    clip.stabilization = {};
    clip.deflicker = {};
    clip.motionTracks.clear();
    clip.cursor = {};
    if (m_slot) {
        TemplateSlot& slot = project.templateInfo->mediaSlots[*m_slot];
        m_slotWasFilled = slot.filled;
        slot.filled = true;
    }
    return std::nullopt;
}

void ReplaceClipSourceCommand::undo(Project& project)
{
    if (auto location = project.locateClip(m_clipId); location && m_before)
        *location->clip = *m_before;
    if (m_slot && project.templateInfo && *m_slot < project.templateInfo->mediaSlots.size())
        project.templateInfo->mediaSlots[*m_slot].filled = m_slotWasFilled;
}

} // namespace tve
