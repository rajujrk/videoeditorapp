#pragma once

#include "audio/dsp/Processors.h"

#include <memory>

namespace tve::dsp {

// Live noise reduction for voices: RNNoise (BSD-3-Clause, model data CC0) per channel, on
// its 10 ms frames; with RNNoise's own 20 ms delay the sound comes out 30 ms late.
// Settings: "amount" (0: the input as it was … 1: fully cleaned), "quiet" (dB taken off
// where RNNoise hears no voice, 0: none). Its own blend keeps the dry sound in step with
// the delayed clean one.
std::unique_ptr<Processor> makeDenoiser();

// The delay makeDenoiser() adds, in samples at kSampleRate.
inline constexpr int kDenoiseLatency = 1440;

} // namespace tve::dsp
