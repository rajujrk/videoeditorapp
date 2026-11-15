#include "analysis/Tracking.h"

#include "decode/VideoDecoder.h"
#include "model/Translate.h"

#import <Foundation/Foundation.h>
#import <CoreImage/CoreImage.h>
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
        fail(error, TVE_TR("core", "Draw a box around what to follow first."));
        return std::nullopt;
    }
    auto decoder = VideoDecoder::open(path, error);
    if (!decoder || !decoder->seek(from)) {
        if (error && error->empty())
            *error = TVE_TR("core", "This file has no pictures to follow.");
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
                        fail(error, TVE_TR("core", "No flat rectangle (a screen, a sign, a page) was found in the box."));
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
        fail(error, TVE_TR("core", "Nothing to follow there."));
        return std::nullopt;
    }
    return samples;
}

std::optional<std::vector<TrackSample>> trackPlane(const std::string& path, Ticks from, Ticks to, const Quad& corners,
                                                   const AnalysisProgress& progress, std::string* error)
{
    if (!squareToQuad(corners)) {
        fail(error, TVE_TR("core", "Place the four corners on the flat surface to follow first."));
        return std::nullopt;
    }
    auto decoder = VideoDecoder::open(path, error);
    if (!decoder || !decoder->seek(from)) {
        if (error && error->empty())
            *error = TVE_TR("core", "This file has no pictures to follow.");
        return std::nullopt;
    }
    // Template tracking: the surface straightened (seen face on) in the first frame is the
    // template. Each frame the surface is straightened again where it was last, registered
    // against the template (a projective map, Vision), and its corners put right, so small
    // errors never add up.
    std::vector<TrackSample> samples;
    Quad quad = corners;
    CIImage* surfaceTemplate = nil; // retained
    // Frames in a row where the surface could not be seen for sure (something passing over it,
    // a blur): the frames either side say where it was; after half a second, it is lost.
    int unsure = 0;
    constexpr int kMostUnsure = 15;
    CGSize templateSize = CGSizeZero;
    // A frame's picture straightened where `q` says the surface is, the template's size, and
    // the map from that straightened picture (pixels, y up) to the frame's (fractions, y down).
    const auto straighten = [&](CIImage* frame, double width, double height, const Quad& q, CGSize size,
                                std::optional<Homography>* toFrame) -> CIImage* {
        const auto ci = [&](std::size_t i) { return CGPointMake(q[i * 2] * width, (1.0 - q[i * 2 + 1]) * height); };
        CIFilter* filter = [CIFilter filterWithName:@"CIPerspectiveCorrection"];
        [filter setValue:frame forKey:kCIInputImageKey];
        [filter setValue:[CIVector vectorWithCGPoint:ci(0)] forKey:@"inputTopLeft"];
        [filter setValue:[CIVector vectorWithCGPoint:ci(1)] forKey:@"inputTopRight"];
        [filter setValue:[CIVector vectorWithCGPoint:ci(2)] forKey:@"inputBottomRight"];
        [filter setValue:[CIVector vectorWithCGPoint:ci(3)] forKey:@"inputBottomLeft"];
        CIImage* out = filter.outputImage;
        if (!out || CGRectIsEmpty(out.extent) || CGRectIsInfinite(out.extent))
            return nil;
        const CGRect e = out.extent;
        if (size.width <= 0)
            size = e.size;
        CGAffineTransform fit = CGAffineTransformMakeTranslation(-e.origin.x, -e.origin.y);
        fit = CGAffineTransformConcat(fit, CGAffineTransformMakeScale(size.width / e.size.width, size.height / e.size.height));
        // Straightened pixels (y up): (0, H) top left … (0, 0) bottom left.
        *toFrame = quadToQuad({0, size.height, size.width, size.height, size.width, 0, 0, 0}, q);
        return [[out imageByApplyingTransform:fit] imageByCroppingToRect:CGRectMake(0, 0, size.width, size.height)];
    };
    while (true) {
        auto frame = decoder->nextFrame();
        if (!frame || frame->pts() > to)
            break;
        bool lost = false;
        bool skipFrame = false;
        @autoreleasepool {
            CVPixelBufferRef buffer = frame->pixelBuffer();
            const double width = double(CVPixelBufferGetWidth(buffer)), height = double(CVPixelBufferGetHeight(buffer));
            CIImage* picture = [CIImage imageWithCVPixelBuffer:buffer];
            if (!surfaceTemplate) {
                std::optional<Homography> unused;
                CIImage* straight = straighten(picture, width, height, quad, CGSizeZero, &unused);
                if (!straight || straight.extent.size.width < 16 || straight.extent.size.height < 16) {
                    lost = true;
                } else {
                    // (Rendered once, so it is not straightened again from the first frame each time.)
                    CIContext* context = [CIContext contextWithOptions:nil];
                    CGImageRef still = [context createCGImage:straight fromRect:straight.extent];
                    surfaceTemplate = [[CIImage imageWithCGImage:still] retain];
                    CGImageRelease(still);
                    templateSize = straight.extent.size;
                }
            } else {
                std::optional<Homography> toFrame;
                CIImage* now = straighten(picture, width, height, quad, templateSize, &toFrame);
                VNHomographicImageRegistrationRequest* registration =
                    now ? [[VNHomographicImageRegistrationRequest alloc] initWithTargetedCIImage:now options:@{}] : nil;
                VNImageRequestHandler* handler = [[VNImageRequestHandler alloc] initWithCIImage:surfaceTemplate options:@{}];
                NSError* failure = nil;
                if (registration)
                    [handler performRequests:@[registration] error:&failure];
                VNImageHomographicAlignmentObservation* aligned = registration.results.firstObject;
                const auto inverse = [&]() -> std::optional<Homography> {
                    if (!aligned)
                        return std::nullopt;
                    const matrix_float3x3 w = aligned.warpTransform; // columns
                    Homography h;
                    h.m = {w.columns[0][0], w.columns[1][0], w.columns[2][0], w.columns[0][1], w.columns[1][1],
                           w.columns[2][1], w.columns[0][2], w.columns[1][2], w.columns[2][2]};
                    return h.inverse(); // the template's pixels → the straightened frame's
                }();
                bool sure = false;
                if (inverse && toFrame) {
                    const double tw = templateSize.width, th = templateSize.height;
                    const Quad next = inverse->then(*toFrame).apply(Quad{0, th, tw, th, tw, 0, 0, 0});
                    // A jump of more than a tenth of the picture a frame (more for each frame not
                    // seen), or a quad turned inside out: not it.
                    double jump = 0.0;
                    for (std::size_t i = 0; i < 8; ++i)
                        jump = std::max(jump, std::abs(next[i] - quad[i]));
                    if (jump <= 0.1 * (1 + unsure) && squareToQuad(next)) {
                        quad = next;
                        sure = true;
                    }
                }
                if (sure)
                    unsure = 0;
                else if (++unsure > kMostUnsure)
                    lost = true;
                skipFrame = !sure;
                [handler release];
                [registration release];
            }
            if (!lost && !skipFrame)
                samples.push_back(sampleOfQuad(frame->pts(), quad));
        }
        if (lost)
            break;
        if (progress && samples.size() % 10 == 0 && to > from
            && !progress(std::clamp(double(samples.back().time - from) / double(to - from), 0.0, 1.0))) {
            [surfaceTemplate release];
            return std::nullopt;
        }
    }
    [surfaceTemplate release];
    if (samples.empty()) {
        fail(error, TVE_TR("core", "Nothing to follow there."));
        return std::nullopt;
    }
    return samples;
}

} // namespace tve
