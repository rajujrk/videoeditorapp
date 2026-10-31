#include "speech/Transcriber.h"

#include "decode/AudioDecoder.h"

#include <whisper.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <numbers>
#include <thread>

namespace tve {

namespace {

constexpr int kSpeechRate = 16000;
constexpr int kDecimation = audio::kSampleRate / kSpeechRate; // 3

// A low-pass filter for 48 kHz → 16 kHz: windowed sinc (Blackman), cut-off 7 kHz.
std::vector<float> lowPass()
{
    constexpr int taps = 97;
    std::vector<float> h(taps);
    const double cutoff = 7000.0 / audio::kSampleRate;
    double sum = 0.0;
    for (int i = 0; i < taps; ++i) {
        const double m = i - (taps - 1) / 2.0;
        const double sinc = m == 0 ? 2.0 * cutoff : std::sin(2.0 * std::numbers::pi * cutoff * m) / (std::numbers::pi * m);
        const double window = 0.42 - 0.5 * std::cos(2.0 * std::numbers::pi * i / (taps - 1))
                            + 0.08 * std::cos(4.0 * std::numbers::pi * i / (taps - 1));
        h[std::size_t(i)] = float(sinc * window);
        sum += h[std::size_t(i)];
    }
    for (float& v : h)
        v = float(v / sum);
    return h;
}

// Tokens that are not words: "[MUSIC]", "(laughs)", "♪".
bool annotation(const std::string& word)
{
    return word.find('[') != std::string::npos || word.find(']') != std::string::npos
        || word.find("\xE2\x99\xAA") != std::string::npos || (word.starts_with('(') && word.ends_with(')'))
        || word.starts_with('*');
}

void quiet(ggml_log_level, const char*, void*) {}

// The alignment heads that time words best for a model, from its file name
// ("ggml-base.en-q5_1.bin" → base.en).
whisper_alignment_heads_preset alignmentHeads(const std::string& modelPath)
{
    std::string name = std::filesystem::path(modelPath).stem().string();
    if (name.starts_with("ggml-"))
        name.erase(0, 5);
    if (const auto dash = name.find("-q"); dash != std::string::npos)
        name.erase(dash);
    static const std::pair<const char*, whisper_alignment_heads_preset> known[] = {
        {"tiny.en", WHISPER_AHEADS_TINY_EN}, {"tiny", WHISPER_AHEADS_TINY}, {"base.en", WHISPER_AHEADS_BASE_EN},
        {"base", WHISPER_AHEADS_BASE}, {"small.en", WHISPER_AHEADS_SMALL_EN}, {"small", WHISPER_AHEADS_SMALL},
        {"medium.en", WHISPER_AHEADS_MEDIUM_EN}, {"medium", WHISPER_AHEADS_MEDIUM}, {"large-v1", WHISPER_AHEADS_LARGE_V1},
        {"large-v2", WHISPER_AHEADS_LARGE_V2}, {"large-v3-turbo", WHISPER_AHEADS_LARGE_V3_TURBO},
        {"large-v3", WHISPER_AHEADS_LARGE_V3}};
    for (const auto& [id, preset] : known)
        if (name == id)
            return preset;
    return WHISPER_AHEADS_N_TOP_MOST;
}

// Which 10 ms frames of the 16 kHz sound have sound in them: louder than 30 dB under the
// file's loud parts (and than -50 dBFS).
std::vector<bool> soundFrames(const std::vector<float>& samples)
{
    constexpr std::size_t hop = kSpeechRate / 100;
    std::vector<float> db;
    for (std::size_t i = 0; i < samples.size(); i += hop) {
        double squares = 0.0;
        const std::size_t end = std::min(samples.size(), i + hop);
        for (std::size_t j = i; j < end; ++j)
            squares += double(samples[j]) * samples[j];
        db.push_back(float(10.0 * std::log10(squares / double(end - i) + 1e-12)));
    }
    std::vector<float> sorted = db;
    const auto at = sorted.begin() + std::ptrdiff_t(sorted.size() * 95 / 100);
    std::nth_element(sorted.begin(), at, sorted.end());
    const float threshold = std::max(-50.0f, *at - 30.0f);
    std::vector<bool> on(db.size());
    for (std::size_t i = 0; i < db.size(); ++i)
        on[i] = db[i] >= threshold;
    return on;
}

std::string transcriptPath(const AnalysisCache& cache, const std::string& fingerprint, const std::string& variant)
{
    std::string name = fingerprint + "." + variant;
    for (char& c : name)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.')
            c = '-';
    return (std::filesystem::path(cache.directory()) / (name + ".transcript.json")).string();
}

} // namespace

std::optional<std::vector<float>> speechSamples(const std::string& mediaPath, const AnalysisProgress& progress,
                                                std::string* error)
{
    auto decoder = AudioDecoder::open(mediaPath, error);
    if (!decoder)
        return std::nullopt;
    const double total = std::max(1.0, toSeconds(decoder->info().duration));
    const std::vector<float> h = lowPass();
    const int half = int(h.size() / 2);
    std::vector<float> mono;
    std::vector<float> buffer(std::size_t(audio::kSampleRate) * audio::kChannels);
    for (;;) {
        const int read = decoder->read(buffer.data(), audio::kSampleRate);
        if (read <= 0)
            break;
        for (int i = 0; i < read; ++i)
            mono.push_back(0.5f * (buffer[std::size_t(i) * 2] + buffer[std::size_t(i) * 2 + 1]));
        if (progress && !progress(std::min(1.0, double(mono.size()) / audio::kSampleRate / total)))
            return std::nullopt;
    }
    std::vector<float> out(mono.size() / kDecimation);
    for (std::size_t o = 0; o < out.size(); ++o) {
        const std::int64_t centre = std::int64_t(o) * kDecimation;
        float v = 0.0f;
        for (int k = 0; k < int(h.size()); ++k) {
            const std::int64_t i = centre + k - half;
            if (i >= 0 && i < std::int64_t(mono.size()))
                v += h[std::size_t(k)] * mono[std::size_t(i)];
        }
        out[o] = v;
    }
    return out;
}

std::optional<Transcript> transcribe(const std::string& mediaPath, const std::string& modelPath,
                                     const TranscribeOptions& options, const AnalysisProgress& progress, std::string* error)
{
    const auto fail = [&](const std::string& message) -> std::optional<Transcript> {
        if (error)
            *error = message;
        return std::nullopt;
    };
    // Reading the sound is the first fifth of the work; recognising it the rest.
    const auto samples = speechSamples(mediaPath, [&](double p) { return !progress || progress(0.2 * p); }, error);
    if (!samples)
        return std::nullopt;
    if (samples->size() < kSpeechRate / 10)
        return Transcript{options.language == "auto" ? "" : options.language, {}};

    static std::once_flag silenced;
    std::call_once(silenced, [] {
        whisper_log_set(quiet, nullptr);
        ggml_log_set(quiet, nullptr);
    });
    whisper_context_params contextParams = whisper_context_default_params();
    contextParams.use_gpu = true;
    // Word times by dynamic time warping over the model's alignment heads (closer than the
    // decoder's own timestamps).
    contextParams.dtw_token_timestamps = true;
    contextParams.dtw_aheads_preset = alignmentHeads(modelPath);
    contextParams.dtw_n_top = -1;
    contextParams.flash_attn = false; // DTW needs the attention weights flash attention never keeps
    whisper_context* context = whisper_init_from_file_with_params(modelPath.c_str(), contextParams);
    if (!context)
        return fail("cannot load the speech model " + std::filesystem::path(modelPath).filename().string());

    struct Bridge {
        const AnalysisProgress* progress;
        std::atomic<bool> cancelled{false};
    } bridge{&progress};
    whisper_full_params params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    params.print_realtime = false;
    params.print_progress = false;
    params.print_timestamps = false;
    params.print_special = false;
    params.translate = false;
    params.language = options.language.c_str();
    params.detect_language = false;
    params.n_threads = options.threads > 0 ? options.threads
                                           : std::clamp(int(std::thread::hardware_concurrency()) / 2, 1, 8);
    params.token_timestamps = true;
    params.suppress_nst = true;
    // Written verbatim, fillers and all (they can then be cut): a prompt in that style.
    params.initial_prompt = "Umm, let me think, like, hmm. Okay, uh, here's what I'm thinking.";
    params.progress_callback = [](whisper_context*, whisper_state*, int percent, void* user) {
        auto* b = static_cast<Bridge*>(user);
        if (*b->progress && !(*b->progress)(0.2 + 0.8 * percent / 100.0))
            b->cancelled = true;
    };
    params.progress_callback_user_data = &bridge;
    params.abort_callback = [](void* user) { return static_cast<Bridge*>(user)->cancelled.load(); };
    params.abort_callback_user_data = &bridge;

    const int rc = whisper_full(context, params, samples->data(), int(samples->size()));
    if (bridge.cancelled) {
        whisper_free(context);
        return fail("");
    }
    if (rc != 0) {
        whisper_free(context);
        return fail("speech recognition failed");
    }

    // Tokens into words: a token starting with a space starts a word; the others (parts of
    // words, punctuation) join the word before.
    Transcript transcript;
    transcript.language = whisper_lang_str(whisper_full_lang_id(context));
    const whisper_token eot = whisper_token_eot(context);
    constexpr Ticks centisecond = kTicksPerSecond / 100;
    for (int s = 0; s < whisper_full_n_segments(context); ++s) {
        TranscriptWord word;
        int parts = 0;
        const auto finish = [&] {
            if (parts > 0 && !word.text.empty() && !annotation(word.text)) {
                word.probability /= float(parts);
                transcript.words.push_back(word);
            }
            word = {};
            parts = 0;
        };
        for (int t = 0; t < whisper_full_n_tokens(context, s); ++t) {
            const whisper_token_data data = whisper_full_get_token_data(context, s, t);
            if (data.id >= eot)
                continue; // timestamps and other special tokens
            std::string text = whisper_full_get_token_text(context, s, t);
            const bool starts = text.starts_with(' ') || parts == 0;
            if (starts && parts > 0)
                finish();
            if (text.starts_with(' '))
                text.erase(0, 1);
            if (text.empty())
                continue;
            if (parts == 0)
                word.start = (data.t_dtw >= 0 ? data.t_dtw : data.t0) * centisecond;
            word.end = std::max(word.start, data.t1 * centisecond);
            word.text += text;
            word.probability += data.p;
            ++parts;
        }
        finish();
    }
    whisper_free(context);

    // Times in order; each word ends where the next begins at the latest, then both ends are
    // drawn in to where there is sound, so pauses fall between words, not inside them.
    auto& words = transcript.words;
    for (std::size_t i = 1; i < words.size(); ++i)
        words[i].start = std::max(words[i].start, words[i - 1].start);
    const std::vector<bool> on = soundFrames(*samples);
    const auto frame = [&](Ticks t) { return std::min(on.size() - 1, std::size_t(std::max<Ticks>(t, 0) / (kTicksPerSecond / 100))); };
    constexpr Ticks hop = kTicksPerSecond / 100;
    constexpr std::size_t kGap = 15; // 150 ms of silence ends a word
    for (std::size_t i = 0; i < words.size(); ++i) {
        TranscriptWord& w = words[i];
        const Ticks limit = i + 1 < words.size() ? words[i + 1].start : Ticks(samples->size()) * kTicksPerSecond / kSpeechRate;
        // Starting in silence: from where the sound starts.
        while (w.start + hop < limit && !on[frame(w.start)])
            w.start += hop;
        // Starting late inside a sound: from where that sound began (after the word before).
        const Ticks earliest = i > 0 ? words[i - 1].end : 0;
        while (w.start - hop >= earliest && on[frame(w.start - hop)])
            w.start -= hop;
        // Ending at the first real silence after it starts, or where the next word begins.
        w.end = limit;
        std::size_t silent = 0;
        for (Ticks t = w.start; t < limit; t += hop) {
            silent = on[frame(t)] ? 0 : silent + 1;
            if (silent == kGap) {
                w.end = t - Ticks(kGap - 1) * hop;
                break;
            }
        }
        w.end = std::max(w.end, w.start + hop);
    }
    return transcript;
}

std::optional<Transcript> loadTranscript(const AnalysisCache& cache, const std::string& fingerprint, const std::string& variant)
{
    std::ifstream in(transcriptPath(cache, fingerprint, variant));
    if (!in)
        return std::nullopt;
    try {
        const nlohmann::json json = nlohmann::json::parse(in);
        if (json.at("version").get<int>() != Transcript::kVersion)
            return std::nullopt;
        Transcript transcript;
        transcript.language = json.at("language").get<std::string>();
        for (const auto& w : json.at("words"))
            transcript.words.push_back({w.at(0).get<Ticks>(), w.at(1).get<Ticks>(), w.at(2).get<std::string>(), w.at(3).get<float>()});
        return transcript;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

bool saveTranscript(const AnalysisCache& cache, const std::string& fingerprint, const std::string& variant,
                    const Transcript& transcript)
{
    nlohmann::json words = nlohmann::json::array();
    for (const TranscriptWord& w : transcript.words)
        words.push_back({w.start, w.end, w.text, w.probability});
    std::error_code ignored;
    std::filesystem::create_directories(cache.directory(), ignored);
    std::ofstream out(transcriptPath(cache, fingerprint, variant));
    out << nlohmann::json{{"version", Transcript::kVersion}, {"language", transcript.language}, {"words", words}}.dump();
    return bool(out);
}

} // namespace tve
