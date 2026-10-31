#include "analysis/MediaAnalysis.h"
#include "audio/SoundPacks.h"
#include "audio/SoundPreview.h"
#include "audio/Synth.h"
#include "decode/AudioDecoder.h"
#include "encode/WavWriter.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>

#include <unistd.h>

using namespace tve;
using Catch::Approx;
namespace fs = std::filesystem;

TEST_CASE("every built-in sound renders, as long as it says, loud enough and never clipping")
{
    const auto& sounds = builtInSounds();
    REQUIRE(sounds.size() == 27);
    CHECK(std::ranges::count(sounds, std::string("music"), &SoundRecipe::kind) == 12);
    CHECK(std::ranges::count(sounds, std::string("sfx"), &SoundRecipe::kind) == 15);
    CHECK(sounds.front().kind == "music"); // music first
    std::set<std::string> ids, moods;
    for (const SoundRecipe& s : sounds) {
        INFO(s.id);
        CHECK(ids.insert(s.id).second);
        if (s.kind == "music")
            moods.insert(s.category);
        CHECK(nlohmann::json::parse(s.json).value("license", "") == "TVE-Original");
        std::string error;
        const auto audio = renderSound(s, &error);
        REQUIRE(audio);
        CHECK(double(audio->size() / 2) / 48000.0 == Approx(soundDuration(s)).margin(0.01));
        float peak = 0.0f;
        for (const float v : *audio)
            peak = std::max(peak, std::abs(v));
        CHECK(peak <= 1.0f);
        CHECK(peak > 0.4f);
    }
    CHECK(moods == std::set<std::string>{"calm", "upbeat", "cinematic", "lofi", "corporate", "electronic"});
    // The same recipe, the same sound.
    CHECK(*renderSound(*findSound("tve.music.pulse")) == *renderSound(*findSound("tve.music.pulse")));
    CHECK_FALSE(findSound("tve.music.none"));
    std::string error;
    CHECK_FALSE(renderSound({"x", "sfx", "X", "", R"({"generator": "kazoo"})"}, &error));
    CHECK(error.find("kazoo") != std::string::npos);
}

TEST_CASE("music keeps its tempo and a whoosh sweeps up")
{
    // The beat finder (12a) hears the recipe's tempo in the rendered track.
    for (const char* id : {"tve.music.good_morning", "tve.music.neon_run"}) {
        const SoundRecipe& recipe = *findSound(id);
        const auto audio = renderSound(recipe);
        REQUIRE(audio);
        const std::string path = (fs::temp_directory_path() / ("tve_synth_" + std::to_string(::getpid()) + ".wav")).string();
        {
            auto writer = WavWriter::open(path, 48000, 2);
            REQUIRE(writer);
            writer->write(audio->data(), std::int64_t(audio->size() / 2));
            writer->finish();
        }
        const auto features = analyzeAudio(path);
        REQUIRE(features);
        const double tempo = nlohmann::json::parse(recipe.json).at("tempo").get<double>();
        INFO(id);
        CHECK(findBeats(*features).bpm == Approx(tempo).margin(2.0));
        fs::remove(path);
    }
    // The whoosh's sound moves from low to high: more change between samples at the end.
    const auto whoosh = renderSound(*findSound("tve.sfx.whoosh"));
    REQUIRE(whoosh);
    const auto roughness = [&](double from, double to) {
        double sum = 0.0, level = 0.0;
        for (auto i = std::size_t(from * 48000) + 1; i < std::size_t(to * 48000); ++i) {
            sum += std::abs((*whoosh)[i * 2] - (*whoosh)[(i - 1) * 2]);
            level += std::abs((*whoosh)[i * 2]);
        }
        return sum / std::max(1e-9, level);
    };
    CHECK(roughness(0.6, 0.75) > roughness(0.15, 0.3) * 1.3); // (a broad first-order band)
}

TEST_CASE("a sound pack is written, read back and checked file by file")
{
    const fs::path root = fs::temp_directory_path() / ("tve_pack_" + std::to_string(::getpid()));
    fs::remove_all(root);
    const SoundRecipe* music = findSound("tve.music.sunrise");
    const SoundRecipe* pop = findSound("tve.sfx.pop");
    REQUIRE(music);
    REQUIRE(pop);
    std::string error;
    REQUIRE(writeSoundPack((root / "pack").string(), "test.pack", "Test", {*music, *pop}, 5, &error));

    auto pack = loadSoundPack((root / "pack").string(), &error);
    REQUIRE(pack);
    CHECK(pack->id == "test.pack");
    CHECK(pack->title == "Test");
    CHECK(pack->license == "TVE-Original");
    CHECK(pack->problems.empty());
    REQUIRE(pack->sounds.size() == 2);
    const PackSound& song = pack->sounds[0];
    CHECK(song.id == "tve.music.sunrise");
    CHECK(song.kind == "music");
    CHECK(song.category == music->category);
    CHECK(song.duration == Approx(soundDuration(*music)).margin(0.01));
    // A long sound gets a short preview, faded out; a short one previews itself.
    REQUIRE(!song.preview.empty());
    auto preview = AudioDecoder::open(song.preview);
    REQUIRE(preview);
    CHECK(double(preview->info().duration) / double(kTicksPerSecond) == Approx(5.0).margin(0.01));
    std::vector<float> tail(2 * 48000 * 5);
    tail.resize(std::size_t(preview->read(tail.data(), 48000 * 5)) * 2);
    CHECK(std::abs(tail[tail.size() - 2]) < 1e-3f);
    CHECK(pack->sounds[1].preview.empty());
    auto whole = AudioDecoder::open(pack->sounds[1].path);
    REQUIRE(whole);
    CHECK(double(whole->info().duration) / double(kTicksPerSecond) == Approx(soundDuration(*pop)).margin(0.01));

    // A changed file is left out (and said so); so is one outside the pack's folder.
    {
        std::ofstream(pack->sounds[1].path, std::ios::app) << 'x';
        auto manifestFile = std::ifstream(root / "pack" / "manifest.json");
        auto manifest = nlohmann::json::parse(manifestFile);
        manifest["sounds"].push_back({{"id", "escape"}, {"kind", "sfx"}, {"title", "Escape"},
                                      {"file", "../outside.wav"}, {"sha256", ""}});
        std::ofstream(root / "pack" / "manifest.json") << manifest.dump();
    }
    pack = loadSoundPack((root / "pack").string(), &error);
    REQUIRE(pack);
    REQUIRE(pack->sounds.size() == 1);
    CHECK(pack->sounds[0].id == "tve.music.sunrise");
    REQUIRE(pack->problems.size() == 2);
    CHECK(pack->problems[0].find("Pop") != std::string::npos);
    CHECK(pack->problems[1].find("Escape") != std::string::npos);

    // Not a pack: no manifest, another format, a newer version.
    CHECK(!loadSoundPack(root.string(), &error));
    std::ofstream(root / "manifest.json") << R"({"format": "something-else", "version": 1, "id": "x", "sounds": []})";
    CHECK(!loadSoundPack(root.string(), &error));
    std::ofstream(root / "manifest.json") << R"({"format": "tve-sound-pack", "version": 2, "id": "x", "sounds": []})";
    CHECK(!loadSoundPack(root.string(), &error));
    CHECK(error.find("newer") != std::string::npos);
    // All the packs in a folder.
    CHECK(loadSoundPacks(root.string()).size() == 1);
    fs::remove_all(root);
}

TEST_CASE("a file's SHA-256")
{
    const fs::path path = fs::temp_directory_path() / ("tve_sha_" + std::to_string(::getpid()));
    std::ofstream(path) << "abc";
    CHECK(fileSha256(path.string()) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    fs::remove(path);
    CHECK(fileSha256(path.string()).empty());
}

TEST_CASE("a preview plays its sound once, then silence")
{
    SoundPreview preview(AudioOutput::Backend::None);
    CHECK(!preview.playing());
    std::vector<float> samples(2 * 1000);
    for (std::size_t i = 0; i < samples.size(); ++i)
        samples[i] = float(i % 7) * 0.1f;
    REQUIRE(preview.play(samples));
    CHECK(preview.playing());
    std::vector<float> out(2 * 600, 1.0f);
    preview.render(out.data(), 600);
    CHECK(std::equal(out.begin(), out.end(), samples.begin()));
    CHECK(preview.progress() == Approx(0.6));
    CHECK(preview.playing());
    preview.render(out.data(), 600);
    CHECK(std::equal(out.begin(), out.begin() + 800, samples.begin() + 1200));
    CHECK(std::all_of(out.begin() + 800, out.end(), [](float v) { return v == 0.0f; }));
    CHECK(!preview.playing());
    preview.stop();
    CHECK(!preview.play({}));
}
