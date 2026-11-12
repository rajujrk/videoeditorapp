#include "model/LogoReveal.h"

#include "model/Translate.h"

#include <algorithm>

namespace tve {

const std::vector<LogoRevealPreset>& logoRevealPresets()
{
    static const std::vector<LogoRevealPreset> presets{
        {"clean", TVE_TR("core", "Clean fade"), "fade-in", 1.0, "fade-out", 0.6, 0.0, "fade", TextUnit::Whole, 1.0, 0.0, 0.8},
        {"pop", TVE_TR("core", "Pop"), "pop-in", 0.6, "fade-out", 0.5, 0.0, "pop", TextUnit::Word, 0.6, 0.5, 0.5},
        {"spin", TVE_TR("core", "Spin and settle"), "spin-in", 1.0, "fade-out", 0.6, 0.5, "rise", TextUnit::Character, 0.8, 0.6, 0.9},
        {"drop", TVE_TR("core", "Drop"), "drop-in", 0.7, "fade-out", 0.5, 0.0, "drop", TextUnit::Word, 0.6, 0.5, 0.6},
        {"zoom", TVE_TR("core", "Zoom through"), "shrink-in", 0.9, "grow-out", 0.6, 0.5, "tracking", TextUnit::Character, 1.4, 0.0, 0.7},
        {"slide", TVE_TR("core", "Slide and type"), "slide-in-left", 0.8, "slide-out-right", 0.6, 0.5, "typewriter", TextUnit::Character, 1.2, 0.6, 0.9},
    };
    return presets;
}

const LogoRevealPreset* findLogoReveal(std::string_view id)
{
    const auto& presets = logoRevealPresets();
    const auto it = std::ranges::find_if(presets, [&](const LogoRevealPreset& p) { return id == p.id; });
    return it == presets.end() ? nullptr : &*it;
}

} // namespace tve
