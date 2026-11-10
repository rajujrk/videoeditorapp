#include "io/ProjectSerializer.h"
#include "model/Colour.h"
#include "model/Project.h"

#include <nlohmann/json.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace tve;
using Catch::Approx;

TEST_CASE("transfer functions: PQ, HLG and SDR, relative to reference white")
{
    // PQ: 203 nits is reference white; 10 000 nits the top of the signal.
    CHECK(pqFromNits(10000.0) == Approx(1.0));
    CHECK(pqFromNits(0.0) == Approx(0.0).margin(1e-6));
    CHECK(pqFromNits(100.0) == Approx(0.508).margin(0.001));   // a well-known point
    CHECK(pqFromNits(1000.0) == Approx(0.752).margin(0.001));
    CHECK(nitsFromPq(pqFromNits(203.0)) == Approx(203.0));
    CHECK(decodeTransfer(Transfer::Pq, pqFromNits(203.0)) == Approx(1.0));
    for (double v : {0.1, 0.5, 0.9})
        CHECK(encodeTransfer(Transfer::Pq, decodeTransfer(Transfer::Pq, v)) == Approx(v));
    // SDR: a 2.4 power.
    CHECK(decodeTransfer(Transfer::Sdr, 0.5) == Approx(std::pow(0.5, 2.4)));
    CHECK(encodeTransfer(Transfer::Sdr, decodeTransfer(Transfer::Sdr, 0.3)) == Approx(0.3));
    // HLG: reference white (203 nits on a 1000-nit display) sits at about 75 % signal.
    const auto white = encodeHlg({1.0, 1.0, 1.0});
    CHECK(white[0] == Approx(0.75).margin(0.01));
    const auto back = decodeHlg(white);
    CHECK(back[0] == Approx(1.0).margin(1e-6));
    CHECK(decodeHlg({1.0, 1.0, 1.0})[1] == Approx(1000.0 / 203.0)); // peak
}

TEST_CASE("primaries matrices keep white and map the gamuts")
{
    const auto m = primariesMatrix(Primaries::Rec709, Primaries::Rec2020);
    // White stays white (rows sum to 1).
    for (int r = 0; r < 3; ++r)
        CHECK(m[std::size_t(r * 3)] + m[std::size_t(r * 3 + 1)] + m[std::size_t(r * 3 + 2)] == Approx(1.0).margin(1e-6));
    // The known BT.2087 matrix.
    CHECK(m[0] == Approx(0.6274).margin(1e-3));
    CHECK(m[1] == Approx(0.3293).margin(1e-3));
    CHECK(m[4] == Approx(0.9195).margin(1e-3));
    // And back.
    const auto inv = primariesMatrix(Primaries::Rec2020, Primaries::Rec709);
    CHECK(inv[0] == Approx(1.6605).margin(1e-3));
    CHECK(primariesMatrix(Primaries::DisplayP3, Primaries::DisplayP3)[0] == 1.0);
}

TEST_CASE("conversions: SDR into HDR, HDR into SDR with highlights rolled off, identity")
{
    // The same space: nothing.
    CHECK(conversion(workingSpace(ProjectColour::Sdr), workingSpace(ProjectColour::Sdr)).identity);
    // SDR white into a PQ project: reference white, 203 nits.
    const auto up = conversion({Primaries::Rec709, Transfer::Sdr}, workingSpace(ProjectColour::HdrPq));
    CHECK_FALSE(up.identity);
    CHECK(up.targetPeak == 0.0f);
    const auto white = convert(up, {1.0, 1.0, 1.0});
    CHECK(nitsFromPq(white[0]) == Approx(203.0).margin(0.5));
    CHECK(nitsFromPq(white[2]) == Approx(203.0).margin(0.5));
    // PQ into SDR: reference white near SDR white, 1000 nits rolled off below 1, order kept.
    const auto down = conversion(workingSpace(ProjectColour::HdrPq), {Primaries::Rec709, Transfer::Sdr});
    CHECK(down.targetPeak == 1.0f);
    const auto grey = convert(down, {pqFromNits(50.0), pqFromNits(50.0), pqFromNits(50.0)});
    CHECK(grey[0] == Approx(encodeTransfer(Transfer::Sdr, 50.0 / 203.0)).margin(1e-6)); // below the knee: as is
    const double ref = convert(down, {pqFromNits(203.0), pqFromNits(203.0), pqFromNits(203.0)})[0];
    const double bright = convert(down, {pqFromNits(1000.0), pqFromNits(1000.0), pqFromNits(1000.0)})[0];
    CHECK(ref > 0.85);
    CHECK(bright > ref);
    CHECK(bright < 1.0);
    // Pure Rec.2020 green has no Rec.709 equivalent: clipped into gamut, still green.
    const auto green = convert(down, {0.0, pqFromNits(100.0), 0.0});
    CHECK(green[0] == 0.0);
    CHECK(green[1] > 0.5);
    // On an EDR display with 4× headroom, 1000 nits (4.9×) is rolled off to fit under 4.
    const auto edr = conversion(workingSpace(ProjectColour::HdrPq), {Primaries::Rec709, Transfer::Linear}, 4.0);
    CHECK(edr.targetPeak == 4.0f);
    const double peak = convert(edr, {pqFromNits(1000.0), pqFromNits(1000.0), pqFromNits(1000.0)})[0];
    CHECK(peak > 3.0);
    CHECK(peak < 4.0);
    CHECK(convert(edr, {pqFromNits(203.0), pqFromNits(203.0), pqFromNits(203.0)})[0] == Approx(1.0).margin(1e-3));
    // HLG to PQ: both 1000-nit HDR, nothing rolled off.
    const auto hlg = conversion(workingSpace(ProjectColour::HdrHlg), workingSpace(ProjectColour::HdrPq));
    CHECK(hlg.targetPeak == 0.0f);
    CHECK(nitsFromPq(convert(hlg, {0.75, 0.75, 0.75})[0]) == Approx(203.0).margin(5.0));
}

TEST_CASE("a project's colour and its media's are saved, old projects read as SDR")
{
    Project project;
    project.settings.colour = ProjectColour::HdrHlg;
    project.media.push_back({.id = 1, .path = "/m/hdr.mov", .duration = kTicksPerSecond, .hasVideo = true,
                             .colour = {Primaries::Rec2020, Transfer::Pq}});
    auto json = projectToJson(project);
    CHECK(json["settings"]["colour"] == "hdr-hlg");
    const Project back = projectFromJson(json);
    CHECK(back.settings.colour == ProjectColour::HdrHlg);
    CHECK(back.media[0].colour == ColourSpace{Primaries::Rec2020, Transfer::Pq});
    // Without the fields: SDR Rec.709.
    json["settings"].erase("colour");
    json["media"][0].erase("colour");
    const Project old = projectFromJson(json);
    CHECK(old.settings.colour == ProjectColour::Sdr);
    CHECK(old.media[0].colour == ColourSpace{});
    CHECK(toString(ProjectColour::HdrPq) == "hdr-pq");
    CHECK(projectColourFrom("hdr-hlg") == ProjectColour::HdrHlg);
    CHECK(transferFrom("pq") == Transfer::Pq);
    CHECK(primariesFrom("p3") == Primaries::DisplayP3);
}
