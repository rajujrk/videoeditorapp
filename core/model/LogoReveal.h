#pragma once

#include "model/TextAnimation.h"

#include <string_view>
#include <vector>

// Logo reveals: a logo picture arriving with one of the picture animations (its motion
// blurred where it moves fast), a tagline title under it arriving a moment later with a text
// animation, both leaving together at the end. Original designs, built from the app's own
// animations, so each part can be changed afterwards like any other.
namespace tve {

struct LogoRevealPreset {
    const char* id;
    const char* title;     // (marked for translation)
    const char* logoIn;    // picture animation presets (ClipAnimation.h)
    double inSeconds;
    const char* logoOut;
    double outSeconds;
    double motionBlur;     // the logo's shutter (0: none)
    const char* taglineIn; // a text animation preset (TextAnimation.h)
    TextUnit taglineUnit;
    double taglineSeconds;
    double taglineStagger;
    double taglineDelay;   // seconds after the logo
};

const std::vector<LogoRevealPreset>& logoRevealPresets();
const LogoRevealPreset* findLogoReveal(std::string_view id);

// Where the parts sit (canvas fractions from the middle) and how big.
inline constexpr double kLogoRevealScale = 0.36; // of the canvas's height
inline constexpr double kLogoRevealWidth = 0.6;  // of its width
inline constexpr double kLogoRevealY = -0.08;
inline constexpr double kLogoTaglineY = 0.27;
inline constexpr double kLogoTaglineSize = 0.06;
inline constexpr double kLogoRevealLength = 4.0; // seconds

} // namespace tve
