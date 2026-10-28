#include "commands/CommandStack.h"
#include "commands/EditCommands.h"
#include "io/LutFile.h"
#include "io/ProjectSerializer.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>

using namespace tve;
using Catch::Approx;

namespace {

constexpr Ticks kSec = kTicksPerSecond;

std::array<float, 3> graded(const ColorGrade& grade, std::array<float, 3> rgb)
{
    return applyGrade(gradeCoefficients(grade), bakeCurves(grade), rgb, bakeHueCurves(grade));
}

// A .cube text of size n whose output is f(r, g, b).
template <typename F>
std::string cube(int n, F f, const std::string& header = {})
{
    std::string text = header + "LUT_3D_SIZE " + std::to_string(n) + "\n";
    for (int b = 0; b < n; ++b)
        for (int g = 0; g < n; ++g)
            for (int r = 0; r < n; ++r) {
                const auto o = f(float(r) / float(n - 1), float(g) / float(n - 1), float(b) / float(n - 1));
                text += std::to_string(o[0]) + " " + std::to_string(o[1]) + " " + std::to_string(o[2]) + "\n";
            }
    return text;
}

} // namespace

TEST_CASE("a neutral grade leaves colours alone", "[color]")
{
    const ColorGrade neutral;
    CHECK(neutral.isNeutral());
    for (auto rgb : {std::array<float, 3>{0, 0, 0}, {1, 1, 1}, {0.2f, 0.5f, 0.9f}, {0.435f, 0.1f, 0.0f}}) {
        const auto out = graded(neutral, rgb);
        for (int c = 0; c < 3; ++c)
            CHECK(out[c] == Approx(rgb[c]).margin(1e-5));
    }
}

TEST_CASE("basic corrections", "[color]")
{
    ColorGrade g;
    g.exposure = 1.0; // twice the light
    auto out = graded(g, {kMidGrey, kMidGrey, kMidGrey});
    CHECK(out[0] == Approx(std::pow(std::pow(kMidGrey, 2.2f) * 2.0f, 1.0f / 2.2f)).margin(1e-5));

    g = {};
    g.saturation = -1.0;
    out = graded(g, {0.8f, 0.2f, 0.1f});
    CHECK(out[0] == Approx(out[1]).margin(1e-6));
    CHECK(out[1] == Approx(out[2]).margin(1e-6));

    g = {};
    g.temperature = 1.0; // warmer: more red, less blue
    out = graded(g, {0.5f, 0.5f, 0.5f});
    CHECK(out[0] > 0.5f);
    CHECK(out[1] == Approx(0.5f).margin(1e-5));
    CHECK(out[2] < 0.5f);

    g = {};
    g.contrast = 1.0; // twice as steep around mid grey
    out = graded(g, {kMidGrey + 0.1f, kMidGrey, kMidGrey - 0.1f});
    CHECK(out[0] == Approx(kMidGrey + 0.2f).margin(1e-5));
    CHECK(out[1] == Approx(kMidGrey).margin(1e-5));
    CHECK(out[2] == Approx(kMidGrey - 0.2f).margin(1e-5));
}

TEST_CASE("colour wheels push shadows, midtones and highlights", "[color]")
{
    ColorGrade g;
    g.lift.master = 0.5; // raise black by a quarter, leave white
    auto out = graded(g, {0, 0, 0});
    CHECK(out[0] == Approx(0.25f).margin(1e-5));
    CHECK(graded(g, {1, 1, 1})[0] == Approx(1.0f).margin(1e-5));

    g = {};
    g.gain.master = -0.5; // white to half, black stays
    CHECK(graded(g, {1, 1, 1})[0] == Approx(0.5f).margin(1e-5));
    CHECK(graded(g, {0, 0, 0})[0] == Approx(0.0f).margin(1e-5));

    g = {};
    g.gamma.master = 0.5; // brighter midtones, same ends
    CHECK(graded(g, {0.5f, 0.5f, 0.5f})[0] > 0.5f);
    CHECK(graded(g, {1, 1, 1})[0] == Approx(1.0f).margin(1e-5));

    g = {};
    g.gain.x = 1.0; // towards red (hue 0)
    out = graded(g, {0.5f, 0.5f, 0.5f});
    CHECK(out[0] > 0.5f);
    CHECK(out[1] < 0.5f);
    CHECK(out[2] < 0.5f);
    CHECK(out[1] == Approx(out[2]).margin(1e-5));

    g = {};
    g.offset.master = 0.4;
    CHECK(graded(g, {0.2f, 0.2f, 0.2f})[0] == Approx(0.3f).margin(1e-5));
}

TEST_CASE("tone curves", "[color]")
{
    CHECK(evaluateColorCurve({}, 0.3) == 0.3);
    const ColorCurve s{{0, 0}, {0.25, 0.15}, {0.75, 0.85}, {1, 1}};
    for (const CurvePoint& p : s)
        CHECK(evaluateColorCurve(s, p.x) == Approx(p.y).margin(1e-9));
    double last = -1;
    for (int i = 0; i <= 100; ++i) { // smooth and never turning back
        const double y = evaluateColorCurve(s, i / 100.0);
        CHECK(y >= last);
        last = y;
    }
    CHECK(evaluateColorCurve(s, 0.5) == Approx(0.5).margin(1e-9));
    const ColorCurve inverted{{0, 1}, {1, 0}};
    CHECK(evaluateColorCurve(inverted, 0.2) == Approx(0.8).margin(1e-9));

    ColorGrade g;
    g.red = inverted;
    g.master = {{0, 0}, {1, 0.5}};
    const auto tables = bakeCurves(g, 5);
    CHECK(tables[0][0] == Approx(0.5f)); // red: inverted, then halved by master
    CHECK(tables[0][4] == Approx(0.0f));
    CHECK(tables[1][4] == Approx(0.5f)); // green: master only
    const auto out = graded(g, {0.0f, 1.0f, 0.5f});
    CHECK(out[0] == Approx(0.5f).margin(1e-4));
    CHECK(out[1] == Approx(0.5f).margin(1e-4));
    CHECK(out[2] == Approx(0.25f).margin(1e-4));
}

TEST_CASE("HSV round trip", "[color]")
{
    for (auto rgb : {std::array<float, 3>{1, 0, 0}, {0.2f, 0.8f, 0.4f}, {0.1f, 0.3f, 0.9f}, {0.5f, 0.5f, 0.5f}, {0, 0, 0}}) {
        const auto back = hsvToRgb(rgbToHsv(rgb));
        for (int c = 0; c < 3; ++c)
            CHECK(back[c] == Approx(rgb[c]).margin(1e-5));
    }
    CHECK(rgbToHsv({1, 0, 0})[0] == Approx(0.0f));
    CHECK(rgbToHsv({0, 1, 0})[0] == Approx(1.0f / 3.0f));
    CHECK(rgbToHsv({0, 0, 1})[0] == Approx(2.0f / 3.0f));
}

TEST_CASE("HSL bands change only their colours", "[color]")
{
    ColorGrade g;
    g.hsl[5].saturation = -1.0; // blues to grey
    const std::array<float, 3> blue = hsvToRgb({240.0f / 360.0f, 0.9f, 0.9f}); // on the blue band
    const std::array<float, 3> red{0.9f, 0.15f, 0.1f};
    auto out = graded(g, blue);
    CHECK(rgbToHsv(out)[1] < 0.05f);
    out = graded(g, red);
    for (int c = 0; c < 3; ++c)
        CHECK(out[c] == Approx(red[c]).margin(1e-4)); // reds untouched
    out = graded(g, {0.5f, 0.5f, 0.5f});
    CHECK(out[0] == Approx(0.5f).margin(1e-5)); // greys untouched

    g = {};
    g.hsl[0].hue = 1.0; // reds turn 30° towards yellow
    out = graded(g, {1.0f, 0.0f, 0.0f});
    CHECK(rgbToHsv(out)[0] == Approx(30.0f / 360.0f).margin(1e-3));
    g = {};
    g.hsl[0].luminance = -1.0;
    CHECK(graded(g, {1.0f, 0.0f, 0.0f})[0] == Approx(0.5f).margin(1e-4));
    // Between two bands (15°: halfway from red to orange) both count half.
    g = {};
    g.hsl[1].saturation = -1.0;
    out = graded(g, hsvToRgb({15.0f / 360.0f, 1.0f, 1.0f}));
    CHECK(rgbToHsv(out)[1] == Approx(0.5f).margin(1e-3));
}

TEST_CASE("hue curves", "[color]")
{
    ColorGrade g;
    g.hueVsSat = {{0, 0.5}, {0.33, 0.0}, {0.5, 0.5}, {1, 0.5}}; // greens to grey
    const auto green = hsvToRgb({0.33f, 0.9f, 0.8f}); // on the curve point
    auto out = graded(g, green);
    CHECK(rgbToHsv(out)[1] < 0.05f);
    const auto red = hsvToRgb({0.0f, 0.9f, 0.8f});
    out = graded(g, red);
    CHECK(rgbToHsv(out)[1] == Approx(0.9f).margin(1e-3));

    g = {};
    g.hueVsHue = {{0, 1.0}, {1, 1.0}}; // every hue +60°
    out = graded(g, red);
    CHECK(rgbToHsv(out)[0] == Approx(1.0f / 6.0f).margin(1e-3));

    g = {};
    g.lumaVsSat = {{0, 0.0}, {0.5, 0.0}, {1, 0.5}}; // dark colours lose their colour
    out = graded(g, hsvToRgb({0.6f, 1.0f, 0.25f}));
    CHECK(rgbToHsv(out)[1] < 0.05f);

    const auto tables = bakeHueCurves(ColorGrade{}, 4);
    CHECK(tables[1][2] == 0.5f);
}

TEST_CASE("grades are checked", "[color]")
{
    ColorGrade g;
    g.exposure = 5;
    CHECK(validateGrade(g).value() == "exposure must be between -4 and +4 stops");
    g = {};
    g.tint = -1.5;
    CHECK(validateGrade(g).has_value());
    g = {};
    g.gamma.x = 0.9;
    g.gamma.y = 0.9;
    CHECK(validateGrade(g).value() == "gamma is out of range");
    g = {};
    g.blue = {{0, 0}};
    CHECK(validateGrade(g).value() == "a curve has 2 to 16 points");
    g.blue = {{0.1, 0}, {1, 1}};
    CHECK(validateGrade(g).value() == "a curve runs from input 0 to input 1");
    g.blue = {{0, 0}, {0.5, 1.2}, {1, 1}};
    CHECK(validateGrade(g).value() == "curve points are between 0 and 1");
    g.blue = {{0, 0}, {0.5, 0.5}, {0.5, 0.6}, {1, 1}};
    CHECK(validateGrade(g).value() == "curve points must be in order");
    g = {};
    g.lutIntensity = 2;
    CHECK(validateGrade(g).has_value());
    g = {};
    g.hsl[3].saturation = 1.5;
    CHECK(validateGrade(g).value() == "HSL adjustments must be between -100% and +100%");
    g = {};
    g.hueVsHue = {{0, 0.5}};
    CHECK(validateGrade(g).has_value());
}

TEST_CASE("grades on clips: set, paste, split, save", "[color]")
{
    Project project;
    CommandStack stack(project);
    const auto push = [&](auto command) {
        auto error = stack.push(std::move(command));
        INFO(error.value_or(""));
        REQUIRE_FALSE(error);
    };
    auto video = std::make_unique<AddTrackCommand>(TrackKind::Video, "V1");
    const auto* videoCmd = video.get();
    push(std::move(video));
    auto audio = std::make_unique<AddTrackCommand>(TrackKind::Audio, "A1");
    const auto* audioCmd = audio.get();
    push(std::move(audio));
    auto media = std::make_unique<ImportMediaCommand>(MediaItem{.path = "/m.mp4", .duration = 60 * kSec, .hasAudio = true});
    const auto* mediaCmd = media.get();
    push(std::move(media));
    auto a = std::make_unique<AddClipCommand>(videoCmd->trackId(), mediaCmd->mediaId(), 0, 4 * kSec, 0);
    const Id clipA = (push(std::move(a)), project.tracks[0].clips[0].id);
    push(std::make_unique<AddClipCommand>(videoCmd->trackId(), mediaCmd->mediaId(), 4 * kSec, 4 * kSec, 0));
    const Id clipB = project.tracks[0].clips[1].id;
    push(std::make_unique<AddClipCommand>(audioCmd->trackId(), mediaCmd->mediaId(), 0, 4 * kSec, 0));
    const Id sound = project.tracks[1].clips[0].id;

    ColorGrade warm;
    warm.temperature = 0.4;
    warm.red = {{0, 0}, {0.5, 0.6}, {1, 1}};
    warm.lutPath = "/luts/film.cube";
    warm.hsl[4].hue = 0.3;
    warm.hueVsSat = {{0, 0.5}, {0.5, 0.8}, {1, 0.5}};
    warm.lutIntensity = 0.5;
    push(std::make_unique<SetClipColorCommand>(std::vector<Id>{clipA}, warm));
    CHECK(stack.undoLabel() == "Change Color");
    CHECK(project.locateClip(clipA)->clip->color == warm);

    push(std::make_unique<SetClipColorCommand>(std::vector<Id>{clipB}, warm, "Paste Grade"));
    CHECK(stack.undoLabel() == "Paste Grade");
    stack.undo();
    CHECK(project.locateClip(clipB)->clip->color.isNeutral());
    stack.redo();

    CHECK(stack.push(std::make_unique<SetClipColorCommand>(std::vector<Id>{sound}, warm)).value()
          == "colour applies to video clips");
    ColorGrade bad;
    bad.exposure = 9;
    CHECK(stack.push(std::make_unique<SetClipColorCommand>(std::vector<Id>{clipA}, bad)).has_value());

    auto split = std::make_unique<SplitClipCommand>(clipA, 2 * kSec);
    const auto* splitCmd = split.get();
    push(std::move(split));
    CHECK(project.locateClip(splitCmd->rightClipId())->clip->color == warm);

    const Project loaded = projectFromJson(projectToJson(project));
    CHECK(loaded.tracks[0].clips[0].color == warm);
    CHECK(gradeFromJson(gradeToJson(warm)) == warm);
    CHECK(gradeFromJson(nlohmann::json::object()).isNeutral());
}

TEST_CASE(".cube LUTs", "[color][lut]")
{
    const Lut3d identity = parseCubeLut(cube(2, [](float r, float g, float b) { return std::array{r, g, b}; },
                                            "# made by hand\nTITLE \"Identity\"\n"));
    CHECK(identity.size == 2);
    CHECK(identity.title == "Identity");
    const auto out = identity.lookup({0.2f, 0.7f, 0.4f});
    CHECK(out[0] == Approx(0.2f).margin(1e-5));
    CHECK(out[1] == Approx(0.7f).margin(1e-5));
    CHECK(out[2] == Approx(0.4f).margin(1e-5));

    // Red changes fastest in the file.
    const Lut3d swap = parseCubeLut(cube(3, [](float r, float g, float b) { return std::array{b, g, r}; }));
    CHECK(swap.at(2, 0, 0)[2] == Approx(1.0f));
    const auto swapped = swap.lookup({1.0f, 0.5f, 0.0f});
    CHECK(swapped[0] == Approx(0.0f).margin(1e-5));
    CHECK(swapped[2] == Approx(1.0f).margin(1e-5));

    const Lut3d ranged = parseCubeLut("DOMAIN_MIN 0 0 0\nDOMAIN_MAX 2 2 2\n"
                                      + cube(2, [](float r, float g, float b) { return std::array{r, g, b}; }));
    CHECK(ranged.lookup({1.0f, 1.0f, 1.0f})[0] == Approx(0.5f).margin(1e-5));

    const auto fails = [](const std::string& text) {
        try {
            parseCubeLut(text);
        } catch (const LutError& e) {
            return std::string(e.what());
        }
        return std::string();
    };
    CHECK(fails("LUT_1D_SIZE 1024\n0 0 0\n") == "1D LUTs are not supported; use a 3D LUT");
    CHECK(fails("0 0 0\n") == "table values before LUT_3D_SIZE on line 1");
    CHECK(fails("TITLE \"x\"\n") == "no LUT_3D_SIZE: not a 3D .cube LUT");
    CHECK(fails("LUT_3D_SIZE 2\n0 0 0\n") == "the table has 1 rows, expected 8");
    CHECK(fails("LUT_3D_SIZE 1\n") == "LUT size must be between 2 and 129");
    CHECK(fails("LUT_3D_SIZE 2\n0 0 zero\n") == "bad number on line 2");
    CHECK(fails("LUT_3D_SIZE 2\n0 0\n") == "expected three numbers on line 2");
}

TEST_CASE(".3dl LUTs", "[color][lut]")
{
    // Mesh line, then 10-bit rows with blue changing fastest; output = input with red and
    // blue swapped.
    std::string text = "# test\n0 1023\n";
    for (int r = 0; r < 2; ++r)
        for (int g = 0; g < 2; ++g)
            for (int b = 0; b < 2; ++b)
                text += std::to_string(b * 1023) + " " + std::to_string(g * 1023) + " " + std::to_string(r * 1023) + "\n";
    const Lut3d lut = parse3dlLut(text);
    CHECK(lut.size == 2);
    const auto out = lut.lookup({1.0f, 0.0f, 0.25f});
    CHECK(out[0] == Approx(0.25f).margin(1e-5));
    CHECK(out[2] == Approx(1.0f).margin(1e-5));

    // Without the mesh line, 12-bit.
    std::string twelve;
    for (int i = 0; i < 8; ++i)
        twelve += "4095 0 2047\n";
    const Lut3d flat = parse3dlLut(twelve);
    CHECK(flat.size == 2);
    CHECK(flat.lookup({0.3f, 0.3f, 0.3f})[2] == Approx(2047.0f / 4095.0f).margin(1e-5));

    CHECK_THROWS_AS(parse3dlLut("0 1023\n0 0 0\n"), LutError);
}

TEST_CASE("LUT files load by extension", "[color][lut]")
{
    const auto dir = std::filesystem::temp_directory_path();
    const auto path = dir / "tve_test.cube";
    std::ofstream(path) << cube(2, [](float r, float g, float b) { return std::array{r, g, b}; });
    CHECK(loadLut(path).size == 2);
    std::filesystem::remove(path);
    CHECK_THROWS_AS(loadLut(dir / "tve_missing.cube"), LutError);
    const auto other = dir / "tve_test.png";
    std::ofstream(other) << "x";
    CHECK_THROWS_WITH(loadLut(other), Catch::Matchers::Equals("unsupported LUT format .png (use .cube or .3dl)"));
    std::filesystem::remove(other);
}
