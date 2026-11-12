// The performance budgets (docs/TechieVideoEditor_Build_Plan.md §10), measured on this Mac
// with the real engine and renderer, each test failing when its budget is missed:
//
//   playback   4K30 H.264, three layers and two effects, previewed at half resolution: real
//              time, under one dropped frame per 10 s
//   scrubbing  a seek to the frame drawn: under 80 ms with proxies, 150 ms without
//   start-up   the app launched to its Home screen drawn: under 2.5 s
//   opening    a 200-clip project opened, its first frame ready: under 3 s
//   export     1080p H.264 at twice real time or faster
//   memory     the most the process holds while playing and exporting (see kMemoryBudget)
//
// They run on their own (RUN_SERIAL), since anything else on the machine skews them, and
// need the generated samples (tools/make_test_media.sh). WARN lines give the measurements.
#include "Exporter.h"
#include "FrameCompositor.h"
#include "ProjectController.h"
#include "RenderCache.h"

#include "commands/CommandStack.h"
#include "commands/EditCommands.h"
#include "encode/Proxy.h"
#include "io/ProjectSerializer.h"
#include "model/Project.h"
#include "model/RenderCost.h"
#include "playback/TimelinePlayback.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>
#include <rhi/qrhi.h>

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <mach/mach.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <thread>

using namespace tve;
using namespace std::chrono_literals;

namespace {

constexpr Ticks kSec = kTicksPerSecond;
// The most the process may hold (its memory footprint, as Activity Monitor shows it) while
// playing three 4K layers or exporting: decoded 4K frames are 12 MB each, and the playback
// queue, reversed-clip windows and the GPU's textures hold a few hundred of them at most.
constexpr double kMemoryBudgetMB = 1500.0;

std::string sample(const char* name)
{
    return (std::filesystem::path(TVE_SAMPLES_DIR) / name).string();
}

bool haveSamples()
{
    return std::filesystem::exists(sample("4k30_h264.mp4")) && std::filesystem::exists(sample("test_1080p30_h264.mp4"));
}

QTemporaryDir& scratch()
{
    static QTemporaryDir dir;
    return dir;
}

double footprintMB()
{
    task_vm_info_data_t info{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS)
        return 0.0;
    return double(info.phys_footprint) / (1024.0 * 1024.0);
}

// The process's largest footprint while it lives, sampled every 20 ms.
class PeakMemory {
public:
    PeakMemory()
        : m_thread([this] {
              while (!m_stop) {
                  m_peak = std::max(m_peak.load(), footprintMB());
                  std::this_thread::sleep_for(20ms);
              }
          })
    {
    }
    ~PeakMemory() { stop(); }
    double stop()
    {
        m_stop = true;
        if (m_thread.joinable())
            m_thread.join();
        return m_peak;
    }

private:
    std::atomic<bool> m_stop{false};
    std::atomic<double> m_peak{0.0};
    std::thread m_thread;
};

// A project, built with the editor's own commands.
struct Builder {
    std::shared_ptr<Project> project = std::make_shared<Project>();
    CommandStack stack{*project};

    explicit Builder(int width, int height, FrameRate rate = {30, 1})
    {
        project->settings.width = width;
        project->settings.height = height;
        project->settings.frameRate = rate;
    }
    template <typename C> C* push(std::unique_ptr<C> command)
    {
        C* raw = command.get();
        REQUIRE_FALSE(stack.push(std::move(command)));
        return raw;
    }
    Id track(TrackKind kind = TrackKind::Video)
    {
        return push(std::make_unique<AddTrackCommand>(kind, "Track"))->trackId();
    }
    Id media(const std::string& path, int width, int height, Ticks duration, const std::string& proxy = {})
    {
        MediaItem item{.path = path, .duration = duration};
        item.width = width;
        item.height = height;
        item.proxyPath = proxy;
        return push(std::make_unique<ImportMediaCommand>(std::move(item)))->mediaId();
    }
    Id clip(Id track, Id media, Ticks start, Ticks duration, Ticks in = 0, ClipVisual visual = {})
    {
        return push(std::make_unique<AddClipCommand>(track, media, start, duration, in, visual))->clipId();
    }
};

// The preview's work, headless: FrameCompositor on a Metal device of its own, each frame
// drawn and waited for (stricter than the preview, which doesn't wait).
class Renderer {
public:
    Renderer()
    {
        QRhiMetalInitParams params;
        m_rhi.reset(QRhi::create(QRhi::Metal, &params));
        REQUIRE(m_rhi);
        m_compositor = std::make_unique<FrameCompositor>(m_rhi.get());
        REQUIRE(m_compositor->isValid());
    }
    ~Renderer() { m_compositor.reset(); }
    void draw(CompositeFrame& frame, QSize canvas)
    {
        @autoreleasepool {
            drawInPool(frame, canvas);
        }
    }
    void drawInPool(CompositeFrame& frame, QSize canvas)
    {
        QRhiCommandBuffer* cb = nullptr;
        REQUIRE(m_rhi->beginOffscreenFrame(&cb) == QRhi::FrameOpSuccess);
        std::vector<CVMetalTextureRef> retained;
        m_compositor->render(cb, frame, canvas, float(toSeconds(frame.time)), nullptr, retained);
        REQUIRE(m_rhi->endOffscreenFrame() == QRhi::FrameOpSuccess);
        for (CVMetalTextureRef texture : retained)
            CFRelease(texture);
        m_compositor->flushTextureCache();
    }

private:
    std::unique_ptr<QRhi> m_rhi;
    std::unique_ptr<FrameCompositor> m_compositor;
};

// Waits for the first frame of a restart (at or after `time`) and draws it.
double drawFirstFrame(TimelinePlayback& playback, Renderer& renderer, Ticks time, QSize canvas)
{
    const auto begin = std::chrono::steady_clock::now();
    playback.seek(time);
    for (;;) {
        if (auto front = playback.queue().frontPts()) {
            int skipped = 0;
            auto frame = playback.queue().takeLatestDue(std::max(*front, time), skipped);
            renderer.draw(*frame, canvas);
            break;
        }
        REQUIRE(std::chrono::steady_clock::now() - begin < 5s);
        std::this_thread::sleep_for(200us);
    }
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
}

// Plays `seconds` of the timeline from 0 as the preview does on a 60 Hz display (each vsync,
// the frame due, drawn); the frames drawn and dropped.
std::pair<std::int64_t, std::int64_t> present(TimelinePlayback& playback, Renderer& renderer, QSize canvas, double seconds)
{
    drawFirstFrame(playback, renderer, 0, canvas); // the playhead's frame is up before play
    playback.play();
    const auto vsync = std::chrono::microseconds(16667);
    std::int64_t shown = 0, skipped = 0;
    for (auto next = std::chrono::steady_clock::now() + vsync; playback.clock().now() < Ticks(seconds * kSec); next += vsync) {
        std::this_thread::sleep_until(next);
        int late = 0;
        if (auto frame = playback.queue().takeLatestDue(playback.clock().now(), late)) {
            renderer.draw(*frame, canvas);
            ++shown;
        }
        skipped += late;
    }
    playback.pause();
    return {shown, skipped};
}

double percentile(std::vector<double> values, double p)
{
    std::sort(values.begin(), values.end());
    return values[std::min(values.size() - 1, std::size_t(p * double(values.size())))];
}

} // namespace

TEST_CASE("playback: 4K30, three layers and two effects at half resolution keep real time", "[perf]")
{
    if (!haveSamples())
        SKIP("no samples: run tools/make_test_media.sh");
    Builder b(3840, 2160);
    const Id media = b.media(sample("4k30_h264.mp4"), 3840, 2160, 20 * kSec);
    // The full picture, a picture in picture and a smaller one over it: three 4K decodes.
    const Id bottom = b.clip(b.track(), media, 0, 12 * kSec);
    ClipVisual inset;
    inset.transform.scale = 0.5;
    inset.transform.x = 0.2;
    const Id middle = b.clip(b.track(), media, 0, 12 * kSec, 2 * kSec, inset);
    ClipVisual corner;
    corner.transform.scale = 0.3;
    corner.transform.x = -0.3;
    corner.transform.y = -0.3;
    b.clip(b.track(), media, 0, 12 * kSec, 5 * kSec, corner);
    b.push(std::make_unique<SetClipEffectsCommand>(bottom, std::vector<ClipEffect>{{"tve.color.adjust", true, {{"saturation", {0.4f}}}}}, "Effects"));
    b.push(std::make_unique<SetClipEffectsCommand>(middle, std::vector<ClipEffect>{{"tve.blur.gaussian", true, {}}}, "Effects"));

    Renderer renderer;
    const QSize half(1920, 1080);
    PeakMemory memory;
    TimelinePlayback playback;
    playback.setProject(b.project);
    drawFirstFrame(playback, renderer, 0, half); // the playhead's frame is up before play
    // Presenting as the preview does on a 60 Hz display: each vsync, the frame due.
    playback.play();
    const auto begin = std::chrono::steady_clock::now();
    const auto vsync = std::chrono::microseconds(16667);
    std::int64_t shown = 0, skipped = 0;
    for (auto next = begin + vsync; playback.clock().now() < 10 * kSec; next += vsync) {
        std::this_thread::sleep_until(next);
        int late = 0;
        if (auto frame = playback.queue().takeLatestDue(playback.clock().now(), late)) {
            renderer.draw(*frame, half);
            ++shown;
        }
        skipped += late;
    }
    playback.pause();
    const double peak = memory.stop();
    WARN("10 s of 4K30 × 3 layers + 2 effects at 1920×1080: " << shown << " frames drawn, " << skipped
                                                               << " dropped; peak memory " << peak << " MB");
    CHECK(shown >= 295);
    CHECK(skipped <= 1);
    CHECK(peak < kMemoryBudgetMB);
}

// Not run by default (`tve_perf_tests "[stress]"`): how many 4K30 layers, each with an
// effect, play in real time at full and half resolution — where a stretch is heavy enough
// to render (see RenderCost).
TEST_CASE("playback stress: 4K30 layers until frames drop", "[.][stress]")
{
    if (!haveSamples())
        SKIP("no samples: run tools/make_test_media.sh");
    const auto env = qEnvironmentVariable("TVE_STRESS_EFFECT"); // "", tve.color.adjust, tve.blur.gaussian…
    const std::string effect = env.toStdString();
    for (const bool halfSize : {true, false}) {
        for (int layers = 4; layers <= 12; layers += 2) {
            Builder b(3840, 2160);
            const Id media = b.media(sample("4k30_h264.mp4"), 3840, 2160, 20 * kSec);
            for (int i = 0; i < layers; ++i) {
                ClipVisual v;
                v.transform.scale = i == 0 ? 1.0 : 0.6 - 0.04 * i;
                v.transform.x = i == 0 ? 0.0 : -0.3 + 0.05 * i;
                const Id clip = b.clip(b.track(), media, 0, 6 * kSec, Ticks(i % 10) * kSec, v);
                if (!effect.empty())
                    b.push(std::make_unique<SetClipEffectsCommand>(clip, std::vector<ClipEffect>{{effect, true, {}}}, "Effects"));
            }
            Renderer renderer;
            TimelinePlayback playback;
            playback.setProject(b.project);
            const auto [shown, skipped] = present(playback, renderer, halfSize ? QSize(1920, 1080) : QSize(3840, 2160), 5.0);
            WARN((halfSize ? "half" : "full") << " size, " << layers << " layers, effect '" << effect << "': " << shown
                                              << " of 150 drawn, " << skipped << " dropped; load "
                                              << playbackLoad(*b.project)[0].load);
        }
    }
}

TEST_CASE("render files: a stretch too heavy for real time plays smoothly once rendered", "[perf]")
{
    if (!haveSamples())
        SKIP("no samples: run tools/make_test_media.sh");
    // Eight 4K30 layers for 4 s, each blurred: well over what plays in real time.
    Builder b(3840, 2160);
    const Id media = b.media(sample("4k30_h264.mp4"), 3840, 2160, 20 * kSec);
    for (int i = 0; i < 8; ++i) {
        ClipVisual v;
        v.transform.scale = i == 0 ? 1.0 : 0.6 - 0.05 * i;
        v.transform.x = i == 0 ? 0.0 : -0.3 + 0.08 * i;
        const Id clip = b.clip(b.track(), media, 0, 4 * kSec, Ticks(i) * kSec, v);
        b.push(std::make_unique<SetClipEffectsCommand>(clip, std::vector<ClipEffect>{{"tve.blur.gaussian", true, {}}}, "Effects"));
    }
    const auto heavy = heavyStretches(*b.project, kRealTimeLoad, [](const ClipEffect&) { return 0.22; });
    REQUIRE(heavy.size() == 1);
    CHECK(heavy[0].load > kRealTimeLoad);
    Renderer renderer;
    const QSize canvas(1920, 1080);
    {
        TimelinePlayback playback;
        playback.setProject(b.project);
        const auto [shown, skipped] = present(playback, renderer, canvas, 4.0);
        WARN("8 × 4K30 layers blurred, composited: " << shown << " of 120 frames drawn");
        CHECK(shown < 110); // heavy indeed
    }
    QElapsedTimer timer;
    timer.start();
    QString error;
    REQUIRE(RenderCache::render(b.project, heavy[0].from, heavy[0].to, {}, &error));
    const double renderSeconds = double(timer.elapsed()) / 1000.0;
    const auto rendered = RenderCache().validFor(*b.project);
    REQUIRE(rendered.size() == 1);
    TimelinePlayback playback;
    playback.setProject(b.project, rendered);
    const auto [shown, skipped] = present(playback, renderer, canvas, 4.0);
    WARN("rendered in " << renderSeconds << " s; then played from the render file: " << shown << " of 120 frames drawn, "
                        << skipped << " dropped");
    CHECK(shown >= 118);
    CHECK(skipped <= 1);
}

// Not run by default (`tve_perf_tests "[leak]"`): what playing and rendering leave behind.
TEST_CASE("memory left behind after playing, rendering and exporting", "[.][leak]")
{
    if (!haveSamples())
        SKIP("no samples: run tools/make_test_media.sh");
    WARN("at the start: " << footprintMB() << " MB");
    for (int round = 0; round < 3; ++round) {
        {
            Builder b(3840, 2160);
            const Id media = b.media(sample("4k30_h264.mp4"), 3840, 2160, 20 * kSec);
            for (int i = 0; i < 3; ++i)
                b.clip(b.track(), media, 0, 4 * kSec, Ticks(i) * kSec);
            Renderer renderer;
            TimelinePlayback playback;
            playback.setProject(b.project);
            present(playback, renderer, {1920, 1080}, 3.0);
        }
        WARN("after playing, round " << round << ": " << footprintMB() << " MB");
    }
    for (int round = 0; round < 2; ++round) {
        {
            Builder b(1920, 1080);
            const Id media = b.media(sample("test_1080p30_h264.mp4"), 1920, 1080, 10 * kSec);
            b.clip(b.track(), media, 0, 5 * kSec);
            Exporter::Options options;
            const QString path = QDir(scratch().path()).filePath(QStringLiteral("leak.mp4"));
            REQUIRE(Exporter::run(b.project, path, options, {}).ok);
        }
        WARN("after exporting, round " << round << ": " << footprintMB() << " MB");
    }
}

TEST_CASE("scrubbing: a seek to the frame drawn, with and without proxies", "[perf]")
{
    if (!haveSamples())
        SKIP("no samples: run tools/make_test_media.sh");
    const std::string proxy = QDir(scratch().path()).filePath(QStringLiteral("4k30_proxy.mov")).toStdString();
    if (!std::filesystem::exists(proxy)) {
        std::string error;
        REQUIRE(makeProxy(sample("4k30_h264.mp4"), proxy, ProxyOptions{}, {}, &error));
    }
    for (const bool proxies : {true, false}) {
        Builder b(3840, 2160);
        const Id media = b.media(sample("4k30_h264.mp4"), 3840, 2160, 20 * kSec, proxies ? proxy : std::string());
        b.clip(b.track(), media, 0, 20 * kSec);
        Renderer renderer;
        TimelinePlayback playback;
        playback.setProject(b.project);
        drawFirstFrame(playback, renderer, 0, {1920, 1080});
        // Here and there, as a hand on the playhead goes: 40 places across the clip.
        std::vector<double> latencies;
        std::uint32_t seed = 7;
        for (int i = 0; i < 40; ++i) {
            seed = seed * 1664525u + 1013904223u;
            const Ticks at = Ticks(seed % 19000) * kSec / 1000;
            latencies.push_back(drawFirstFrame(playback, renderer, at, {1920, 1080}));
        }
        const double typical = percentile(latencies, 0.5);
        const double slow = percentile(latencies, 0.9);
        WARN((proxies ? "with proxies" : "without proxies") << ": median " << typical << " ms, 90th percentile " << slow
                                                             << " ms, slowest " << percentile(latencies, 1.0) << " ms");
        CHECK(slow < (proxies ? 80.0 : 150.0));
    }
}

TEST_CASE("start-up: the app launched to its Home screen drawn", "[perf]")
{
    QProcess app;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    // Everything it keeps, in scratch folders: this Mac's own settings and data untouched.
    const QString root = scratch().path() + QStringLiteral("/app");
    QDir().mkpath(root);
    for (const char* name : {"TVE_AUTOSAVE_DIR", "TVE_PROXY_DIR", "TVE_RECORDING_DIR", "TVE_VOICE_DIR", "TVE_MODEL_DIR",
                             "TVE_GENERATED_DIR", "TVE_IMPORT_DIR", "TVE_PROCESSED_DIR", "TVE_ENHANCED_DIR",
                             "TVE_ANALYSIS_DIR", "TVE_COLOR_PRESET_DIR", "TVE_TITLE_PRESET_DIR", "TVE_OVERLAY_CACHE_DIR", "TVE_SOUND_CACHE_DIR",
                             "TVE_SOUND_PACK_DIR", "TVE_TEMPLATE_DIR", "TVE_TEMPLATE_CACHE_DIR", "TVE_PACKS_DIR",
                             "TVE_CONSENT_DIR", "TVE_KEY_STORE", "TVE_RENDER_CACHE_DIR", "TVE_LIVE_PHOTO_LIBRARY"}) {
        const QString dir = root + QLatin1Char('/') + QString::fromLatin1(name);
        QDir().mkpath(dir);
        env.insert(QString::fromLatin1(name), dir);
    }
    for (const char* name : {"TVE_LANGUAGE_FILE", "TVE_KEYMAP_FILE", "TVE_CLOUD_SETTINGS_FILE", "TVE_STORE_SETTINGS_FILE",
                             "TVE_MUSIC_FOLDERS_FILE", "TVE_TEMPLATE_FAVORITES_FILE", "TVE_PLUGIN_CACHE_FILE", "TVE_TELEPROMPTER_FILE"})
        env.insert(QString::fromLatin1(name), root + QLatin1Char('/') + QString::fromLatin1(name) + QStringLiteral(".json"));
    env.insert(QStringLiteral("TVE_AUDIO"), QStringLiteral("null"));
    env.insert(QStringLiteral("TVE_CLOUD_MOCK"), QStringLiteral("1"));
    app.setProcessEnvironment(env);
    // The second launch is the one measured: the first fills the system's caches, as an
    // app opened before has them.
    double total = 0;
    QString line;
    for (int run = 0; run < 2; ++run) {
        QElapsedTimer timer;
        timer.start();
        app.start(QStringLiteral(TVE_APP_PATH), {QStringLiteral("--startup-probe")});
        REQUIRE(app.waitForStarted(10000));
        REQUIRE(app.waitForFinished(30000));
        total = double(timer.elapsed());
        line = QString::fromUtf8(app.readAllStandardOutput()).trimmed();
    }
    REQUIRE(line.startsWith(QStringLiteral("startup ")));
    WARN("launch to the Home screen drawn: " << total << " ms (in the app: " << line.mid(8).toStdString() << " ms)");
    CHECK(total < 2500.0);
}

TEST_CASE("opening: a 200-clip project, to its first frame ready", "[perf]")
{
    if (!haveSamples())
        SKIP("no samples: run tools/make_test_media.sh");
    // 200 clips of a dozen files on three tracks, with titles and dissolves.
    Builder b(1920, 1080);
    const char* files[] = {"test_1080p30_h264.mp4", "test_av_1080p30.mp4", "gradients_1080p30_h264.mp4",
                           "test_hevc10_1080p30.mp4", "people.mp4", "shaky.mp4", "flicker.mp4", "card.mp4",
                           "test_720p30_mjpeg.mov", "4k30_h264.mp4", "sync_b.mov", "test_alpha_prores.mov"};
    std::vector<Id> media;
    for (const char* file : files)
        media.push_back(b.media(sample(file), 1920, 1080, 2 * kSec));
    const Id main = b.track();
    const Id over = b.track();
    const Id titles = b.track();
    for (int i = 0; i < 120; ++i)
        b.clip(main, media[std::size_t(i) % media.size()], Ticks(i) * kSec, kSec, 0);
    for (int i = 0; i < 50; ++i) {
        ClipVisual inset;
        inset.transform.scale = 0.4;
        b.clip(over, media[std::size_t(i + 3) % media.size()], Ticks(i) * 2 * kSec, kSec, 0, inset);
    }
    for (int i = 0; i < 30; ++i) {
        ClipText text;
        text.text = "Part " + std::to_string(i + 1);
        b.push(std::make_unique<AddTitleCommand>(titles, Ticks(i) * 4 * kSec, 2 * kSec, text));
    }
    const QString path = QDir(scratch().path()).filePath(QStringLiteral("two-hundred.tve"));
    saveProject(*b.project, path.toStdString());

    PeakMemory memory;
    ProjectController controller;
    controller.setAutosaveEnabled(false);
    controller.setAutoProxies(false);
    QElapsedTimer timer;
    timer.start();
    REQUIRE(controller.open(QUrl::fromLocalFile(path)));
    const double opened = double(timer.elapsed());
    // Its first frame: what the player shows next.
    while (!controller.playback()->queue().frontPts()) {
        QCoreApplication::processEvents();
        REQUIRE(timer.elapsed() < 10000);
        std::this_thread::sleep_for(1ms);
    }
    const double ready = double(timer.elapsed());
    std::size_t clips = 0;
    for (const Track& track : controller.project().tracks)
        clips += track.clips.size();
    REQUIRE(clips == 200);
    WARN("200 clips: opened in " << opened << " ms, first frame ready at " << ready << " ms; peak memory "
                                 << memory.stop() << " MB");
    CHECK(ready < 3000.0);
}

TEST_CASE("export: 1080p H.264 at twice real time or faster", "[perf]")
{
    if (!haveSamples())
        SKIP("no samples: run tools/make_test_media.sh");
    // A minute of cuts every 4 s with dissolves, a picture in picture and titles over it.
    Builder b(1920, 1080);
    const Id a = b.media(sample("test_1080p30_h264.mp4"), 1920, 1080, 10 * kSec);
    const Id c = b.media(sample("gradients_1080p30_h264.mp4"), 1920, 1080, 10 * kSec);
    const Id main = b.track();
    const Id over = b.track();
    for (int i = 0; i < 15; ++i)
        b.clip(main, i % 2 ? c : a, Ticks(i) * 4 * kSec, 4 * kSec, Ticks(i % 3) * kSec);
    ClipVisual inset;
    inset.transform.scale = 0.35;
    inset.transform.x = 0.3;
    for (int i = 0; i < 6; ++i)
        b.clip(over, i % 2 ? a : c, Ticks(i) * 10 * kSec + 2 * kSec, 5 * kSec, 0, inset);
    const Id titles = b.track();
    for (int i = 0; i < 6; ++i) {
        ClipText text;
        text.text = "Chapter " + std::to_string(i + 1);
        b.push(std::make_unique<AddTitleCommand>(titles, Ticks(i) * 10 * kSec, 3 * kSec, text));
    }

    Exporter::Options options;
    options.settings.width = 1920;
    options.settings.height = 1080;
    options.settings.frameRate = {30, 1};
    const QString path = QDir(scratch().path()).filePath(QStringLiteral("export.mp4"));
    PeakMemory memory;
    QElapsedTimer timer;
    timer.start();
    const Exporter::Result result = Exporter::run(b.project, path, options, {});
    const double seconds = double(timer.elapsed()) / 1000.0;
    REQUIRE(result.ok);
    const double speed = toSeconds(b.project->duration()) / seconds;
    WARN("60 s of 1080p30 exported in " << seconds << " s: " << speed << "× real time; peak memory " << memory.stop()
                                        << " MB");
    CHECK(speed >= 2.0);
}

int main(int argc, char** argv)
{
    // App data in a scratch folder (QStandardPaths' test locations) and no sound device.
    QStandardPaths::setTestModeEnabled(true);
    qputenv("TVE_AUDIO", "null");
    qputenv("TVE_CLOUD_MOCK", "1");
    qputenv("TVE_KEY_STORE", (scratch().path() + QStringLiteral("/keys")).toLocal8Bit());
    qputenv("TVE_AUTOSAVE_DIR", (scratch().path() + QStringLiteral("/autosave")).toLocal8Bit());
    qputenv("TVE_PROXY_DIR", (scratch().path() + QStringLiteral("/proxies")).toLocal8Bit());
    qputenv("TVE_PLUGIN_CACHE_FILE", (scratch().path() + QStringLiteral("/plugins.json")).toLocal8Bit());
    qputenv("TVE_LANGUAGE_FILE", (scratch().path() + QStringLiteral("/language.json")).toLocal8Bit());
    qputenv("TVE_RENDER_CACHE_DIR", (scratch().path() + QStringLiteral("/render")).toLocal8Bit());
    QGuiApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}
