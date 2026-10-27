#include "audio/AudioOutput.h"
#include "commands/CommandStack.h"
#include "commands/EditCommands.h"
#include "decode/AudioDecoder.h"
#include "playback/PlaybackClock.h"
#include "playback/TimelineAudio.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <numbers>
#include <thread>

using namespace tve;

namespace {

constexpr Ticks kSec = kTicksPerSecond;
constexpr float kQuantization = 2.0f / 32768.0f; // 16-bit source files

std::string sample(const char* name)
{
    return (std::filesystem::path(TVE_SAMPLES_DIR) / name).string();
}

void requireSamples()
{
    for (const char* name : {"test_tone_440.wav", "test_clicks.wav", "test_av_1080p30.mp4"}) {
        if (!std::filesystem::exists(sample(name)))
            SKIP("sample missing: " << name << " (run tools/make_test_media.sh)");
    }
}

float tone(std::int64_t sampleIndex)
{
    return 0.5f * float(std::sin(2.0 * std::numbers::pi * 440.0 * double(sampleIndex) / audio::kSampleRate));
}

// First sample index whose left channel is louder than `threshold`, or -1.
std::int64_t firstLoud(const std::vector<float>& stereo, float threshold = 0.1f)
{
    for (std::size_t i = 0; i < stereo.size() / 2; ++i) {
        if (std::abs(stereo[i * 2]) > threshold)
            return std::int64_t(i);
    }
    return -1;
}

float peak(const std::vector<float>& stereo, std::size_t from, std::size_t to)
{
    float p = 0.0f;
    for (std::size_t i = from; i < std::min(to, stereo.size() / 2); ++i)
        p = std::max(p, std::abs(stereo[i * 2]));
    return p;
}

// Collects every chunk the mixer produces, as one buffer starting at the first chunk.
std::vector<float> drainMix(TimelineAudio& mixer)
{
    std::vector<float> all;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (std::chrono::steady_clock::now() < deadline) {
        if (auto chunk = mixer.queue().tryPop()) {
            all.insert(all.end(), chunk->samples.begin(), chunk->samples.end());
            continue;
        }
        if (mixer.producerFinished() && mixer.queue().size() == 0)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return all;
}

struct AudioProject {
    std::shared_ptr<Project> project = std::make_shared<Project>();
    CommandStack stack{*project};
    Id videoTrack = kNoId;
    Id audioTrack = kNoId;

    AudioProject()
    {
        videoTrack = push<AddTrackCommand>(TrackKind::Video, "V1").trackId();
        audioTrack = push<AddTrackCommand>(TrackKind::Audio, "A1").trackId();
    }

    template <typename T, typename... Args>
    T& push(Args&&... args)
    {
        auto command = std::make_unique<T>(std::forward<Args>(args)...);
        T& ref = *command;
        auto error = stack.push(std::move(command));
        INFO(error.value_or(""));
        REQUIRE_FALSE(error);
        return ref;
    }

    Id importAudio(const char* name, Ticks duration, bool hasVideo = false)
    {
        return push<ImportMediaCommand>(
                   MediaItem{.path = sample(name), .duration = duration, .hasVideo = hasVideo, .hasAudio = true})
            .mediaId();
    }
};

} // namespace

TEST_CASE("audio decoder produces the exact tone in the engine format")
{
    requireSamples();
    auto decoder = AudioDecoder::open(sample("test_tone_440.wav"));
    REQUIRE(decoder);
    CHECK(decoder->info().sourceSampleRate == 48000);
    CHECK(decoder->info().sourceChannels == 2);

    std::vector<float> buffer(4800 * 2);
    REQUIRE(decoder->read(buffer.data(), 4800) == 4800);
    for (int i = 0; i < 4800; i += 37) {
        INFO("sample " << i);
        CHECK(buffer[std::size_t(i) * 2] == Catch::Approx(tone(i)).margin(kQuantization));
        CHECK(buffer[std::size_t(i) * 2 + 1] == Catch::Approx(tone(i)).margin(kQuantization));
    }
    CHECK(decoder->position() == audio::ticksFromSamples(4800));
}

TEST_CASE("audio seek is sample accurate")
{
    requireSamples();
    auto decoder = AudioDecoder::open(sample("test_tone_440.wav"));
    REQUIRE(decoder);
    const std::int64_t target = 60001; // 1.25002 s, deliberately off any packet boundary
    REQUIRE(decoder->seek(audio::ticksFromSamples(target)));
    std::vector<float> buffer(100 * 2);
    REQUIRE(decoder->read(buffer.data(), 100) == 100);
    for (int i = 0; i < 100; i += 9)
        CHECK(buffer[std::size_t(i) * 2] == Catch::Approx(tone(target + i)).margin(kQuantization));
}

TEST_CASE("click markers land on their exact sample")
{
    requireSamples();
    auto decoder = AudioDecoder::open(sample("test_clicks.wav"));
    REQUIRE(decoder);
    std::vector<float> all(3 * 48000 * 2);
    REQUIRE(decoder->read(all.data(), 3 * 48000) == 3 * 48000);
    CHECK(firstLoud(all) == 24000); // 0.5 s
}

TEST_CASE("AAC sound from a video file decodes")
{
    requireSamples();
    auto decoder = AudioDecoder::open(sample("test_av_1080p30.mp4"));
    REQUIRE(decoder);
    CHECK(decoder->info().codecName == "aac");
    std::vector<float> buffer(48000 * 2);
    REQUIRE(decoder->read(buffer.data(), 48000) == 48000);
    CHECK(peak(buffer, 4800, 48000) == Catch::Approx(0.5f).margin(0.05));
}

TEST_CASE("mixer places clips at their timeline position")
{
    requireSamples();
    AudioProject p;
    const Id clicks = p.importAudio("test_clicks.wav", 3 * kSec);
    // Clicks at source 0.5 s and 1.5 s; the clip starts at timeline 1 s with in-point 0.25 s,
    // so they are heard at 1.25 s and 2.25 s.
    p.push<AddClipCommand>(p.audioTrack, clicks, kSec, 2 * kSec, kSec / 4);

    TimelineAudio mixer;
    mixer.restart(p.project, 0);
    const auto mix = drainMix(mixer);
    CHECK(firstLoud(mix) == 60000); // 1.25 s × 48 000
    CHECK(peak(mix, 0, 60000) == 0.0f);
    CHECK(peak(mix, 60000, 60480) == Catch::Approx(0.9f).margin(0.01));
    CHECK(peak(mix, 108000, 108480) == Catch::Approx(0.9f).margin(0.01)); // 2.25 s
}

TEST_CASE("mixer applies volume, fades and mute")
{
    requireSamples();
    AudioProject p;
    const Id toneMedia = p.importAudio("test_tone_440.wav", 3 * kSec);
    const Id clip = p.push<AddClipCommand>(p.audioTrack, toneMedia, 0, 2 * kSec).clipId();
    p.push<SetClipAudioCommand>(clip, ClipAudio{-6.0206f, kSec, 0}); // half amplitude, 1 s fade in

    TimelineAudio mixer;
    mixer.restart(p.project, 0);
    auto mix = drainMix(mixer);
    CHECK(peak(mix, 0, 480) < 0.01f);                                          // fade starts at silence
    CHECK(peak(mix, 23500, 24500) == Catch::Approx(0.125f).margin(0.01));       // halfway through the fade
    CHECK(peak(mix, 60000, 61000) == Catch::Approx(0.25f).margin(kQuantization * 2)); // after the fade

    p.push<SetTrackFlagCommand>(p.audioTrack, SetTrackFlagCommand::Flag::Muted, true);
    mixer.restart(p.project, 0);
    mix = drainMix(mixer);
    CHECK(peak(mix, 0, mix.size() / 2) == 0.0f);
}

TEST_CASE("mixer plays video clips' sound until it is detached")
{
    requireSamples();
    AudioProject p;
    const Id av = p.importAudio("test_av_1080p30.mp4", 2 * kSec, true);
    const Id clip = p.push<AddClipCommand>(p.videoTrack, av, 0, 2 * kSec).clipId();

    TimelineAudio mixer;
    mixer.restart(p.project, 0);
    CHECK(peak(drainMix(mixer), 4800, 48000) == Catch::Approx(0.5f).margin(0.05));

    // Detached: the video clip is silent, the audio clip plays the same sound once.
    p.push<DetachAudioCommand>(clip, p.audioTrack);
    mixer.restart(p.project, 0);
    CHECK(peak(drainMix(mixer), 4800, 48000) == Catch::Approx(0.5f).margin(0.05));

    p.push<SetTrackFlagCommand>(p.audioTrack, SetTrackFlagCommand::Flag::Muted, true);
    mixer.restart(p.project, 0);
    CHECK(peak(drainMix(mixer), 0, 96000) == 0.0f);
}

TEST_CASE("restarting mid-timeline mixes from that sample")
{
    requireSamples();
    AudioProject p;
    const Id clicks = p.importAudio("test_clicks.wav", 3 * kSec);
    p.push<AddClipCommand>(p.audioTrack, clicks, 0, 3 * kSec);

    TimelineAudio mixer;
    mixer.restart(p.project, kSec + kSec / 10); // 1.1 s: next click at 1.5 s
    const auto mix = drainMix(mixer);
    CHECK(firstLoud(mix) == 19200); // 0.4 s after the restart point
}

TEST_CASE("output follows the clock and is silent when paused")
{
    requireSamples();
    AudioProject p;
    const Id clicks = p.importAudio("test_clicks.wav", 3 * kSec);
    p.push<AddClipCommand>(p.audioTrack, clicks, 0, 3 * kSec);

    auto mixer = std::make_shared<TimelineAudio>();
    PlaybackClock clock;
    clock.seek(kSec + 4 * kSec / 10); // 1.4 s: click at 1.5 s
    mixer->restart(p.project, clock.now());
    std::this_thread::sleep_for(std::chrono::milliseconds(50)); // let the mixer fill its queue

    AudioOutput output(AudioOutput::Backend::None);
    output.setSource(mixer, &clock);
    std::vector<float> out(9600 * 2);

    output.render(out.data(), 9600); // paused
    CHECK(peak(out, 0, 9600) == 0.0f);

    clock.play();
    output.render(out.data(), 9600);
    // The click is 0.1 s (4 800 samples) ahead; allow for the time between play() and render().
    const auto onset = firstLoud(out);
    CHECK(onset > 4700);
    CHECK(onset <= 4800);
    CHECK(output.peakLeft() == Catch::Approx(0.9f).margin(0.01));
    CHECK(output.underruns() == 0);
}

#include "audio/WaveformCache.h"

TEST_CASE("waveform peaks follow the sound")
{
    requireSamples();
    const WaveformPeaks peaks = WaveformCache::compute(sample("test_clicks.wav"));
    REQUIRE(peaks.peaks.size() == 300); // 3 s at 100 peaks per second
    CHECK(peaks.maxIn(0, kSec / 2) == 0.0f);
    CHECK(peaks.maxIn(kSec / 2, kSec / 2 + kSec / 100) == Catch::Approx(0.9f).margin(0.01));
    CHECK(peaks.maxIn(kSec, 3 * kSec / 2) == 0.0f);

    // The background cache delivers the same result and calls back once.
    std::mutex mutex;
    std::condition_variable ready;
    int calls = 0;
    WaveformCache cache([&](const std::string&) {
        std::lock_guard lock(mutex);
        ++calls;
        ready.notify_all();
    });
    CHECK_FALSE(cache.peaks(sample("test_clicks.wav")));
    std::unique_lock lock(mutex);
    REQUIRE(ready.wait_for(lock, std::chrono::seconds(10), [&] { return calls == 1; }));
    lock.unlock();
    REQUIRE(cache.peaks(sample("test_clicks.wav")));
    CHECK(cache.peaks(sample("test_clicks.wav"))->peaks == peaks.peaks);
}

namespace {

int zeroCrossings(const std::vector<float>& stereo, std::size_t from, std::size_t to)
{
    int crossings = 0;
    for (std::size_t i = from + 1; i < std::min(to, stereo.size() / 2); ++i) {
        if ((stereo[(i - 1) * 2] < 0.0f) != (stereo[i * 2] < 0.0f))
            ++crossings;
    }
    return crossings;
}

} // namespace

TEST_CASE("double speed raises a tone by an octave")
{
    requireSamples();
    AudioProject p;
    const Id toneMedia = p.importAudio("test_tone_440.wav", 3 * kSec);
    const Id clip = p.push<AddClipCommand>(p.audioTrack, toneMedia, 0, 3 * kSec).clipId();
    p.push<SetClipSpeedCommand>(clip, 2.0, false); // now 1.5 s long

    TimelineAudio mixer;
    mixer.restart(p.project, 0);
    const auto mix = drainMix(mixer);
    // 880 Hz → 1760 zero crossings per second; measure over 1 s.
    CHECK(zeroCrossings(mix, 4800, 52800) == Catch::Approx(1760).margin(6));
    CHECK(peak(mix, 4800, 52800) == Catch::Approx(0.5f).margin(0.01));
    CHECK(peak(mix, 74000, 96000) == 0.0f); // ends at 1.5 s
}

TEST_CASE("reversed audio plays the clicks backwards")
{
    requireSamples();
    AudioProject p;
    const Id clicks = p.importAudio("test_clicks.wav", 3 * kSec);
    const Id clip = p.push<AddClipCommand>(p.audioTrack, clicks, 0, 3 * kSec).clipId();
    p.push<SetClipSpeedCommand>(clip, 1.0, true);

    TimelineAudio mixer;
    mixer.restart(p.project, 0);
    const auto mix = drainMix(mixer);
    // Source bursts at [2.5, 2.51) s play at timeline [0.49, 0.5): first loud sample ≈ 23 520.
    CHECK(std::llabs(firstLoud(mix) - 23520) <= 2);
    CHECK(peak(mix, 0, 23400) == 0.0f);
}

TEST_CASE("freeze frames are silent")
{
    requireSamples();
    AudioProject p;
    const Id av = p.importAudio("test_av_1080p30.mp4", 2 * kSec, true);
    const Id clip = p.push<AddClipCommand>(p.videoTrack, av, 0, 2 * kSec).clipId();
    p.push<FreezeFrameCommand>(clip, kSec, kSec); // still at 1–2 s, clip resumes at 2–3 s

    TimelineAudio mixer;
    mixer.restart(p.project, 0);
    const auto mix = drainMix(mixer);
    CHECK(peak(mix, 4800, 47000) == Catch::Approx(0.5f).margin(0.05)); // before the still
    CHECK(peak(mix, 49000, 95000) == 0.0f);                            // during the still
    CHECK(peak(mix, 97000, 140000) == Catch::Approx(0.5f).margin(0.05)); // after it
}
