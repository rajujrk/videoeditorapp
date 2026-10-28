#pragma once

#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace tve {

enum class ParamType { Float, Int, Bool, Enum, Color };

// One user-adjustable effect parameter. Values are held as floats: one component for
// float/int/bool/enum (enum = index into enumValues), four (RGBA) for color.
struct EffectParamSpec {
    std::string id; // also the uniform member name in the effect's shaders
    ParamType type = ParamType::Float;
    std::map<std::string, std::string> title; // language → text, "en" required
    double min = 0.0;
    double max = 1.0;
    std::vector<float> defaultValue;
    std::vector<std::string> enumValues;
    bool keyframable = false;

    std::size_t componentCount() const { return type == ParamType::Color ? 4 : 1; }
};

struct EffectPassSpec {
    std::string fragment; // shader file name inside the effect's folder, e.g. "blur_h.frag"
};

// Where a video effect runs. Picture effects work on the clip's own picture before it is
// placed; canvas effects run after placement on a transparent canvas of the project's size
// holding just that clip, so they can draw outside the picture (shadows, borders, fills).
enum class EffectSpace { Picture, Canvas };

// One stage of an audio effect: a built-in processor (engine/audio/dsp) with fixed settings.
// The effect's params override the settings of the same name in every stage that has them.
struct AudioStageSpec {
    std::string processor;
    std::map<std::string, float> settings;
};

inline constexpr const char* kVideoEffectKind = "video-effect";
inline constexpr const char* kTransitionKind = "transition"; // one pass, two inputs
inline constexpr const char* kAudioEffectKind = "audio-effect"; // a chain of processors, no shaders

struct EffectManifest {
    std::string id; // reverse-DNS style, e.g. "tve.blur.gaussian"
    int version = 1;
    std::string kind; // kVideoEffectKind or kTransitionKind
    std::vector<std::string> categories;
    EffectSpace space = EffectSpace::Picture;
    std::map<std::string, std::string> title;
    std::vector<EffectParamSpec> params;
    std::vector<EffectPassSpec> passes;  // video effects and transitions
    std::vector<AudioStageSpec> chain;   // audio effects
    std::string license;

    const EffectParamSpec* findParam(std::string_view paramId) const;
    std::string displayTitle(const std::string& language = "en") const;
};

// Thrown when a manifest is invalid. Lists every problem found, not just the first.
class EffectManifestError : public std::runtime_error {
public:
    explicit EffectManifestError(std::vector<std::string> problems);
    const std::vector<std::string>& problems() const { return m_problems; }

private:
    std::vector<std::string> m_problems;
};

EffectManifest parseEffectManifest(const nlohmann::json& json);
EffectManifest parseEffectManifest(std::string_view jsonText);

std::string_view toString(ParamType type);

} // namespace tve
