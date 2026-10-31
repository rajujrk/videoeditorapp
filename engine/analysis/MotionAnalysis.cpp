#include "analysis/MotionAnalysis.h"

#include "decode/Thumbnail.h"
#include "decode/VideoDecoder.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace tve {

namespace {

// Luma at half the picture's size, each pixel the mean of four.
GrayImage halfLuma(const RgbaImage& picture)
{
    GrayImage out{picture.width / 2, picture.height / 2, {}};
    out.pixels.resize(std::size_t(out.width) * std::size_t(out.height));
    const auto luma = [&](int x, int y) {
        const std::uint8_t* p = &picture.pixels[(std::size_t(y) * std::size_t(picture.width) + std::size_t(x)) * 4];
        return (0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2]) / 255.0f;
    };
    for (int y = 0; y < out.height; ++y)
        for (int x = 0; x < out.width; ++x)
            out.pixels[std::size_t(y) * std::size_t(out.width) + std::size_t(x)] =
                0.25f * (luma(2 * x, 2 * y) + luma(2 * x + 1, 2 * y) + luma(2 * x, 2 * y + 1) + luma(2 * x + 1, 2 * y + 1));
    return out;
}

std::string cachePath(const AnalysisCache& cache, const std::string& fingerprint)
{
    std::string name = fingerprint;
    std::ranges::replace(name, ':', '_');
    return (std::filesystem::path(cache.directory()) / (name + ".motion.json")).string();
}

} // namespace

std::optional<MotionAnalysis> analyzeMotion(const std::string& path, const AnalysisProgress& progress, std::string* error)
{
    auto decoder = VideoDecoder::open(path, error);
    if (!decoder) {
        if (error && error->empty())
            *error = "This file has no pictures to look at.";
        return std::nullopt;
    }
    const double total = std::max(1e-3, toSeconds(decoder->info().duration));
    MotionAnalysis analysis;
    GrayImage previous;
    while (auto frame = decoder->nextFrame()) {
        const auto picture = pictureOf(*frame, 360);
        if (!picture)
            continue;
        GrayImage luma = halfLuma(*picture);
        analysis.width = luma.width;
        analysis.height = luma.height;
        analysis.times.push_back(frame->pts());
        analysis.motion.push_back(previous.width == luma.width && previous.height == luma.height
                                      ? estimateMotion(previous, luma)
                                      : FrameMotion{});
        double sum = 0.0;
        for (const float v : luma.pixels)
            sum += v;
        analysis.brightness.push_back(float(sum / double(std::max<std::size_t>(1, luma.pixels.size()))));
        previous = std::move(luma);
        if (progress && analysis.times.size() % 15 == 0
            && !progress(std::min(1.0, toSeconds(analysis.times.back()) / total)))
            return std::nullopt;
    }
    if (analysis.times.empty()) {
        if (error)
            *error = "This file has no pictures to look at.";
        return std::nullopt;
    }
    return analysis;
}

std::optional<MotionAnalysis> loadMotion(const AnalysisCache& cache, const std::string& fingerprint)
{
    std::ifstream in(cachePath(cache, fingerprint));
    if (!in)
        return std::nullopt;
    try {
        const nlohmann::json json = nlohmann::json::parse(in);
        if (json.value("version", 0) != MotionAnalysis::kVersion)
            return std::nullopt;
        MotionAnalysis a;
        a.width = json.at("width").get<int>();
        a.height = json.at("height").get<int>();
        for (const auto& f : json.at("frames")) {
            a.times.push_back(f.at(0).get<Ticks>());
            a.motion.push_back({f.at(1).get<double>(), f.at(2).get<double>(), f.at(3).get<double>(), f.at(4).get<double>(),
                                f.at(5).get<double>()});
            a.brightness.push_back(f.at(6).get<float>());
        }
        return a;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

bool saveMotion(const AnalysisCache& cache, const std::string& fingerprint, const MotionAnalysis& analysis)
{
    nlohmann::json frames = nlohmann::json::array();
    for (std::size_t k = 0; k < analysis.times.size(); ++k) {
        const FrameMotion& m = analysis.motion[k];
        frames.push_back({analysis.times[k], m.dx, m.dy, m.angle, m.scale, m.confidence, analysis.brightness[k]});
    }
    const std::string path = cachePath(cache, fingerprint);
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    {
        std::ofstream out(path + ".part");
        out << nlohmann::json{{"version", MotionAnalysis::kVersion},
                              {"width", analysis.width},
                              {"height", analysis.height},
                              {"frames", frames}}
                   .dump();
        if (!out)
            return false;
    }
    std::filesystem::rename(path + ".part", path, ec);
    return !ec;
}

} // namespace tve
