#pragma once

#include <cstdint>
#include <vector>

namespace tve::dsp {

// Changes the speed of stereo sound without changing its pitch (WSOLA: overlapping
// windowed grains of the input, each taken where it best continues the previous one).
// Streaming: push input as it is decoded, pull output at any rate (input samples per output
// sample, which may change between pulls, e.g. along a speed ramp).
class TimeStretcher {
public:
    static constexpr int kWindow = 1024;    // grain length (~21 ms at 48 kHz)
    static constexpr int kHop = kWindow / 2; // output advance per grain
    static constexpr int kSearch = 256;     // how far a grain may move to fit (±)

    // Starts over: the first output sample corresponds to input sample `position` (absolute
    // index); input will be pushed starting at absolute index `inputStart`. Input before
    // `inputStart` counts as silence.
    void reset(std::int64_t inputStart, double position, double rate);
    void push(const float* stereo, int frames);
    // Marks the end of the input: missing input after it is silence.
    void finish() { m_finished = true; }

    // Whether enough input has been pushed to pull `frames` at `rate`.
    bool canPull(int frames, double rate) const;
    // The absolute index of the next input sample push() expects.
    std::int64_t inputEnd() const { return m_inputStart + std::int64_t(m_input.size() / 2); }
    // Writes `frames` output frames; call when canPull() (otherwise missing input is silence).
    void pull(float* stereo, int frames, double rate);

private:
    float at(std::int64_t index, int channel) const;
    std::int64_t bestStart(std::int64_t nominal) const;
    void addGrain(double rate);

    std::vector<float> m_input; // interleaved, from m_inputStart
    std::int64_t m_inputStart = 0;
    double m_position = 0.0;     // nominal input start of the next grain
    std::int64_t m_previous = -1; // where the previous grain started
    std::vector<float> m_overlap = std::vector<float>(std::size_t(kWindow) * 2, 0.0f);
    std::vector<float> m_ready; // finished output not yet pulled
    std::size_t m_readyAt = 0;
    int m_discard = 0; // output still to drop after a reset (the first grain's fade-in)
    bool m_finished = false;
};

} // namespace tve::dsp
