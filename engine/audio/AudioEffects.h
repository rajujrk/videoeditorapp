#pragma once

#include "audio/dsp/Processors.h"
#include "content/EffectManifest.h"
#include "model/Project.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace tve {

// The audio effect packages (effects/audio/<folder>/manifest.json, kind "audio-effect"),
// each checked against the built-in processors: every stage's processor exists, every
// setting is one of its parameters, and every param of the package (but "mix") sets a
// parameter of at least one stage. A "mix" param blends the effect with the dry sound.
class AudioEffectLibrary {
public:
    // The packages compiled into the app (embedded by engine/CMakeLists.txt).
    static const AudioEffectLibrary& builtIn();
    // From (folder name, manifest JSON) pairs.
    static AudioEffectLibrary fromManifests(const std::vector<std::pair<std::string, std::string>>& manifests);

    const std::vector<EffectManifest>& effects() const { return m_effects; }
    const EffectManifest* find(std::string_view id) const;
    // One message per package left out.
    const std::vector<std::string>& errors() const { return m_errors; }

private:
    std::vector<EffectManifest> m_effects;
    std::vector<std::string> m_errors;
};

// (folder, JSON) of every built-in package; generated at build time.
const std::vector<std::pair<std::string, std::string>>& builtInAudioEffectManifests();

// A clip's audio effects, running: one processor per stage. setEffects() rebuilds only
// when the stack's effects change; new values just reconfigure the processors.
class AudioEffectChain {
public:
    explicit AudioEffectChain(const AudioEffectLibrary& library = AudioEffectLibrary::builtIn());

    void setEffects(const std::vector<ClipEffect>& effects);
    bool empty() const { return m_effects.empty(); }
    void process(float* stereo, int frames);
    void reset();
    // How long the chain may keep sounding after its input stops, in seconds.
    double tail() const;

private:
    struct Stage {
        std::unique_ptr<dsp::Processor> processor;
        const dsp::ProcessorInfo* info = nullptr;
        const AudioStageSpec* spec = nullptr;
    };
    struct Running {
        const EffectManifest* manifest = nullptr;
        std::vector<Stage> stages;
        float mix = 1.0f; // < 0: the effect has no mix param
        std::map<std::string, std::vector<float>> values;
    };
    void configure(Running& effect);

    const AudioEffectLibrary& m_library;
    std::vector<Running> m_effects;
    std::vector<std::string> m_structure; // effect ids of the running stack
    std::vector<float> m_dry;
};

} // namespace tve
