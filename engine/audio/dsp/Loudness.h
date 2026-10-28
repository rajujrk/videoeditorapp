#pragma once

#include "audio/dsp/Biquad.h"

#include <array>
#include <cstdint>
#include <deque>
#include <vector>

namespace tve::dsp {

// Loudness of stereo sound as ITU-R BS.1770 / EBU R128 measure it, in LUFS: K-weighted
// mean square over 400 ms (momentary), 3 s (short term), and the whole programme with
// gating (integrated: 400 ms blocks every 100 ms, ignoring those below -70 LUFS and then
// those 10 LU below the average of the rest). Also the sample peak.
class LoudnessMeter {
public:
    LoudnessMeter();

    void reset();
    void process(const float* stereo, int frames);

    // -infinity until there is enough sound to measure.
    double momentary() const;
    double shortTerm() const;
    double integrated() const;
    float peak() const { return m_peak; }

    static double toLufs(double meanSquare);

private:
    double meanOfLast(std::size_t subBlocks) const;

    std::array<Biquad, 2> m_shelf;
    std::array<Biquad, 2> m_highPass;
    double m_sum = 0.0;        // K-weighted energy of the sub-block being filled
    int m_count = 0;           // its samples so far
    std::deque<double> m_subBlocks; // the last 3 s of 100 ms sub-block mean squares
    std::vector<double> m_blocks;   // every 400 ms block's mean square, for the integrated value
    float m_peak = 0.0f;
};

} // namespace tve::dsp
