#include "audio/Synth.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <numbers>
#include <random>

namespace tve {

// (folder name, recipe JSON) of every built-in sound; generated at build time.
const std::vector<std::pair<std::string, std::string>>& builtInSoundRecipes();

namespace {

constexpr double kRate = 48000.0;
constexpr double kTwoPi = 2.0 * std::numbers::pi;

using json = nlohmann::json;

double dbToGain(double db)
{
    return std::pow(10.0, db / 20.0);
}

double midiToHz(double note)
{
    return 440.0 * std::pow(2.0, (note - 69.0) / 12.0);
}

// A stereo buffer to mix voices into.
struct Mix {
    std::vector<float> samples; // interleaved
    std::size_t frames() const { return samples.size() / 2; }
    void add(std::size_t frame, double left, double right)
    {
        if (frame < frames()) {
            samples[frame * 2] += float(left);
            samples[frame * 2 + 1] += float(right);
        }
    }
};

// Attack, then a hold at full level, then release (seconds).
double envelope(double t, double attack, double hold, double release)
{
    if (t < 0.0)
        return 0.0;
    if (t < attack)
        return t / std::max(1e-6, attack);
    if (t < attack + hold)
        return 1.0;
    const double r = t - attack - hold;
    return r < release ? 1.0 - r / release : 0.0;
}

struct OnePole {
    double state = 0.0;
    double a = 0.0;
    void cutoff(double hz) { a = std::exp(-kTwoPi * hz / kRate); }
    double lowpass(double x) { return state = (1.0 - a) * x + a * state; }
};

// --- instruments: each plays one note into the mix ---

struct Note {
    double start = 0.0;    // seconds
    double length = 0.0;   // seconds held
    double pitch = 60.0;   // MIDI
    double velocity = 1.0;
};

void pad(Mix& mix, const Note& n, double gain, double pan, std::mt19937& random)
{
    const double release = 0.8;
    const auto frames = std::size_t((n.length + release) * kRate);
    const std::size_t from = std::size_t(n.start * kRate);
    const double f = midiToHz(n.pitch);
    std::uniform_real_distribution<double> phase(0.0, 1.0);
    std::array<double, 3> p{phase(random), phase(random), phase(random)};
    const std::array<double, 3> detune{0.996, 1.0, 1.004};
    OnePole l, r;
    l.cutoff(1600.0);
    r.cutoff(1700.0);
    for (std::size_t i = 0; i < frames; ++i) {
        const double t = double(i) / kRate;
        const double e = envelope(t, 0.45, std::max(0.0, n.length - 0.45), release) * n.velocity * gain;
        double left = 0.0, right = 0.0;
        for (std::size_t k = 0; k < 3; ++k) {
            p[k] += f * detune[k] / kRate;
            p[k] -= std::floor(p[k]);
            const double saw = 2.0 * p[k] - 1.0;
            left += saw * (k == 0 ? 0.6 : k == 1 ? 0.5 : 0.4);
            right += saw * (k == 2 ? 0.6 : k == 1 ? 0.5 : 0.4);
        }
        mix.add(from + i, l.lowpass(left) * e * (1.0 - pan * 0.5), r.lowpass(right) * e * (1.0 + pan * 0.5));
    }
}

void pluck(Mix& mix, const Note& n, double gain, double pan, std::mt19937& random)
{
    // Karplus–Strong: a burst of noise round a delay line one period long, averaged each pass.
    const double f = midiToHz(n.pitch);
    const auto period = std::max<std::size_t>(2, std::size_t(kRate / f));
    std::vector<double> line(period);
    std::uniform_real_distribution<double> noise(-1.0, 1.0);
    for (double& v : line)
        v = noise(random);
    const auto frames = std::size_t(std::min(2.5, n.length + 1.2) * kRate);
    const std::size_t from = std::size_t(n.start * kRate);
    std::size_t at = 0;
    for (std::size_t i = 0; i < frames; ++i) {
        const double v = line[at];
        const double next = line[(at + 1) % period];
        line[at] = 0.497 * (v + next);
        at = (at + 1) % period;
        const double e = n.velocity * gain * (1.0 - double(i) / double(frames));
        mix.add(from + i, v * e * (1.0 - pan), v * e * (1.0 + pan));
    }
}

void bass(Mix& mix, const Note& n, double gain)
{
    const double f = midiToHz(n.pitch);
    const auto frames = std::size_t((n.length + 0.08) * kRate);
    const std::size_t from = std::size_t(n.start * kRate);
    OnePole lp;
    lp.cutoff(420.0);
    double p = 0.0;
    for (std::size_t i = 0; i < frames; ++i) {
        const double t = double(i) / kRate;
        p += f / kRate;
        p -= std::floor(p);
        const double tone = std::sin(kTwoPi * p) + 0.35 * (2.0 * p - 1.0);
        const double e = envelope(t, 0.006, std::max(0.0, n.length - 0.006), 0.08) * (0.75 + 0.25 * std::exp(-t * 6.0));
        const double v = lp.lowpass(tone) * e * n.velocity * gain;
        mix.add(from + i, v, v);
    }
}

void lead(Mix& mix, const Note& n, double gain, double pan)
{
    const double f = midiToHz(n.pitch);
    const auto frames = std::size_t((n.length + 0.15) * kRate);
    const std::size_t from = std::size_t(n.start * kRate);
    double p = 0.0;
    for (std::size_t i = 0; i < frames; ++i) {
        const double t = double(i) / kRate;
        p += f * (1.0 + 0.004 * std::sin(kTwoPi * 5.0 * t) * std::min(1.0, t * 3.0)) / kRate;
        p -= std::floor(p);
        // A square from its first odd harmonics (soft, no aliasing).
        double v = 0.0;
        for (int h = 1; h <= 7; h += 2)
            v += std::sin(kTwoPi * p * h) / h;
        const double e = envelope(t, 0.02, std::max(0.0, n.length - 0.02), 0.15) * n.velocity * gain;
        mix.add(from + i, v * e * (1.0 - pan * 0.5), v * e * (1.0 + pan * 0.5));
    }
}

void keys(Mix& mix, const Note& n, double gain, double pan)
{
    const double f = midiToHz(n.pitch);
    const auto frames = std::size_t(std::min(3.0, n.length + 1.0) * kRate);
    const std::size_t from = std::size_t(n.start * kRate);
    for (std::size_t i = 0; i < frames; ++i) {
        const double t = double(i) / kRate;
        const double v = std::sin(kTwoPi * f * t) * std::exp(-t * 1.6) + 0.4 * std::sin(kTwoPi * 2.0 * f * t) * std::exp(-t * 3.5)
                       + 0.15 * std::sin(kTwoPi * 3.0 * f * t) * std::exp(-t * 6.0);
        const double e = std::min(1.0, t / 0.004) * (t > n.length ? std::exp(-(t - n.length) * 8.0) : 1.0) * n.velocity * gain;
        mix.add(from + i, v * e * (1.0 - pan * 0.5), v * e * (1.0 + pan * 0.5));
    }
}

void drum(Mix& mix, const std::string& kind, double start, double velocity, double gain, std::mt19937& random)
{
    std::uniform_real_distribution<double> noise(-1.0, 1.0);
    const std::size_t from = std::size_t(start * kRate);
    if (kind == "kick") {
        double p = 0.0;
        for (std::size_t i = 0; i < std::size_t(0.45 * kRate); ++i) {
            const double t = double(i) / kRate;
            p += (45.0 + 75.0 * std::exp(-t * 28.0)) / kRate;
            const double v = (std::sin(kTwoPi * p) * std::exp(-t * 7.0) + (t < 0.004 ? noise(random) * 0.3 : 0.0)) * velocity * gain;
            mix.add(from + i, v, v);
        }
    } else if (kind == "snare") {
        OnePole lp;
        lp.cutoff(5000.0);
        for (std::size_t i = 0; i < std::size_t(0.25 * kRate); ++i) {
            const double t = double(i) / kRate;
            const double n = noise(random);
            const double v = (0.7 * (n - lp.lowpass(n)) * std::exp(-t * 18.0) + 0.5 * std::sin(kTwoPi * 185.0 * t) * std::exp(-t * 30.0))
                           * velocity * gain;
            mix.add(from + i, v, v);
        }
    } else if (kind == "hat" || kind == "shaker") {
        OnePole lp;
        lp.cutoff(kind == "hat" ? 7000.0 : 4000.0);
        const double decay = kind == "hat" ? 70.0 : 35.0;
        for (std::size_t i = 0; i < std::size_t(0.12 * kRate); ++i) {
            const double t = double(i) / kRate;
            const double n = noise(random);
            const double v = (n - lp.lowpass(n)) * std::exp(-t * decay) * velocity * gain * 0.6;
            mix.add(from + i, v * 0.8, v);
        }
    } else if (kind == "clap") {
        OnePole lp;
        lp.cutoff(1200.0);
        for (std::size_t i = 0; i < std::size_t(0.3 * kRate); ++i) {
            const double t = double(i) / kRate;
            // Three quick bursts, then the tail.
            const double bursts = t < 0.03 ? (std::fmod(t, 0.01) < 0.004 ? 1.0 : 0.2) : std::exp(-(t - 0.03) * 14.0);
            const double n = noise(random);
            const double v = (n - lp.lowpass(n)) * bursts * velocity * gain * 0.7;
            mix.add(from + i, v, v);
        }
    }
}

// --- music ---

int degreeOf(const std::string& numeral)
{
    static const std::array<std::string, 7> names{"i", "ii", "iii", "iv", "v", "vi", "vii"};
    std::string lower;
    for (const char c : numeral)
        if (std::isalpha(static_cast<unsigned char>(c)))
            lower += char(std::tolower(static_cast<unsigned char>(c)));
    const auto it = std::ranges::find(names, lower);
    return it == names.end() ? 0 : int(it - names.begin());
}

int keyOffset(const std::string& key)
{
    static const std::map<std::string, int> keys{{"C", 0},  {"C#", 1}, {"Db", 1},  {"D", 2},  {"D#", 3}, {"Eb", 3},
                                                 {"E", 4},  {"F", 5},  {"F#", 6},  {"Gb", 6}, {"G", 7},  {"G#", 8},
                                                 {"Ab", 8}, {"A", 9},  {"A#", 10}, {"Bb", 10}, {"B", 11}};
    const auto it = keys.find(key);
    return it == keys.end() ? 0 : it->second;
}

std::optional<std::vector<float>> renderMusic(const json& r, std::string* error)
{
    const double tempo = std::clamp(r.value("tempo", 100.0), 50.0, 180.0);
    const int bars = std::clamp(r.value("bars", 16), 2, 64);
    const int intro = std::clamp(r.value("intro", 0), 0, bars);
    const bool minor = r.value("scale", std::string("major")) == "minor";
    const int root = 60 + keyOffset(r.value("key", std::string("C")));
    const std::array<int, 7> major{0, 2, 4, 5, 7, 9, 11};
    const std::array<int, 7> minorScale{0, 2, 3, 5, 7, 8, 10};
    const auto& scale = minor ? minorScale : major;
    const auto scaleNote = [&](int degree) {
        const int octave = int(std::floor(double(degree) / 7.0));
        return root + scale[std::size_t(((degree % 7) + 7) % 7)] + 12 * octave;
    };
    std::vector<std::string> progression;
    for (const auto& c : r.value("progression", json::array({"I", "IV", "V", "I"})))
        progression.push_back(c.get<std::string>());
    if (progression.empty()) {
        if (error)
            *error = "a progression is needed";
        return std::nullopt;
    }
    const double beat = 60.0 / tempo;
    const double bar = beat * 4.0;
    const double tail = 2.5;
    Mix mix{std::vector<float>(std::size_t((bars * bar + tail) * kRate) * 2, 0.0f)};
    std::mt19937 random(r.value("seed", 1u));

    int partIndex = 0;
    for (const auto& part : r.value("parts", json::array())) {
        const std::string instrument = part.value("instrument", std::string("pad"));
        const double gain = dbToGain(part.value("gain", -12.0));
        const int octave = part.value("octave", 0);
        const double pan = std::clamp(part.value("pan", 0.0), -1.0, 1.0);
        const int from = part.value("from", instrument == "kick" || instrument == "snare" || instrument == "hat"
                                                    || instrument == "clap" || instrument == "shaker"
                                                ? intro
                                                : 0);
        std::mt19937 partRandom(r.value("seed", 1u) * 7919u + unsigned(partIndex++));
        if (part.contains("steps")) {
            // A drum row: 16 steps a bar, "x" a hit, "o" a soft one.
            const std::string steps = part.value("steps", std::string());
            for (int b = from; b < bars; ++b)
                for (std::size_t s = 0; s < steps.size(); ++s) {
                    if (steps[s] != 'x' && steps[s] != 'o')
                        continue;
                    const double at = b * bar + double(s) * bar / double(steps.size());
                    drum(mix, instrument, at, steps[s] == 'x' ? 1.0 : 0.55, gain, partRandom);
                }
            continue;
        }
        const std::string pattern = part.value("pattern", std::string("hold"));
        for (int b = from; b < bars; ++b) {
            const std::string& numeral = progression[std::size_t(b) % progression.size()];
            const int degree = degreeOf(numeral);
            std::vector<int> chord{scaleNote(degree), scaleNote(degree + 2), scaleNote(degree + 4)};
            if (numeral.find('7') != std::string::npos)
                chord.push_back(scaleNote(degree + 6));
            const double t0 = b * bar;
            const auto play = [&](const Note& n) {
                Note note = n;
                note.pitch += 12.0 * octave;
                if (instrument == "pad")
                    pad(mix, note, gain, pan, partRandom);
                else if (instrument == "pluck")
                    pluck(mix, note, gain, pan, partRandom);
                else if (instrument == "bass")
                    bass(mix, note, gain);
                else if (instrument == "lead")
                    lead(mix, note, gain, pan);
                else
                    keys(mix, note, gain, pan);
            };
            if (pattern == "hold") {
                for (const int p : chord)
                    play({t0, bar, double(p), 0.8});
            } else if (pattern == "stab") {
                for (const int beatAt : {1, 3})
                    for (const int p : chord)
                        play({t0 + beatAt * beat, beat * 0.4, double(p), 0.8});
            } else if (pattern == "arp8" || pattern == "arp16") {
                const int per = pattern == "arp8" ? 8 : 16;
                std::vector<int> notes = chord;
                notes.push_back(chord.front() + 12);
                for (int k = 0; k < per; ++k)
                    play({t0 + k * bar / per, bar / per * 0.9, double(notes[std::size_t(k) % notes.size()]), k % 4 == 0 ? 1.0 : 0.75});
            } else if (pattern == "root4" || pattern == "root8" || pattern == "rootfifth") {
                const int per = pattern == "root8" ? 8 : 4;
                for (int k = 0; k < per; ++k) {
                    const int p = pattern == "rootfifth" && k % 2 == 1 ? chord[2] : chord[0];
                    play({t0 + k * bar / per, bar / per * 0.85, double(p - 12), 1.0});
                }
            } else if (pattern == "melody") {
                // A seeded walk over the scale near the chord, eighths and quarters, a few rests.
                std::uniform_int_distribution<int> step(-2, 2), length(1, 2), rest(0, 6);
                int at = degree + 7;
                double t = t0;
                while (t < t0 + bar - 1e-6) {
                    const double len = length(partRandom) * beat / 2.0;
                    if (rest(partRandom) != 0)
                        play({t, len * 0.9, double(scaleNote(at)), 0.9});
                    at = std::clamp(at + step(partRandom), degree + 4, degree + 11);
                    t += len;
                }
            }
        }
    }
    // The last chord rings on; fade the tail; leave headroom, then a soft saturation.
    float peak = 0.0f;
    for (const float v : mix.samples)
        peak = std::max(peak, std::abs(v));
    if (peak <= 0.0f) {
        if (error)
            *error = "the recipe makes no sound";
        return std::nullopt;
    }
    const double scaleBy = dbToGain(-2.0) / peak * 1.3;
    const std::size_t fadeFrom = std::size_t((bars * bar - bar * 0.5) * kRate);
    for (std::size_t i = 0; i < mix.frames(); ++i) {
        const double fade = i < fadeFrom ? 1.0 : std::max(0.0, 1.0 - double(i - fadeFrom) / double(mix.frames() - fadeFrom));
        for (int c = 0; c < 2; ++c) {
            float& v = mix.samples[i * 2 + std::size_t(c)];
            v = float(std::tanh(v * scaleBy) / std::tanh(1.3) * dbToGain(-1.0) * fade);
        }
    }
    return std::move(mix.samples);
}

// --- sound effects ---

std::optional<std::vector<float>> renderEffect(const json& r, std::string* error)
{
    const std::string generator = r.value("generator", std::string());
    const double duration = std::clamp(r.value("duration", 1.0), 0.02, 10.0);
    Mix mix{std::vector<float>(std::size_t(duration * kRate) * 2, 0.0f)};
    std::mt19937 random(r.value("seed", 1u));
    std::uniform_real_distribution<double> noise(-1.0, 1.0);
    const double from = r.value("from", 400.0), to = r.value("to", 3000.0);
    const std::size_t n = mix.frames();
    const auto at = [&](std::size_t i) { return double(i) / kRate; };
    if (generator == "whoosh" || generator == "riser") {
        // Noise through a band that sweeps from → to (a riser: louder and with a rising tone).
        double low = 0.0, band = 0.0, p = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            const double u = double(i) / double(n);
            const double centre = from * std::pow(to / from, u);
            const double k = 1.0 - std::exp(-kTwoPi * centre / kRate);
            const double x = noise(random);
            low += k * (x - low);
            band += k * (low - band);
            double env = generator == "whoosh" ? std::sin(std::numbers::pi * u) * std::sin(std::numbers::pi * u) : u * u;
            double v = (low - band) * 3.0 * env;
            if (generator == "riser") {
                p += centre * 0.25 / kRate;
                v += 0.35 * std::sin(kTwoPi * p) * env;
            }
            const double pan = generator == "whoosh" ? (u * 2.0 - 1.0) * 0.8 : 0.0;
            mix.add(i, v * (1.0 - pan * 0.5), v * (1.0 + pan * 0.5));
        }
    } else if (generator == "impact") {
        double p = 0.0;
        OnePole lp;
        lp.cutoff(900.0);
        for (std::size_t i = 0; i < n; ++i) {
            const double t = at(i);
            p += (32.0 + 50.0 * std::exp(-t * 6.0)) / kRate;
            const double v = std::sin(kTwoPi * p) * std::exp(-t * 2.2) + lp.lowpass(noise(random)) * std::exp(-t * 12.0) * 1.5;
            mix.add(i, v, v);
        }
    } else if (generator == "pop" || generator == "click") {
        double p = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            const double t = at(i);
            if (generator == "pop") {
                p += (to + (from - to) * std::exp(-t * 60.0)) / kRate;
                const double v = std::sin(kTwoPi * p) * std::exp(-t * 40.0);
                mix.add(i, v, v);
            } else {
                const double v = noise(random) * std::exp(-t * 900.0) + 0.5 * std::sin(kTwoPi * 2500.0 * t) * std::exp(-t * 300.0);
                mix.add(i, v, v);
            }
        }
    } else if (generator == "ding" || generator == "chime") {
        // A bell: inharmonic partials, each fading at its own rate (a chime: two notes).
        const std::array<double, 5> ratio{1.0, 2.76, 5.40, 8.93, 13.34};
        const std::array<double, 5> decay{1.2, 2.0, 3.4, 5.0, 7.0};
        const auto bell = [&](double start, double f, double gain) {
            for (std::size_t i = std::size_t(start * kRate); i < n; ++i) {
                const double t = at(i) - start;
                double v = 0.0;
                for (std::size_t k = 0; k < ratio.size(); ++k)
                    v += std::sin(kTwoPi * f * ratio[k] * t) * std::exp(-t * decay[k]) / double(k + 1);
                mix.add(i, v * gain, v * gain);
            }
        };
        bell(0.0, from, 1.0);
        if (generator == "chime")
            bell(duration * 0.18, to, 0.9);
    } else if (generator == "beep" || generator == "error" || generator == "success") {
        // A run of square-ish tones: beep repeats one; error steps down; success climbs.
        const int count = generator == "beep" ? std::clamp(r.value("count", 1), 1, 8) : generator == "error" ? 2 : 3;
        const double each = duration / count;
        for (int c = 0; c < count; ++c) {
            const double f = generator == "beep" ? from : generator == "error" ? from * std::pow(0.75, c) : from * std::pow(1.26, c);
            for (std::size_t i = std::size_t(c * each * kRate); i < std::size_t((c + 0.8) * each * kRate) && i < n; ++i) {
                const double t = at(i) - c * each;
                double v = 0.0;
                for (int h = 1; h <= 5; h += 2)
                    v += std::sin(kTwoPi * f * h * t) / h;
                v *= std::min(1.0, t / 0.005) * std::min(1.0, (each * 0.8 - t) / 0.01) * 0.6;
                mix.add(i, v, v);
            }
        }
    } else if (generator == "glitch") {
        std::uniform_real_distribution<double> pick(0.0, 1.0);
        double hold = 0.0, value = 0.0, f = from;
        for (std::size_t i = 0; i < n; ++i) {
            if (hold <= 0.0) {
                hold = 0.01 + pick(random) * 0.06;
                value = pick(random) < 0.5 ? 0.0 : 1.0;
                f = from + pick(random) * (to - from);
            }
            hold -= 1.0 / kRate;
            const double v = value * (std::fmod(at(i) * f, 1.0) < 0.5 ? 0.5 : -0.5) + 0.2 * value * noise(random);
            mix.add(i, v, v * 0.9);
        }
    } else if (generator == "typing" || generator == "shutter") {
        // Clicks: at random, as on a keyboard; or the two of a camera's shutter.
        std::uniform_real_distribution<double> gap(0.06, 0.18);
        std::vector<double> times;
        if (generator == "shutter")
            times = {0.0, 0.09};
        else
            for (double t = 0.0; t < duration - 0.05; t += gap(random))
                times.push_back(t);
        for (const double start : times)
            for (std::size_t i = std::size_t(start * kRate); i < std::min(n, std::size_t((start + 0.05) * kRate)); ++i) {
                const double t = at(i) - start;
                const double v = (noise(random) * 0.8 + std::sin(kTwoPi * 1800.0 * t) * 0.4) * std::exp(-t * 250.0);
                mix.add(i, v, v);
            }
    } else if (generator == "heartbeat") {
        for (double beatAt = 0.0; beatAt < duration - 0.2; beatAt += 0.85)
            for (const double offset : {0.0, 0.22})
                for (std::size_t i = std::size_t((beatAt + offset) * kRate); i < std::min(n, std::size_t((beatAt + offset + 0.25) * kRate)); ++i) {
                    const double t = at(i) - beatAt - offset;
                    const double v = std::sin(kTwoPi * (50.0 + 30.0 * std::exp(-t * 30.0)) * t) * std::exp(-t * 14.0) * (offset > 0 ? 0.7 : 1.0);
                    mix.add(i, v, v);
                }
    } else {
        if (error)
            *error = "unknown sound generator \"" + generator + "\"";
        return std::nullopt;
    }
    float peak = 0.0f;
    for (const float v : mix.samples)
        peak = std::max(peak, std::abs(v));
    const double gain = peak > 0.0f ? dbToGain(r.value("gain", -3.0)) / peak : 0.0;
    for (float& v : mix.samples)
        v = float(v * gain);
    return std::move(mix.samples);
}

} // namespace

const std::vector<SoundRecipe>& builtInSounds()
{
    static const std::vector<SoundRecipe> sounds = [] {
        std::vector<SoundRecipe> list;
        for (const auto& [folder, text] : builtInSoundRecipes()) {
            try {
                const json r = json::parse(text);
                list.push_back({r.at("id").get<std::string>(), r.at("kind").get<std::string>(),
                                r.at("title").value("en", folder), r.value("category", std::string()), text});
            } catch (const std::exception&) {
                // (a broken recipe is left out; the tests check them all)
            }
        }
        std::ranges::sort(list, [](const SoundRecipe& a, const SoundRecipe& b) {
            return std::tie(a.kind, a.category, a.title) < std::tie(b.kind, b.category, b.title); // "music" < "sfx"
        });
        return list;
    }();
    return sounds;
}

const SoundRecipe* findSound(const std::string& id)
{
    const auto& all = builtInSounds();
    const auto it = std::ranges::find(all, id, &SoundRecipe::id);
    return it == all.end() ? nullptr : &*it;
}

std::optional<std::vector<float>> renderSound(const SoundRecipe& recipe, std::string* error)
{
    try {
        const json r = json::parse(recipe.json);
        if (recipe.kind == "music")
            return renderMusic(r, error);
        if (recipe.kind == "sfx")
            return renderEffect(r, error);
    } catch (const std::exception& e) {
        if (error)
            *error = std::string("the recipe could not be read: ") + e.what();
        return std::nullopt;
    }
    if (error)
        *error = "unknown kind of sound";
    return std::nullopt;
}

double soundDuration(const SoundRecipe& recipe)
{
    try {
        const json r = json::parse(recipe.json);
        if (recipe.kind == "music")
            return std::clamp(r.value("bars", 16), 2, 64) * 4.0 * 60.0 / std::clamp(r.value("tempo", 100.0), 50.0, 180.0) + 2.5;
        return std::clamp(r.value("duration", 1.0), 0.02, 10.0);
    } catch (const std::exception&) {
        return 0.0;
    }
}

} // namespace tve
