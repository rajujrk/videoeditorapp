#include "analysis/Tracking.h"

#include "decode/VideoDecoder.h"

#import <Foundation/Foundation.h>
#import <Vision/Vision.h>

#include <algorithm>
#include <cmath>

namespace tve {

namespace {

// Vision's normalized rectangles have their origin at the bottom left.
CGRect visionRect(const TrackSample& s)
{
    return CGRectMake(s.x - s.width / 2, 1.0 - (s.y + s.height / 2), s.width, s.height);
}

TrackSample sampleOf(Ticks time, CGRect r)
{
    return {time, r.origin.x + r.size.width / 2, 1.0 - (r.origin.y + r.size.height / 2), r.size.width, r.size.height, 0.0};
}

// A rectangle from its corners (Vision's, y up), measured in pixels so turns keep their shape.
TrackSample sampleOf(Ticks time, VNRectangleObservation* r, double width, double height)
{
    const auto px = [&](CGPoint p) { return std::array<double, 2>{p.x * width, (1.0 - p.y) * height}; };
    const auto tl = px(r.topLeft), tr = px(r.topRight), bl = px(r.bottomLeft), br = px(r.bottomRight);
    const double cx = (tl[0] + tr[0] + bl[0] + br[0]) / 4, cy = (tl[1] + tr[1] + bl[1] + br[1]) / 4;
    const double top = std::hypot(tr[0] - tl[0], tr[1] - tl[1]), bottom = std::hypot(br[0] - bl[0], br[1] - bl[1]);
    const double left = std::hypot(bl[0] - tl[0], bl[1] - tl[1]), right = std::hypot(br[0] - tr[0], br[1] - tr[1]);
    const double angle = std::atan2((tr[1] - tl[1]) + (br[1] - bl[1]), (tr[0] - tl[0]) + (br[0] - bl[0]));
    return {time, cx / width, cy / height, (top + bottom) / 2 / width, (left + right) / 2 / height, angle};
}

// The rectangle (a flat surface) inside `around`, grown by `grow` (Vision's: retained, or
// nil), the one whose centre is nearest `around`'s.
VNRectangleObservation* findRectangle(CVPixelBufferRef buffer, const TrackSample& around, double grow)
{
    TrackSample area = around;
    area.width *= grow;
    area.height *= grow;
    VNDetectRectanglesRequest* find = [[VNDetectRectanglesRequest alloc] init];
    find.regionOfInterest = CGRectIntersection(visionRect(area), CGRectMake(0, 0, 1, 1));
    find.maximumObservations = 4;
    find.minimumSize = float(0.5 / grow); // about the size it was
    find.minimumConfidence = 0.3f;
    find.quadratureTolerance = 30.0f;
    find.minimumAspectRatio = 0.1f;
    VNImageRequestHandler* image = [[VNImageRequestHandler alloc] initWithCVPixelBuffer:buffer options:@{}];
    NSError* failure = nil;
    [image performRequests:@[find] error:&failure];
    VNRectangleObservation* best = nil;
    double nearest = 0.0;
    for (VNRectangleObservation* r in find.results) {
        const CGRect b = r.boundingBox;
        const double d = std::hypot(b.origin.x + b.size.width / 2 - around.x, 1.0 - (b.origin.y + b.size.height / 2) - around.y);
        if (!best || d < nearest) {
            best = r;
            nearest = d;
        }
    }
    [best retain];
    [image release];
    [find release];
    return best;
}

void fail(std::string* error, std::string message)
{
    if (error)
        *error = std::move(message);
}

} // namespace

std::optional<std::vector<TrackSample>> trackRegion(const std::string& path, Ticks from, Ticks to, const TrackSample& box,
                                                    bool surface, const AnalysisProgress& progress, std::string* error)
{
    if (box.width <= 0.0 || box.height <= 0.0) {
        fail(error, "Draw a box around what to follow first.");
        return std::nullopt;
    }
    auto decoder = VideoDecoder::open(path, error);
    if (!decoder || !decoder->seek(from)) {
        if (error && error->empty())
            *error = "This file has no pictures to follow.";
        return std::nullopt;
    }
    std::vector<TrackSample> samples;
    VNSequenceRequestHandler* handler = [[VNSequenceRequestHandler alloc] init];
    VNDetectedObjectObservation* last = nil; // retained
    bool lost = false;
    int skipped = 0; // frames in a row where a surface could not be seen for sure
    while (!lost) {
        auto frame = decoder->nextFrame();
        if (!frame || frame->pts() > to)
            break;
        @autoreleasepool {
            CVPixelBufferRef buffer = frame->pixelBuffer();
            const double width = double(CVPixelBufferGetWidth(buffer)), height = double(CVPixelBufferGetHeight(buffer));
            NSError* failure = nil;
            if (!last) {
                // The first frame: the box as drawn (an object), or the rectangle inside it.
                if (!surface) {
                    last = [[VNDetectedObjectObservation observationWithBoundingBox:visionRect(box)] retain];
                    samples.push_back({frame->pts(), box.x, box.y, box.width, box.height, 0.0});
                } else {
                    // Found again near where it was drawn: grown a little, so the whole of it is in.
                    VNRectangleObservation* found = findRectangle(buffer, box, 1.2);
                    if (found) {
                        last = found;
                        samples.push_back(sampleOf(frame->pts(), found, width, height));
                    }
                    if (!last) {
                        [handler release];
                        fail(error, "No flat rectangle (a screen, a sign, a page) was found in the box.");
                        return std::nullopt;
                    }
                }
            } else {
                VNTrackingRequest* follow =
                    surface ? (VNTrackingRequest*)[[VNTrackRectangleRequest alloc] initWithRectangleObservation:(VNRectangleObservation*)last]
                            : (VNTrackingRequest*)[[VNTrackObjectRequest alloc] initWithDetectedObjectObservation:last];
                follow.trackingLevel = VNRequestTrackingLevelAccurate;
                [handler performRequests:@[follow] onCVPixelBuffer:buffer error:&failure];
                VNDetectedObjectObservation* seen = follow.results.firstObject;
                // A surface keeps about the size it started at, and turns smoothly: anything else
                // is another rectangle taken for it (creeping, or all at once).
                const auto plausible = [&](VNDetectedObjectObservation* o) {
                    if (!o || o.confidence < 0.3f)
                        return false;
                    if (!surface)
                        return true;
                    const TrackSample now = sampleOf(frame->pts(), (VNRectangleObservation*)o, width, height);
                    const TrackSample& start = samples.front();
                    // (The turn allowed grows with each frame it was not seen.)
                    return std::abs(now.width / start.width - 1.0) < 0.08 && std::abs(now.height / start.height - 1.0) < 0.08
                        && std::abs(now.angle - samples.back().angle) < 0.06 * (1 + skipped);
                };
                if (surface && !plausible(seen)) {
                    // Vision's corner trackers give up easily (a turn, a busy background): the
                    // rectangle found afresh near where it was carries on.
                    seen = [findRectangle(buffer, samples.back(), 1.6) autorelease];
                }
                if (plausible(seen)) {
                    [last release];
                    last = [seen retain];
                    samples.push_back(surface ? sampleOf(frame->pts(), (VNRectangleObservation*)seen, width, height)
                                              : sampleOf(frame->pts(), seen.boundingBox));
                    skipped = 0;
                } else if (surface && ++skipped <= 10) {
                    // Not seen for sure in this frame: the frames either side will say where it was.
                } else {
                    lost = true;
                }
                [follow release];
            }
        }
        if (progress && samples.size() % 10 == 0 && to > from
            && !progress(std::clamp(double(samples.back().time - from) / double(to - from), 0.0, 1.0))) {
            [last release];
            [handler release];
            return std::nullopt;
        }
    }
    [last release];
    [handler release];
    if (samples.empty()) {
        fail(error, "Nothing to follow there.");
        return std::nullopt;
    }
    return samples;
}

} // namespace tve
