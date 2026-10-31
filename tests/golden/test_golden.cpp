// Golden-frame tests: fixed projects, built here from generated pictures, rendered at fixed
// times by the real renderer (StillRenderer: playback layers + FrameCompositor on Metal) and
// compared with reference PNGs in tests/golden/metal/ within a perceptual tolerance.
//
//   TVE_GOLDEN_UPDATE=1 tve_golden_tests   writes the references (after a deliberate change)
//
// A failing case leaves its picture and a difference image in <temp>/tve_golden_failures/.
#include "LookLibrary.h"
#include "StillRenderer.h"
#include "Stickers.h"

#include "gpu/EffectLibrary.h"
#include "model/TextAnimation.h"

#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <functional>
#include <map>
#include <set>

using namespace tve;

namespace {

constexpr Ticks kSec = kTicksPerSecond;
constexpr int kCanvasW = 256;
constexpr int kCanvasH = 144;
const QSize kReference(128, 72); // stored and compared at half size, which evens out edge noise

// --- Source pictures, drawn once per run ---

struct Pictures {
    QTemporaryDir dir;
    std::map<std::string, std::string> paths;

    Pictures()
    {
        // A hue sweep across, light to dark down: every colour and level.
        QImage gradient(384, 216, QImage::Format_RGB32);
        for (int y = 0; y < gradient.height(); ++y)
            for (int x = 0; x < gradient.width(); ++x)
                gradient.setPixelColor(x, y, QColor::fromHsvF(float(x) / gradient.width(), 0.85f,
                                                              1.0f - 0.8f * float(y) / gradient.height()));
        save("gradient", gradient);

        // Edges and detail: a checkerboard with discs and bars.
        QImage pattern(384, 216, QImage::Format_RGB32);
        pattern.fill(QColor(30, 30, 40));
        {
            QPainter p(&pattern);
            p.setRenderHint(QPainter::Antialiasing);
            for (int y = 0; y < 216; y += 24)
                for (int x = (y / 24) % 2 * 24; x < 384; x += 48)
                    p.fillRect(x, y, 24, 24, QColor(200, 200, 210));
            p.setBrush(QColor(230, 60, 50));
            p.setPen(Qt::NoPen);
            p.drawEllipse(QPointF(110, 108), 60, 60);
            p.setBrush(QColor(40, 170, 90));
            p.drawEllipse(QPointF(270, 90), 45, 45);
            p.fillRect(40, 180, 300, 14, QColor(250, 210, 40));
        }
        save("pattern", pattern);

        // Upright (letterboxed on a wide canvas).
        QImage portrait(144, 256, QImage::Format_RGB32);
        for (int y = 0; y < 256; ++y)
            for (int x = 0; x < 144; ++x)
                portrait.setPixelColor(x, y, QColor(40 + y / 2, 90, 200 - y / 2));
        save("portrait", portrait);

        // A green screen with a red square.
        QImage keyed(320, 180, QImage::Format_RGB32);
        keyed.fill(QColor(0, 176, 64));
        QPainter(&keyed).fillRect(110, 55, 100, 70, QColor(220, 30, 30));
        save("keyed", keyed);

        // Transparency: a yellow disc on nothing.
        QImage disc(200, 200, QImage::Format_ARGB32);
        disc.fill(Qt::transparent);
        {
            QPainter p(&disc);
            p.setRenderHint(QPainter::Antialiasing);
            p.setBrush(QColor(250, 200, 40));
            p.setPen(Qt::NoPen);
            p.drawEllipse(QPointF(100, 100), 70, 70);
        }
        save("disc", disc);

        // A soft white-on-black spot, for luma mattes.
        QImage spot(256, 144, QImage::Format_RGB32);
        for (int y = 0; y < 144; ++y)
            for (int x = 0; x < 256; ++x) {
                const double d = std::hypot((x - 128) / 90.0, (y - 72) / 60.0);
                const int v = int(std::clamp(1.0 - d, 0.0, 1.0) * 255);
                spot.setPixel(x, y, qRgb(v, v, v));
            }
        save("spot", spot);
    }

    void save(const char* name, const QImage& image)
    {
        const QString path = dir.filePath(QString::fromLatin1(name) + QStringLiteral(".png"));
        REQUIRE(image.save(path));
        paths[name] = path.toStdString();
    }
};

Pictures& pictures()
{
    static Pictures instance;
    return instance;
}

// --- Building projects ---

// A 256 × 144, 30 fps project with three video tracks (V1 at the bottom) and a title track.
struct Scene {
    std::shared_ptr<Project> project = std::make_shared<Project>();
    std::map<std::string, Id> media;

    Scene()
    {
        project->settings.width = kCanvasW;
        project->settings.height = kCanvasH;
        for (const char* name : {"V1", "V2", "V3"})
            project->tracks.push_back(Track{.id = project->allocateId(), .kind = TrackKind::Video, .name = name});
        project->tracks.push_back(Track{.id = project->allocateId(), .kind = TrackKind::Title, .name = "T1"});
    }

    Id mediaFor(const std::string& picture)
    {
        if (auto found = media.find(picture); found != media.end())
            return found->second;
        MediaItem item{.id = project->allocateId(), .path = pictures().paths.at(picture), .duration = kStillDuration,
                       .hasVideo = true, .hasAudio = false, .still = true, .alpha = picture == "disc"};
        project->media.push_back(item);
        media[picture] = item.id;
        return item.id;
    }

    // A clip of a picture on track `track` (0: V1) from `start` for `duration` seconds.
    Clip& still(const std::string& picture, std::size_t track = 0, double start = 0, double duration = 3)
    {
        Clip clip;
        clip.id = project->allocateId();
        clip.mediaId = mediaFor(picture);
        clip.start = Ticks(start * kSec);
        clip.duration = Ticks(duration * kSec);
        return insert(track, clip);
    }

    Clip& title(ClipText text, double start = 0, double duration = 3)
    {
        Clip clip;
        clip.id = project->allocateId();
        clip.start = Ticks(start * kSec);
        clip.duration = Ticks(duration * kSec);
        clip.text = std::move(text);
        return insert(3, clip);
    }

    Clip& sticker(const QString& id, double start = 0, double duration = 3)
    {
        const std::string path = stickers::pathOf(id).toStdString();
        auto found = std::ranges::find(project->media, path, &MediaItem::path);
        Id mediaId = found == project->media.end() ? kNoId : found->id;
        if (mediaId == kNoId) {
            mediaId = project->allocateId();
            project->media.push_back(MediaItem{.id = mediaId, .path = path, .duration = kStillDuration, .hasVideo = true,
                                               .hasAudio = false, .width = 512, .height = 512, .still = true,
                                               .alpha = true, .lottie = true});
        }
        Clip clip;
        clip.id = project->allocateId();
        clip.mediaId = mediaId;
        clip.start = Ticks(start * kSec);
        clip.duration = Ticks(duration * kSec);
        clip.visual.transform.scale = 0.6;
        return insert(1, clip);
    }

    Clip& insert(std::size_t track, const Clip& clip)
    {
        Track& t = project->tracks[track];
        t.insertClip(clip);
        return *std::ranges::find(t.clips, clip.id, &Clip::id);
    }
};

struct GoldenCase {
    std::string name; // "<group>/<case>", the reference's path under tests/golden/metal
    std::function<Scene()> build;
    double seconds = 1.0;
};

std::string slug(std::string text)
{
    for (char& c : text)
        if (!std::isalnum(static_cast<unsigned char>(c)))
            c = '_';
    return text;
}

// The value a parameter takes in the "changed" variant of an effect: well away from its default.
std::vector<float> changedValue(const EffectParamSpec& param)
{
    switch (param.type) {
    case ParamType::Float:
    case ParamType::Int: {
        const double value = param.min + 0.8 * (param.max - param.min);
        return {float(param.type == ParamType::Int ? std::round(value) : value)};
    }
    case ParamType::Bool:
        return {param.defaultValue.empty() || param.defaultValue[0] == 0.0f ? 1.0f : 0.0f};
    case ParamType::Enum:
        return {float(std::max<std::size_t>(param.enumValues.size(), 1) - 1)};
    case ParamType::Color:
        return {0.9f, 0.3f, 0.1f, 1.0f};
    }
    return param.defaultValue;
}

std::vector<GoldenCase> allCases()
{
    std::vector<GoldenCase> cases;
    const auto add = [&](std::string name, std::function<Scene()> build, double seconds = 1.0) {
        cases.push_back({std::move(name), std::move(build), seconds});
    };

    // Effects: each at its defaults, and with every parameter moved well away from them.
    // Canvas effects get a smaller picture so what they draw around it shows.
    for (const LoadedEffect& effect : EffectLibrary::builtIn().effects()) {
        const EffectManifest manifest = effect.manifest;
        for (const bool changed : {false, true}) {
            add("effects/" + slug(manifest.id) + (changed ? "_changed" : "_default"), [manifest, changed] {
                Scene s;
                Clip& clip = s.still("pattern");
                ClipEffect instance{.effectId = manifest.id};
                if (changed)
                    for (const EffectParamSpec& param : manifest.params)
                        instance.values[param.id] = changedValue(param);
                clip.effects = {instance};
                if (manifest.space == EffectSpace::Canvas)
                    clip.visual.transform.scale = 0.7;
                return s;
            });
        }
    }

    // Transitions: four moments of each, gradient into pattern around a cut at 2 s.
    for (const LoadedEffect& transition : EffectLibrary::builtIn().transitions()) {
        const std::string id = transition.manifest.id;
        for (const double progress : {0.2, 0.4, 0.6, 0.8}) {
            add("transitions/" + slug(id) + "_" + std::to_string(int(progress * 100)), [id] {
                Scene s;
                s.still("gradient", 0, 0, 2);
                Clip& incoming = s.still("pattern", 0, 2, 2);
                incoming.transitionIn = ClipTransition{.transitionId = id, .duration = kSec};
                return s;
            }, 1.5 + progress);
        }
    }

    // Blend modes, opaque and half transparent, of the pattern over the gradient.
    const std::pair<const char*, BlendMode> blends[] = {
        {"normal", BlendMode::Normal}, {"multiply", BlendMode::Multiply}, {"screen", BlendMode::Screen},
        {"overlay", BlendMode::Overlay}, {"add", BlendMode::Add}, {"darken", BlendMode::Darken},
        {"lighten", BlendMode::Lighten}};
    for (const auto& [name, mode] : blends) {
        for (const double opacity : {1.0, 0.5}) {
            add(std::string("blend/") + name + (opacity < 1 ? "_half" : ""), [mode, opacity] {
                Scene s;
                s.still("gradient");
                Clip& top = s.still("pattern", 1);
                top.visual.blend = mode;
                top.visual.opacity = opacity;
                return s;
            });
        }
    }

    // Placement: scale, turn, move, crop, opacity; a picture of another shape.
    const auto placed = [&](std::string name, std::function<void(ClipVisual&)> change, std::string picture = "pattern") {
        add("transform/" + name, [change, picture] {
            Scene s;
            s.still("gradient");
            Clip& clip = s.still(picture, 1);
            change(clip.visual);
            return s;
        });
    };
    placed("scale_half", [](ClipVisual& v) { v.transform.scale = 0.5; });
    placed("scale_up", [](ClipVisual& v) { v.transform.scale = 1.6; });
    placed("rotate_30", [](ClipVisual& v) { v.transform.rotation = 30; v.transform.scale = 0.6; });
    placed("rotate_minus_45", [](ClipVisual& v) { v.transform.rotation = -45; v.transform.scale = 0.6; });
    placed("rotate_90", [](ClipVisual& v) { v.transform.rotation = 90; v.transform.scale = 0.5; });
    placed("move_left_up", [](ClipVisual& v) { v.transform = {-0.25, -0.2, 0.5, 0}; });
    placed("move_right_down", [](ClipVisual& v) { v.transform = {0.3, 0.25, 0.4, 10}; });
    placed("off_canvas", [](ClipVisual& v) { v.transform = {0.6, 0, 0.8, 0}; });
    placed("crop_left", [](ClipVisual& v) { v.crop.left = 0.3; });
    placed("crop_all", [](ClipVisual& v) { v.crop = {0.1, 0.2, 0.15, 0.05}; });
    placed("crop_scaled", [](ClipVisual& v) { v.crop = {0.25, 0.25, 0.25, 0.25}; v.transform.scale = 0.8; });
    placed("opacity_30", [](ClipVisual& v) { v.opacity = 0.3; });
    placed("portrait_fit", [](ClipVisual&) {}, "portrait");
    placed("portrait_scaled", [](ClipVisual& v) { v.transform.scale = 1.8; }, "portrait");
    placed("alpha_over", [](ClipVisual&) {}, "disc");
    placed("alpha_over_half", [](ClipVisual& v) { v.opacity = 0.5; v.transform.x = 0.2; }, "disc");

    // Keyframes: a picture moving, growing, turning and fading, seen at three moments.
    const std::pair<const char*, Easing> easings[] = {
        {"linear", Easing::Linear}, {"ease_in", Easing::EaseIn}, {"ease_out", Easing::EaseOut}, {"hold", Easing::Hold}};
    for (const auto& [name, easing] : easings) {
        for (const double at : {0.5, 1.0, 1.5}) {
            add(std::string("keyframes/") + name + "_" + std::to_string(int(at * 10)), [easing] {
                Scene s;
                s.still("gradient");
                Clip& clip = s.still("pattern", 1);
                const auto keys = [&](double from, double to) {
                    return Keyframes{{0, from, easing}, {2 * kSec, to, Easing::Linear}};
                };
                clip.keyframes[std::string(property::kX)] = keys(-0.3, 0.3);
                clip.keyframes[std::string(property::kScale)] = keys(0.3, 0.8);
                clip.keyframes[std::string(property::kRotation)] = keys(0, 90);
                clip.keyframes[std::string(property::kOpacity)] = keys(1, 0.4);
                return s;
            }, at);
        }
    }

    // Colour grading.
    const auto graded = [&](std::string name, std::function<void(ColorGrade&)> change) {
        add("color/" + name, [change] {
            Scene s;
            Clip& clip = s.still("gradient");
            change(clip.color);
            return s;
        });
    };
    graded("exposure_up", [](ColorGrade& g) { g.exposure = 1; });
    graded("exposure_down", [](ColorGrade& g) { g.exposure = -1.5; });
    graded("contrast_up", [](ColorGrade& g) { g.contrast = 0.6; });
    graded("contrast_down", [](ColorGrade& g) { g.contrast = -0.6; });
    graded("saturation_grey", [](ColorGrade& g) { g.saturation = -1; });
    graded("saturation_up", [](ColorGrade& g) { g.saturation = 0.6; });
    graded("warm", [](ColorGrade& g) { g.temperature = 0.7; });
    graded("cool", [](ColorGrade& g) { g.temperature = -0.7; });
    graded("tint_magenta", [](ColorGrade& g) { g.tint = 0.6; });
    graded("tint_green", [](ColorGrade& g) { g.tint = -0.6; });
    graded("lift", [](ColorGrade& g) { g.lift = {0.4, 0.2, 0.2}; });
    graded("lift_dark", [](ColorGrade& g) { g.lift = {-0.3, 0.4, -0.3}; });
    graded("gamma", [](ColorGrade& g) { g.gamma = {-0.4, 0.1, 0.2}; });
    graded("gamma_master", [](ColorGrade& g) { g.gamma.master = -0.4; });
    graded("gain", [](ColorGrade& g) { g.gain = {0.3, -0.3, -0.2}; });
    graded("gain_master", [](ColorGrade& g) { g.gain.master = 0.5; });
    graded("offset", [](ColorGrade& g) { g.offset = {0.2, 0.4, 0.1}; });
    graded("curve_s", [](ColorGrade& g) { g.master = {{0, 0}, {0.25, 0.15}, {0.75, 0.85}, {1, 1}}; });
    graded("curve_red", [](ColorGrade& g) { g.red = {{0, 0.1}, {0.5, 0.65}, {1, 1}}; });
    graded("curve_blue_down", [](ColorGrade& g) { g.blue = {{0, 0}, {0.5, 0.35}, {1, 0.9}}; });
    graded("hue_vs_sat", [](ColorGrade& g) { g.hueVsSat = {{0, 0.5}, {0.33, 1.0}, {0.66, 0.1}, {1, 0.5}}; });
    graded("hue_vs_hue", [](ColorGrade& g) { g.hueVsHue = {{0, 0.5}, {0.5, 0.8}, {1, 0.5}}; });
    graded("luma_vs_sat", [](ColorGrade& g) { g.lumaVsSat = {{0, 0.0}, {0.5, 0.5}, {1, 1.0}}; });
    graded("hsl_red", [](ColorGrade& g) { g.hsl[0] = {0.5, 0.5, 0.2}; });
    graded("hsl_green", [](ColorGrade& g) { g.hsl[3] = {-0.5, -1.0, 0}; });
    graded("hsl_blue", [](ColorGrade& g) { g.hsl[5] = {0.3, 0.4, -0.4}; });
    graded("combined", [](ColorGrade& g) {
        g.exposure = 0.3;
        g.contrast = 0.2;
        g.saturation = 0.2;
        g.temperature = 0.2;
        g.gamma = {0.1, -0.2, 0};
    });

    // Looks from the library, full and at half strength.
    for (const LookInfo& look : builtInLooks()) {
        for (const double intensity : {1.0, 0.5}) {
            if (intensity < 1 && &look - builtInLooks().data() >= 4)
                continue; // half strength for the first few is enough
            add("looks/" + slug(look.id) + (intensity < 1 ? "_half" : ""), [look, intensity] {
                Scene s;
                Clip& clip = s.still("gradient");
                clip.look = ClipLook{.id = look.id, .title = look.title, .intensity = intensity, .grade = look.grade};
                return s;
            });
        }
    }

    // Masks, keys and mattes.
    const auto masked = [&](std::string name, std::vector<ClipMask> masks) {
        add("masks/" + name, [masks] {
            Scene s;
            s.still("gradient");
            Clip& clip = s.still("pattern", 1);
            clip.masks = masks;
            return s;
        });
    };
    const std::pair<const char*, MaskShape> shapes[] = {{"rectangle", MaskShape::Rectangle}, {"ellipse", MaskShape::Ellipse},
                                                         {"line", MaskShape::Line}, {"star", MaskShape::Star},
                                                         {"heart", MaskShape::Heart}};
    for (const auto& [name, shape] : shapes) {
        masked(name, {ClipMask{.shape = shape}});
        masked(std::string(name) + "_inverted", {ClipMask{.shape = shape, .invert = true}});
    }
    const ClipMask pen{.shape = MaskShape::Pen, .points = {{0.2, 0.2}, {0.8, 0.3}, {0.6, 0.8}, {0.25, 0.7}}};
    masked("pen", {pen});
    masked("pen_inverted", {ClipMask{.shape = MaskShape::Pen, .invert = true, .points = pen.points}});
    masked("rectangle_round_turned", {ClipMask{.shape = MaskShape::Rectangle, .rotation = 20, .roundness = 0.6}});
    masked("ellipse_soft", {ClipMask{.shape = MaskShape::Ellipse, .width = 0.4, .feather = 0.2}});
    masked("two_shapes", {ClipMask{.shape = MaskShape::Ellipse, .x = 0.3, .width = 0.3, .height = 0.4},
                          ClipMask{.shape = MaskShape::Star, .x = 0.72, .width = 0.35, .height = 0.5}});

    const auto keyedCase = [&](std::string name, std::function<void(ChromaKey&)> change) {
        add("key/" + name, [change] {
            Scene s;
            s.still("gradient");
            Clip& clip = s.still("keyed", 1);
            clip.chromaKey.enabled = true;
            change(clip.chromaKey);
            return s;
        });
    };
    keyedCase("default", [](ChromaKey&) {});
    keyedCase("tight", [](ChromaKey& k) { k.tolerance = 0.03; });
    keyedCase("soft", [](ChromaKey& k) { k.softness = 0.4; });
    keyedCase("no_spill", [](ChromaKey& k) { k.spill = 0; });
    const std::pair<const char*, MatteMode> mattes[] = {{"alpha", MatteMode::Alpha}, {"alpha_inverted", MatteMode::InvertedAlpha},
                                                         {"luma", MatteMode::Luma}, {"luma_inverted", MatteMode::InvertedLuma}};
    for (const auto& [name, mode] : mattes) {
        add(std::string("matte/") + name, [mode] {
            Scene s;
            s.still("gradient");
            Clip& clip = s.still("pattern", 1);
            clip.matte = mode;
            s.still(mode == MatteMode::Alpha || mode == MatteMode::InvertedAlpha ? "disc" : "spot", 2);
            return s;
        });
    }

    // Titles: shapes behind the text, styling, layout, and animations part way.
    const auto titled = [&](std::string name, std::function<void(ClipText&)> change, double at = 1.0) {
        add("titles/" + name, [change] {
            Scene s;
            s.still("gradient");
            ClipText text;
            text.text = "Golden Frame";
            text.size = 0.14;
            change(text);
            s.title(text);
            return s;
        }, at);
    };
    const std::pair<const char*, TitleShapeKind> titleShapes[] = {
        {"box", TitleShapeKind::Box}, {"rounded", TitleShapeKind::Rounded}, {"pill", TitleShapeKind::Pill},
        {"speech", TitleShapeKind::Speech}, {"thought", TitleShapeKind::Thought}, {"banner", TitleShapeKind::Banner},
        {"underline", TitleShapeKind::Underline}, {"circle", TitleShapeKind::Circle}, {"arrow", TitleShapeKind::Arrow},
        {"tag", TitleShapeKind::Tag}};
    titled("plain", [](ClipText&) {});
    for (const auto& [name, kind] : titleShapes)
        titled(std::string("shape_") + name, [kind](ClipText& t) { t.shape.kind = kind; t.shape.color = {0.1, 0.2, 0.6, 0.85}; });
    titled("bold_italic", [](ClipText& t) { t.bold = true; t.italic = true; });
    titled("outline", [](ClipText& t) { t.outlineWidth = 0.08; t.outlineColor = {0.9, 0.1, 0.1, 1}; });
    titled("shadow", [](ClipText& t) { t.shadow = true; });
    titled("coloured", [](ClipText& t) { t.color = {1, 0.85, 0.2, 1}; });
    titled("left", [](ClipText& t) { t.alignment = TextAlignment::Left; t.text = "Left\nAligned text"; });
    titled("right", [](ClipText& t) { t.alignment = TextAlignment::Right; t.text = "Right\nAligned text"; });
    titled("large", [](ClipText& t) { t.size = 0.3; t.text = "BIG"; });
    titled("letter_spacing", [](ClipText& t) { t.letterSpacing = 0.3; });
    titled("line_spacing", [](ClipText& t) { t.text = "One\nTwo\nThree"; t.lineSpacing = 1.6; t.size = 0.1; });
    for (const char* preset : {"fade", "rise", "zoom", "typewriter", "wipe", "spin"})
        titled(std::string("in_") + preset, [preset](ClipText& t) { t.animationIn = {.preset = preset, .duration = 1.0}; }, 0.5);
    for (const char* preset : {"wave", "pulse", "shake"})
        titled(std::string("loop_") + preset, [preset](ClipText& t) { t.animationLoop = {.preset = preset, .duration = 1.5}; }, 0.8);
    titled("roll_up", [](ClipText& t) { t.scroll = TextScroll::RollUp; t.text = "Credits\nRoll\nUp"; }, 1.5);

    // Stickers: one of each design's motions, part way through its loop.
    int stickerIndex = 0;
    for (const QVariant& entry : stickers::catalog()) {
        if (stickerIndex++ % 4 != 0)
            continue;
        const QString id = entry.toMap().value(QStringLiteral("id")).toString();
        add("stickers/" + slug(id.toStdString()), [id] {
            Scene s;
            s.still("gradient");
            s.sticker(id);
            return s;
        }, 0.6);
    }

    // Compound clips and adjustment layers.
    add("structure/compound", [] {
        Scene s;
        Sequence sequence{.id = s.project->allocateId(), .name = "Inner"};
        Track bottom{.id = s.project->allocateId(), .kind = TrackKind::Video, .name = "V1"};
        Track top{.id = s.project->allocateId(), .kind = TrackKind::Video, .name = "V2"};
        Clip a{.id = s.project->allocateId(), .mediaId = s.mediaFor("gradient"), .start = 0, .duration = 3 * kSec};
        Clip b{.id = s.project->allocateId(), .mediaId = s.mediaFor("disc"), .start = 0, .duration = 3 * kSec};
        b.visual.transform.scale = 0.5;
        bottom.insertClip(a);
        top.insertClip(b);
        sequence.tracks = {bottom, top};
        s.project->sequences.push_back(sequence);
        Clip compound{.id = s.project->allocateId(), .start = 0, .duration = 3 * kSec};
        compound.sequenceId = sequence.id;
        s.insert(0, compound);
        return s;
    });
    add("structure/compound_placed", [] {
        Scene s;
        s.still("pattern");
        Sequence sequence{.id = s.project->allocateId(), .name = "Inner"};
        Track bottom{.id = s.project->allocateId(), .kind = TrackKind::Video, .name = "V1"};
        bottom.insertClip(Clip{.id = s.project->allocateId(), .mediaId = s.mediaFor("gradient"), .start = 0, .duration = 3 * kSec});
        sequence.tracks = {bottom};
        s.project->sequences.push_back(sequence);
        Clip compound{.id = s.project->allocateId(), .start = 0, .duration = 3 * kSec};
        compound.sequenceId = sequence.id;
        compound.visual.transform = {0.2, 0.1, 0.5, 15};
        compound.visual.opacity = 0.8;
        compound.effects = {ClipEffect{.effectId = "tve.blur.gaussian"}};
        s.insert(1, compound);
        return s;
    });
    add("structure/adjustment_grade", [] {
        Scene s;
        s.still("gradient");
        s.still("disc", 1);
        Clip adjustment{.id = s.project->allocateId(), .start = 0, .duration = 3 * kSec};
        adjustment.adjustment = true;
        adjustment.color.saturation = -1;
        adjustment.color.contrast = 0.4;
        s.insert(2, adjustment);
        return s;
    });
    add("structure/adjustment_effect_half", [] {
        Scene s;
        s.still("pattern");
        Clip adjustment{.id = s.project->allocateId(), .start = 0, .duration = 3 * kSec};
        adjustment.adjustment = true;
        adjustment.effects = {ClipEffect{.effectId = "tve.blur.gaussian"}};
        adjustment.visual.opacity = 0.5;
        s.insert(1, adjustment);
        return s;
    });
    add("structure/adjustment_look", [] {
        Scene s;
        s.still("gradient");
        Clip adjustment{.id = s.project->allocateId(), .start = 0, .duration = 3 * kSec};
        adjustment.adjustment = true;
        const LookInfo& look = builtInLooks().front();
        adjustment.look = ClipLook{.id = look.id, .title = look.title, .grade = look.grade};
        s.insert(1, adjustment);
        return s;
    });
    add("structure/gap_is_black", [] {
        Scene s;
        s.still("gradient", 0, 2, 1);
        return s;
    });
    return cases;
}

// --- Comparing ---

struct Difference {
    double mean = 0;    // average channel difference, 0..255
    double outliers = 0; // share of pixels with a channel off by more than 40
};

Difference compare(const QImage& actual, const QImage& expected)
{
    const QImage a = actual.convertToFormat(QImage::Format_RGB32);
    const QImage b = expected.convertToFormat(QImage::Format_RGB32);
    double sum = 0;
    int outliers = 0;
    for (int y = 0; y < a.height(); ++y) {
        const auto* ra = reinterpret_cast<const QRgb*>(a.constScanLine(y));
        const auto* rb = reinterpret_cast<const QRgb*>(b.constScanLine(y));
        for (int x = 0; x < a.width(); ++x) {
            const int dr = std::abs(qRed(ra[x]) - qRed(rb[x]));
            const int dg = std::abs(qGreen(ra[x]) - qGreen(rb[x]));
            const int db = std::abs(qBlue(ra[x]) - qBlue(rb[x]));
            sum += dr + dg + db;
            outliers += std::max({dr, dg, db}) > 40;
        }
    }
    const double pixels = double(a.width()) * a.height();
    return {sum / (pixels * 3), outliers / pixels};
}

QImage differenceImage(const QImage& actual, const QImage& expected)
{
    QImage out(actual.size(), QImage::Format_RGB32);
    for (int y = 0; y < actual.height(); ++y)
        for (int x = 0; x < actual.width(); ++x) {
            const QRgb a = actual.pixel(x, y);
            const QRgb b = expected.pixel(x, y);
            const int d = std::min(255, 4 * std::max({std::abs(qRed(a) - qRed(b)), std::abs(qGreen(a) - qGreen(b)),
                                                      std::abs(qBlue(a) - qBlue(b))}));
            out.setPixel(x, y, qRgb(d, d, d));
        }
    return out;
}

QString referencePath(const std::string& name)
{
    return QDir(QStringLiteral(TVE_GOLDEN_DIR "/metal")).filePath(QString::fromStdString(name) + QStringLiteral(".png"));
}

} // namespace

TEST_CASE("the golden-frame suite has at least 500 cases, each named once")
{
    const auto cases = allCases();
    CHECK(cases.size() >= 500);
    std::set<std::string> names;
    for (const GoldenCase& c : cases)
        CHECK(names.insert(c.name).second);
    INFO(cases.size() << " cases");
    SUCCEED();
}

TEST_CASE("frames render as their references show")
{
    StillRenderer renderer;
    REQUIRE(renderer.isValid());
    const bool update = qEnvironmentVariableIntValue("TVE_GOLDEN_UPDATE") == 1;
    const QString failures = QDir::temp().filePath(QStringLiteral("tve_golden_failures"));
    QDir(failures).removeRecursively();
    int compared = 0;
    int written = 0;
    for (const GoldenCase& golden : allCases()) {
        INFO(golden.name);
        Scene scene = golden.build();
        const QImage frame = renderer.render(scene.project, Ticks(golden.seconds * kSec), QSize(kCanvasW, kCanvasH));
        REQUIRE_FALSE(frame.isNull());
        const QImage actual = frame.scaled(kReference, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        const QString path = referencePath(golden.name);
        if (update) {
            QDir().mkpath(QFileInfo(path).path());
            REQUIRE(actual.save(path));
            ++written;
            continue;
        }
        const QImage expected(path);
        if (expected.isNull()) {
            FAIL_CHECK("no reference " << path.toStdString() << " (run with TVE_GOLDEN_UPDATE=1 to write it)");
            continue;
        }
        REQUIRE(expected.size() == actual.size());
        const Difference d = compare(actual, expected);
        ++compared;
        const bool ok = d.mean <= 2.0 && d.outliers <= 0.01;
        if (!ok) {
            const QString base = QDir(failures).filePath(QString::fromStdString(slug(golden.name)));
            QDir().mkpath(failures);
            actual.save(base + QStringLiteral("_actual.png"));
            expected.save(base + QStringLiteral("_expected.png"));
            differenceImage(actual, expected).save(base + QStringLiteral("_difference.png"));
        }
        INFO("mean difference " << d.mean << ", " << d.outliers * 100 << " % of pixels far off; pictures in "
                                << failures.toStdString());
        CHECK(ok);
    }
    if (update)
        WARN("wrote " << written << " references");
    else
        CHECK(compared >= 500);
}

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}
