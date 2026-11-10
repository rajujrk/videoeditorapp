#include "model/RenderCost.h"

#include "model/TimelineQuery.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <set>
#include <utility>

namespace tve {

namespace {

constexpr int kMaxNesting = 8;

constexpr double k4kPixels = 3840.0 * 2160.0;
constexpr double kLayer4k30 = 0.07; // a 4K30 layer decoded and placed

// The picture a clip decodes, in pixels (a proxy's: at most 720 high), and per second.
std::pair<double, double> pictureOf(const MediaItem& media)
{
    double width = media.width > 0 ? media.width : 1920.0;
    double height = media.height > 0 ? media.height : 1080.0;
    if (!media.proxyPath.empty() && height > 720.0) {
        width *= 720.0 / height;
        height = 720.0;
    }
    const double fps = media.frameRate.num > 0 && media.frameRate.den > 0 ? double(media.frameRate.num) / media.frameRate.den : 30.0;
    return {width * height, fps};
}

// Its effects and analysis, on a picture of `pixels`.
double extraLoad(const Clip& clip, double pixels, const EffectWeight& weight)
{
    const double size = std::max(0.25, pixels / k4kPixels);
    double load = 0.0;
    for (const ClipEffect& effect : clip.effects)
        if (effect.enabled)
            load += weight(effect) * size;
    if (clip.cutout.enabled)
        load += 0.2; // a person matte found per frame
    if (clip.faceMosaic.active())
        load += 0.04;
    if (clip.denoise > 0.0)
        load += 0.08 * size;
    if (clip.stabilization.enabled)
        load += 0.01;
    load += 0.01 * double(clip.masks.size());
    if (clip.chromaKey.enabled)
        load += 0.01 * size;
    if (clip.look)
        load += 0.01 * size;
    return load;
}

double loadAt(const Project& project, const std::vector<Track>& tracks, Ticks time, int depth, const EffectWeight& weight)
{
    const double canvas = double(project.settings.width) * project.settings.height;
    double load = 0.0;
    const auto clipLoad = [&](const Clip& clip) {
        if (clip.text || clip.drawing || clip.adjustment)
            return 0.01 + extraLoad(clip, canvas, weight);
        const MediaItem* media = project.findMedia(clip.mediaId);
        if (!media)
            return 0.0;
        if (media->still)
            return 0.01 + extraLoad(clip, canvas, weight); // drawn once, kept on the GPU
        const auto [pixels, fps] = pictureOf(*media);
        double decode = kLayer4k30 * (pixels / k4kPixels) * (fps / 30.0);
        if (!clip.freeze)
            decode *= std::max(1.0, std::abs(clip.speed)) * (clip.reversed ? 1.5 : 1.0); // reversed: whole groups
        return decode + extraLoad(clip, pixels, weight);
    };
    for (const VisibleClip& layer : visibleVideoLayersAt(project, tracks, time)) {
        const Clip& clip = *layer.clip;
        if (clip.compound()) {
            if (const Sequence* sequence = project.findSequence(clip.sequenceId); sequence && depth < kMaxNesting)
                load += 0.02 + extraLoad(clip, canvas, weight) + loadAt(project, sequence->tracks, layer.sourceTime, depth + 1, weight);
            continue;
        }
        load += clipLoad(clip);
        if (layer.transition)
            load += 0.02 + clipLoad(*layer.transition->to);
    }
    return load;
}

// Where what plays may change: clips' edges, transitions' (as far as they could reach) and,
// in compound clips, their sequence's, on the timeline.
void collectCuts(const Project& project, const std::vector<Track>& tracks, Ticks offset, Ticks from, Ticks to,
                 int depth, std::set<Ticks>& cuts)
{
    const auto add = [&](Ticks t) {
        if (t > from && t < to)
            cuts.insert(t);
    };
    for (const Track& track : tracks) {
        if (track.kind == TrackKind::Audio || track.hidden)
            continue;
        for (const Clip& clip : track.clips) {
            const Ticks start = offset + clip.start;
            add(start);
            add(start + clip.duration);
            if (clip.transitionIn) {
                add(start - clip.transitionIn->duration);
                add(start + clip.transitionIn->duration);
            }
            if (clip.compound() && depth < kMaxNesting)
                if (const Sequence* sequence = project.findSequence(clip.sequenceId))
                    collectCuts(project, sequence->tracks, start - clip.sourceIn, std::max(from, start),
                                std::min(to, start + clip.duration), depth + 1, cuts);
        }
    }
}

} // namespace

double defaultEffectWeight(const ClipEffect& effect)
{
    return effect.effectId.rfind("ofx:", 0) == 0 ? 0.15 : 0.02;
}

std::vector<PlaybackLoad> playbackLoad(const Project& project, const EffectWeight& weight)
{
    const Ticks end = project.duration();
    std::set<Ticks> cuts{0, end};
    collectCuts(project, project.tracks, 0, 0, end, 0, cuts);
    std::vector<PlaybackLoad> stretches;
    for (auto it = cuts.begin(); it != cuts.end() && std::next(it) != cuts.end(); ++it) {
        const Ticks a = *it;
        const Ticks b = *std::next(it);
        if (b <= a)
            continue;
        stretches.push_back({a, b, loadAt(project, project.tracks, a + (b - a) / 2, 0, weight)});
    }
    return stretches;
}

std::vector<PlaybackLoad> heavyStretches(const Project& project, double limit, const EffectWeight& weight)
{
    std::vector<PlaybackLoad> heavy;
    for (const PlaybackLoad& stretch : playbackLoad(project, weight)) {
        if (stretch.load <= limit)
            continue;
        if (!heavy.empty() && heavy.back().to == stretch.from) {
            heavy.back().to = stretch.to;
            heavy.back().load = std::max(heavy.back().load, stretch.load);
        } else {
            heavy.push_back(stretch);
        }
    }
    return heavy;
}

} // namespace tve
