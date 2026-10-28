#include "decode/LottieAnimation.h"

#include <thorvg.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <mutex>
#include <sstream>

namespace tve {

namespace {

void initialize()
{
    static std::once_flag once;
    std::call_once(once, [] { tvg::Initializer::init(0); });
}

} // namespace

struct LottieAnimation::Impl {
    std::string json;
    std::unique_ptr<tvg::Animation> animation;
    // Holds the animation's picture for good (gone before the animation: declared after it).
    std::unique_ptr<tvg::SwCanvas> canvas;
};

LottieAnimation::LottieAnimation(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}

LottieAnimation::~LottieAnimation() = default;

std::unique_ptr<LottieAnimation> LottieAnimation::fromData(const std::string& json)
{
    initialize();
    auto impl = std::make_unique<Impl>();
    impl->json = json;
    impl->animation.reset(tvg::Animation::gen());
    tvg::Picture* picture = impl->animation->picture();
    if (!picture
        || picture->load(impl->json.data(), std::uint32_t(impl->json.size()), "lottie+json", nullptr, true)
               != tvg::Result::Success)
        return nullptr;
    float w = 0.0f;
    float h = 0.0f;
    picture->size(&w, &h);
    impl->canvas.reset(tvg::SwCanvas::gen());
    if (!impl->canvas || impl->canvas->add(picture) != tvg::Result::Success)
        return nullptr;
    const double frames = impl->animation->totalFrame();
    const double duration = impl->animation->duration();
    if (w < 1.0f || h < 1.0f || frames <= 0.0 || duration <= 0.0)
        return nullptr;
    std::unique_ptr<LottieAnimation> result(new LottieAnimation(std::move(impl)));
    result->m_width = int(std::lround(w));
    result->m_height = int(std::lround(h));
    result->m_frames = frames;
    result->m_duration = duration;
    return result;
}

std::unique_ptr<LottieAnimation> LottieAnimation::open(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return nullptr;
    std::ostringstream text;
    text << file.rdbuf();
    return fromData(text.str());
}

std::vector<std::uint8_t> LottieAnimation::render(double seconds, int width, int height)
{
    std::vector<std::uint8_t> pixels(std::size_t(width) * std::size_t(height) * 4, 0);
    if (width <= 0 || height <= 0)
        return pixels;
    const double cycle = std::fmod(std::max(seconds, 0.0), m_duration);
    const float frame = float(std::min(m_frames - 1.0, cycle / m_duration * m_frames));
    m_impl->animation->picture()->size(float(width), float(height));
    m_impl->animation->frame(frame);
    tvg::SwCanvas& canvas = *m_impl->canvas;
    // ABGR8888S: bytes R, G, B, A in memory, straight alpha.
    canvas.target(reinterpret_cast<std::uint32_t*>(pixels.data()), std::uint32_t(width), std::uint32_t(width),
                  std::uint32_t(height), tvg::ColorSpace::ABGR8888S);
    canvas.update();
    canvas.draw(true);
    canvas.sync();
    return pixels;
}

bool isLottieFile(const std::string& path)
{
    std::string lower = path;
    std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return lower.ends_with(".json");
}

} // namespace tve
