#pragma once

#include "content/EffectManifest.h"

#include <QSize>
#include <QString>
#include <QStringList>
#include <rhi/qshader.h>

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace tve {

// Where one manifest parameter is written in a pass's EffectParams uniform block.
struct UniformSlot {
    enum class Kind { Float, Int, Vec3, Vec4 };
    int offset = 0;
    Kind kind = Kind::Float;
};

struct LoadedEffectPass {
    QShader fragment;
    int uniformBlockSize = 0;
    std::map<std::string, UniformSlot> uniformSlots; // param id → slot, for the params this pass declares
};

struct LoadedEffect {
    EffectManifest manifest;
    std::vector<LoadedEffectPass> passes;
};

// Writes a pass's EffectParams block: the header (texel size, time, flipY, then for
// transitions progress) and every param the pass declares, from `values` or the manifest
// default, clamped to its range. `data` must be the block's size.
void writeEffectUniforms(const LoadedEffect& effect, std::size_t passIndex,
                         const std::map<std::string, std::vector<float>>& values, QSize size, float timeSeconds,
                         bool yUpInNdc, float progress, std::vector<char>& data);

// Effect and transition manifests plus their compiled shaders, cross-checked through
// shader reflection: every manifest param must be a uniform in at least one pass, every
// non-header uniform must be a manifest param, and types must be compatible.
class EffectLibrary {
public:
    // The effects and transitions compiled into the app (effects/CMakeLists.txt). Loaded once.
    static const EffectLibrary& builtIn();

    // Loads every <root>/<folder>/manifest.json with its .frag.qsb shaders: video effects
    // from `videoEffectsRoot`, transitions from `transitionsRoot` (if given).
    static EffectLibrary load(const QString& videoEffectsRoot, const QString& vertexShaderPath,
                              const QString& transitionsRoot = {});

    const std::vector<LoadedEffect>& effects() const { return m_effects; }
    const LoadedEffect* find(std::string_view id) const;
    const std::vector<LoadedEffect>& transitions() const { return m_transitions; }
    const LoadedEffect* findTransition(std::string_view id) const;
    const QShader& vertexShader() const { return m_vertexShader; }

    // One message per effect or transition that failed to load; those are left out.
    const QStringList& errors() const { return m_errors; }

private:
    void loadFolder(const QString& root, const char* kind, std::vector<LoadedEffect>& into);

    std::vector<LoadedEffect> m_effects;
    std::vector<LoadedEffect> m_transitions;
    QShader m_vertexShader;
    QStringList m_errors;
};

} // namespace tve
