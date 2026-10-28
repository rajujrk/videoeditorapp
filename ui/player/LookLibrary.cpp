#include "LookLibrary.h"

#include "gpu/ColorGrader.h"
#include "io/LutFile.h"
#include "io/ProjectSerializer.h"

#include <QFile>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>

const std::vector<LookInfo>& builtInLooks()
{
    static const std::vector<LookInfo> looks = [] {
        std::vector<LookInfo> list;
        QFile file(QStringLiteral(":/effects/looks/looks.json"));
        if (!file.open(QIODevice::ReadOnly)) {
            qWarning("LookLibrary: missing :/effects/looks/looks.json");
            return list;
        }
        try {
            const auto json = nlohmann::json::parse(file.readAll().toStdString());
            for (const auto& entry : json.at("looks")) {
                list.push_back({entry.at("id").get<std::string>(), entry.at("title").get<std::string>(),
                                entry.value("category", std::string()), tve::gradeFromJson(entry.at("grade"))});
            }
        } catch (const std::exception& e) {
            qWarning("LookLibrary: bad looks.json: %s", e.what());
        }
        return list;
    }();
    return looks;
}

const LookInfo* findLook(const std::string& id)
{
    const auto& looks = builtInLooks();
    const auto it = std::ranges::find(looks, id, &LookInfo::id);
    return it != looks.end() ? &*it : nullptr;
}

namespace {

// The ungraded sample scene, gamma-encoded RGB per pixel.
std::array<float, 3> sceneAt(float u, float v)
{
    // Sky to horizon, a warm sun, green hills, blue water at the bottom, and a skin-toned
    // and a grey patch to show tones.
    std::array<float, 3> c{0.30f + 0.45f * v, 0.50f + 0.30f * v, 0.85f - 0.25f * v};
    const float sun = std::hypot(u - 0.72f, v - 0.30f);
    if (sun < 0.10f)
        c = {1.0f, 0.86f, 0.55f};
    else if (sun < 0.22f) {
        const float glow = 1.0f - (sun - 0.10f) / 0.12f;
        for (int i = 0; i < 3; ++i)
            c[std::size_t(i)] += (std::array<float, 3>{0.95f, 0.70f, 0.40f}[std::size_t(i)] - c[std::size_t(i)]) * glow * 0.6f;
    }
    const float hill = 0.62f + 0.06f * std::sin(u * 7.0f);
    if (v > hill)
        c = {0.18f + 0.10f * u, 0.42f + 0.10f * (1 - u), 0.16f};
    if (v > 0.84f)
        c = {0.10f, 0.32f + 0.08f * u, 0.55f + 0.10f * u};
    if (u > 0.08f && u < 0.26f && v > 0.40f && v < 0.80f)
        c = {0.87f, 0.64f, 0.52f}; // skin
    if (u > 0.30f && u < 0.40f && v > 0.55f && v < 0.80f)
        c = {0.5f * v, 0.5f * v, 0.5f * v}; // greys
    return c;
}

} // namespace

QImage gradedSample(const tve::ColorGrade& grade, QSize size)
{
    const tve::GradeCoefficients c = tve::gradeCoefficients(grade);
    const auto curves = tve::bakeCurves(grade);
    const auto hueCurves = tve::bakeHueCurves(grade);
    std::optional<tve::Lut3d> lut;
    if (!grade.lutPath.empty() && grade.lutIntensity > 0.0) {
        try {
            lut = tve::readLut(grade.lutPath);
        } catch (const std::exception&) {
            // shown without its LUT
        }
    }
    QImage image(size, QImage::Format_RGB32);
    for (int y = 0; y < size.height(); ++y) {
        auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < size.width(); ++x) {
            auto rgb = tve::applyGrade(c, curves, sceneAt(float(x) / float(size.width()), float(y) / float(size.height())),
                                       hueCurves);
            if (lut) {
                const auto mapped = lut->lookup(rgb);
                for (int i = 0; i < 3; ++i)
                    rgb[std::size_t(i)] += (mapped[std::size_t(i)] - rgb[std::size_t(i)]) * float(grade.lutIntensity);
            }
            line[x] = qRgb(int(std::clamp(rgb[0], 0.0f, 1.0f) * 255.0f + 0.5f),
                           int(std::clamp(rgb[1], 0.0f, 1.0f) * 255.0f + 0.5f),
                           int(std::clamp(rgb[2], 0.0f, 1.0f) * 255.0f + 0.5f));
        }
    }
    return image;
}

LookPreviewProvider::LookPreviewProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

QImage LookPreviewProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize)
{
    static std::mutex mutex;
    static std::map<QString, QImage> cache;
    QImage image;
    {
        std::lock_guard lock(mutex);
        if (auto it = cache.find(id); it != cache.end())
            image = it->second;
    }
    if (image.isNull()) {
        // "none" is the sample as it is.
        const LookInfo* look = findLook(id.toStdString());
        image = gradedSample(look ? look->grade : tve::ColorGrade{}, QSize(kWidth, kHeight));
        std::lock_guard lock(mutex);
        cache[id] = image;
    }
    if (size)
        *size = image.size();
    return requestedSize.isValid() ? image.scaled(requestedSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation) : image;
}
