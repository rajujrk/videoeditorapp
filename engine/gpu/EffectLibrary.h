#pragma once

#include "content/EffectManifest.h"

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

// Effect manifests plus their compiled shaders, cross-checked through shader reflection:
// every manifest param must be a uniform in at least one pass, every non-header uniform
// must be a manifest param, and types must be compatible.
class EffectLibrary {
public:
    // The effects compiled into the app (effects/CMakeLists.txt). Loaded once.
    static const EffectLibrary& builtIn();

    // Loads every <videoEffectsRoot>/<folder>/manifest.json with its .frag.qsb shaders.
    static EffectLibrary load(const QString& videoEffectsRoot, const QString& vertexShaderPath);

    const std::vector<LoadedEffect>& effects() const { return m_effects; }
    const LoadedEffect* find(std::string_view id) const;
    const QShader& vertexShader() const { return m_vertexShader; }

    // One message per effect that failed to load; those effects are left out.
    const QStringList& errors() const { return m_errors; }

private:
    std::vector<LoadedEffect> m_effects;
    QShader m_vertexShader;
    QStringList m_errors;
};

} // namespace tve
