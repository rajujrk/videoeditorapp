#include "analysis/MediaAnalysis.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>

using namespace tve;
using Catch::Approx;
namespace fs = std::filesystem;

namespace {

std::string sample(const char* name)
{
    const std::string path = (fs::path(TVE_SAMPLES_DIR) / name).string();
    if (!fs::exists(path))
        SKIP(name << " missing (run tools/make_test_media.sh)");
    return path;
}

} // namespace

TEST_CASE("two recordings of the same sound are lined up by it")
{
    // sync_a.wav has the speech from 1.0 s, sync_b.mov from 2.3 s (quieter, other noise): what
    // b has at t, a has at t − 1.3 s.
    const auto a = analyzeAudio(sample("sync_a.wav"));
    const auto b = analyzeAudio(sample("sync_b.mov"));
    REQUIRE(a);
    REQUIRE(b);
    const auto found = findAudioOffset(*a, *b);
    REQUIRE(found);
    CHECK(found->offset == Approx(-1.3).margin(0.012));
    CHECK(found->confidence > 0.3);
    // The other way round: the opposite offset.
    const auto back = findAudioOffset(*b, *a);
    REQUIRE(back);
    CHECK(back->offset == Approx(1.3).margin(0.012));
    // Beyond the lags searched, or silence: no answer (or none worth trusting).
    const auto near = findAudioOffset(*a, *b, 0.5);
    CHECK((!near || near->confidence < 0.3));
    AudioFeatures silent = *a;
    std::fill(silent.onset.begin(), silent.onset.end(), 0.0f);
    CHECK_FALSE(findAudioOffset(*a, silent));
}
