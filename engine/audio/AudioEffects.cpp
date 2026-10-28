#include "audio/AudioEffects.h"

#include <algorithm>
#include <cmath>

namespace tve {

AudioEffectLibrary AudioEffectLibrary::fromManifests(const std::vector<std::pair<std::string, std::string>>& manifests)
{
    AudioEffectLibrary library;
    for (const auto& [folder, json] : manifests) {
        try {
            EffectManifest m = parseEffectManifest(json);
            if (m.kind != kAudioEffectKind)
                throw std::runtime_error("kind \"" + m.kind + "\" is not \"audio-effect\"");
            for (const AudioStageSpec& stage : m.chain) {
                const dsp::ProcessorInfo* info = dsp::findProcessor(stage.processor);
                if (!info)
                    throw std::runtime_error("unknown processor \"" + stage.processor + "\"");
                for (const auto& [key, value] : stage.settings) {
                    if (!info->find(key))
                        throw std::runtime_error("processor \"" + stage.processor + "\" has no setting \"" + key + "\"");
                }
            }
            for (const EffectParamSpec& param : m.params) {
                if (param.id == "mix")
                    continue;
                const bool used = std::ranges::any_of(m.chain, [&](const AudioStageSpec& stage) {
                    return dsp::findProcessor(stage.processor)->find(param.id) != nullptr;
                });
                if (!used)
                    throw std::runtime_error("param \"" + param.id + "\" sets nothing in the chain");
            }
            if (std::ranges::any_of(library.m_effects, [&](const EffectManifest& e) { return e.id == m.id; }))
                throw std::runtime_error("duplicate id \"" + m.id + "\"");
            library.m_effects.push_back(std::move(m));
        } catch (const std::exception& e) {
            library.m_errors.push_back(folder + ": " + e.what());
        }
    }
    return library;
}

const AudioEffectLibrary& AudioEffectLibrary::builtIn()
{
    static const AudioEffectLibrary library = fromManifests(builtInAudioEffectManifests());
    return library;
}

const EffectManifest* AudioEffectLibrary::find(std::string_view id) const
{
    const auto it = std::ranges::find(m_effects, id, &EffectManifest::id);
    return it == m_effects.end() ? nullptr : &*it;
}

AudioEffectChain::AudioEffectChain(const AudioEffectLibrary& library) : m_library(library) {}

void AudioEffectChain::configure(Running& effect)
{
    // Each stage: its processor's defaults, the package's fixed settings, then the clip's
    // values (or the package defaults) for the params of the same name.
    const EffectManifest& m = *effect.manifest;
    const auto valueOf = [&](const EffectParamSpec& param) {
        const auto it = effect.values.find(param.id);
        const float v = it != effect.values.end() && !it->second.empty() ? it->second[0] : param.defaultValue[0];
        return std::clamp(v, float(param.min), float(param.max));
    };
    for (Stage& stage : effect.stages) {
        dsp::Settings settings(stage.spec->settings.begin(), stage.spec->settings.end());
        for (const EffectParamSpec& param : m.params) {
            if (stage.info->find(param.id))
                settings[param.id] = valueOf(param);
        }
        stage.processor->configure(stage.info->complete(settings));
    }
    const EffectParamSpec* mix = m.findParam("mix");
    effect.mix = mix ? valueOf(*mix) : -1.0f;
}

void AudioEffectChain::setEffects(const std::vector<ClipEffect>& effects)
{
    std::vector<const ClipEffect*> enabled;
    std::vector<std::string> structure;
    for (const ClipEffect& e : effects) {
        if (e.enabled && m_library.find(e.effectId)) {
            enabled.push_back(&e);
            structure.push_back(e.effectId);
        }
    }
    if (structure != m_structure) {
        m_effects.clear();
        for (const ClipEffect* e : enabled) {
            Running running;
            running.manifest = m_library.find(e->effectId);
            for (const AudioStageSpec& spec : running.manifest->chain) {
                const dsp::ProcessorInfo* info = dsp::findProcessor(spec.processor);
                running.stages.push_back({info->make(), info, &spec});
            }
            running.values = e->values;
            configure(running);
            m_effects.push_back(std::move(running));
        }
        m_structure = std::move(structure);
        return;
    }
    for (std::size_t i = 0; i < enabled.size(); ++i) {
        if (m_effects[i].values != enabled[i]->values) {
            m_effects[i].values = enabled[i]->values;
            configure(m_effects[i]);
        }
    }
}

void AudioEffectChain::process(float* stereo, int frames)
{
    for (Running& effect : m_effects) {
        const bool blend = effect.mix >= 0.0f && effect.mix < 1.0f;
        if (blend)
            m_dry.assign(stereo, stereo + std::size_t(frames) * 2);
        for (Stage& stage : effect.stages)
            stage.processor->process(stereo, frames);
        if (blend) {
            for (std::size_t i = 0; i < std::size_t(frames) * 2; ++i)
                stereo[i] = m_dry[i] + (stereo[i] - m_dry[i]) * effect.mix;
        }
    }
}

void AudioEffectChain::reset()
{
    for (Running& effect : m_effects) {
        for (Stage& stage : effect.stages)
            stage.processor->reset();
    }
}

double AudioEffectChain::tail() const
{
    // Stages in series: each one's ringing goes through the ones after it.
    double total = 0.0;
    for (const Running& effect : m_effects) {
        for (const Stage& stage : effect.stages)
            total += stage.processor->tail();
    }
    return std::min(total, 15.0);
}

} // namespace tve
