#include "gpu/EffectLibrary.h"

#include <QDir>
#include <QFile>
#include <rhi/qshaderdescription.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>
#include <span>
#include <stdexcept>

namespace tve {

namespace {

using Desc = QShaderDescription;

QShader readShader(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QShader::fromSerialized(file.readAll());
}

[[noreturn]] void fail(const std::string& message)
{
    throw std::runtime_error(message);
}

// The fixed header every EffectParams block starts with (see effects/README.md).
// Transitions add their progress after it.
struct HeaderMember {
    const char* name;
    Desc::VariableType type;
    int offset;
};
constexpr HeaderMember kHeader[] = {
    {"tve_texelSize", Desc::Vec2, 0},
    {"tve_time", Desc::Float, 8},
    {"tve_flipY", Desc::Float, 12},
};
constexpr HeaderMember kProgress{"tve_progress", Desc::Float, 16};
constexpr HeaderMember kBounds{"tve_bounds", Desc::Vec4, 16};

struct SamplerSpec {
    const char* name;
    int binding;
};
constexpr SamplerSpec kEffectSamplers[] = {{"tve_source", 1}};
constexpr SamplerSpec kTransitionSamplers[] = {{"tve_from", 1}, {"tve_to", 2}};

UniformSlot slotFor(const EffectParamSpec& param, const Desc::BlockVariable& member, const std::string& where)
{
    if (member.arrayDims.isEmpty()) {
        switch (param.type) {
        case ParamType::Float:
            if (member.type == Desc::Float)
                return {member.offset, UniformSlot::Kind::Float};
            break;
        case ParamType::Int:
        case ParamType::Bool:
        case ParamType::Enum:
            if (member.type == Desc::Int)
                return {member.offset, UniformSlot::Kind::Int};
            if (member.type == Desc::Float)
                return {member.offset, UniformSlot::Kind::Float};
            break;
        case ParamType::Color:
            if (member.type == Desc::Vec4)
                return {member.offset, UniformSlot::Kind::Vec4};
            if (member.type == Desc::Vec3)
                return {member.offset, UniformSlot::Kind::Vec3};
            break;
        }
    }
    fail(where + ": uniform \"" + param.id + "\" has the wrong type for a " + std::string(toString(param.type))
         + " param");
}

LoadedEffectPass loadPass(const EffectManifest& manifest, const QString& folder, const EffectPassSpec& spec)
{
    const std::string where = spec.fragment;
    LoadedEffectPass pass;
    pass.fragment = readShader(folder + QLatin1Char('/') + QString::fromStdString(spec.fragment) + QStringLiteral(".qsb"));
    if (!pass.fragment.isValid())
        fail(where + ": compiled shader not found (is it listed in effects/CMakeLists.txt?)");

    const Desc description = pass.fragment.description();
    const auto blocks = description.uniformBlocks();
    const auto block = std::ranges::find(blocks, 0, &Desc::UniformBlock::binding);
    if (block == blocks.end())
        fail(where + ": no EffectParams uniform block at binding 0");
    pass.uniformBlockSize = block->size;

    const bool transition = manifest.kind == kTransitionKind;
    const auto hasMember = [&](const HeaderMember& header) {
        const auto member = std::ranges::find(block->members, QByteArray(header.name), &Desc::BlockVariable::name);
        return member != block->members.end() && member->type == header.type && member->offset == header.offset;
    };
    for (const HeaderMember& header : kHeader) {
        if (!hasMember(header))
            fail(where + ": EffectParams must start with tve_texelSize, tve_time, tve_flipY");
    }
    if (transition && !hasMember(kProgress))
        fail(where + ": a transition's EffectParams must have tve_progress right after tve_flipY");
    const bool declaresBounds = std::ranges::any_of(block->members, [](const Desc::BlockVariable& m) {
        return m.name == kBounds.name;
    });
    if (declaresBounds) {
        if (transition || !hasMember(kBounds))
            fail(where + ": tve_bounds must be a vec4 right after tve_flipY (video effects only)");
        pass.boundsOffset = kBounds.offset;
    }

    const auto samplers = description.combinedImageSamplers();
    const auto requiredSamplers = transition ? std::span<const SamplerSpec>(kTransitionSamplers)
                                             : std::span<const SamplerSpec>(kEffectSamplers);
    for (const SamplerSpec& required : requiredSamplers) {
        const bool found = std::ranges::any_of(samplers, [&](const Desc::InOutVariable& s) {
            return s.name == required.name && s.binding == required.binding && s.type == Desc::Sampler2D;
        });
        if (!found)
            fail(where + ": needs \"uniform sampler2D " + required.name + "\" at binding "
                 + std::to_string(required.binding));
    }
    if (!transition) {
        for (const Desc::InOutVariable& sampler : samplers) {
            if (sampler.name == "tve_input") {
                if (sampler.binding != 2 || sampler.type != Desc::Sampler2D)
                    fail(where + ": tve_input must be a sampler2D at binding 2");
                pass.readsInput = true;
            } else if (sampler.name != "tve_source") {
                fail(where + ": unknown sampler \"" + sampler.name.toStdString() + "\"");
            }
        }
    }

    for (const Desc::BlockVariable& member : block->members) {
        const std::string name = member.name.toStdString();
        if (name.starts_with("tve_"))
            continue;
        const EffectParamSpec* param = manifest.findParam(name);
        if (!param)
            fail(where + ": uniform \"" + name + "\" has no matching manifest param");
        pass.uniformSlots[name] = slotFor(*param, member, where);
    }
    return pass;
}

LoadedEffect loadEffect(const QString& folder)
{
    QFile file(folder + QStringLiteral("/manifest.json"));
    if (!file.open(QIODevice::ReadOnly))
        fail("manifest.json not found");
    const QByteArray bytes = file.readAll();

    LoadedEffect effect{parseEffectManifest(std::string_view(bytes.constData(), std::size_t(bytes.size()))), {}};
    std::set<std::string> declared;
    for (const EffectPassSpec& spec : effect.manifest.passes) {
        effect.passes.push_back(loadPass(effect.manifest, folder, spec));
        for (const auto& [id, slot] : effect.passes.back().uniformSlots)
            declared.insert(id);
    }
    for (const EffectParamSpec& param : effect.manifest.params) {
        if (!declared.contains(param.id))
            fail("param \"" + param.id + "\" is not declared by any pass shader");
    }
    return effect;
}

} // namespace

void writeEffectUniforms(const LoadedEffect& effect, std::size_t passIndex,
                         const std::map<std::string, std::vector<float>>& values, QSize size, float timeSeconds,
                         bool yUpInNdc, float progress, std::vector<char>& data, const EffectBounds& bounds)
{
    std::ranges::fill(data, 0);
    const float header[5] = {1.0f / float(size.width()), 1.0f / float(size.height()), timeSeconds,
                             yUpInNdc ? 1.0f : 0.0f, progress};
    const bool transition = effect.manifest.kind == kTransitionKind;
    std::memcpy(data.data(), header, transition ? sizeof header : 4 * sizeof(float));
    if (const int offset = effect.passes[passIndex].boundsOffset; offset >= 0)
        std::memcpy(data.data() + offset, bounds.data(), sizeof bounds);

    for (const auto& [paramId, slot] : effect.passes[passIndex].uniformSlots) {
        const EffectParamSpec& spec = *effect.manifest.findParam(paramId);
        auto found = values.find(paramId);
        std::vector<float> value =
            found != values.end() && found->second.size() >= spec.componentCount() ? found->second : spec.defaultValue;
        if (spec.type != ParamType::Color)
            value[0] = std::clamp(value[0], float(spec.min), float(spec.max));

        char* dst = data.data() + slot.offset;
        switch (slot.kind) {
        case UniformSlot::Kind::Float:
            std::memcpy(dst, &value[0], sizeof(float));
            break;
        case UniformSlot::Kind::Int: {
            const auto integer = static_cast<std::int32_t>(std::lround(value[0]));
            std::memcpy(dst, &integer, sizeof integer);
            break;
        }
        case UniformSlot::Kind::Vec3:
            std::memcpy(dst, value.data(), 3 * sizeof(float));
            break;
        case UniformSlot::Kind::Vec4:
            std::memcpy(dst, value.data(), 4 * sizeof(float));
            break;
        }
    }
}

const EffectLibrary& EffectLibrary::builtIn()
{
    static const EffectLibrary library = [] {
        EffectLibrary lib = load(QStringLiteral(":/effects/video"), QStringLiteral(":/effects/common/fullscreen.vert.qsb"),
                                 QStringLiteral(":/effects/transitions"));
        for (const QString& error : lib.errors())
            qWarning("Effect library: %s", qPrintable(error));
        return lib;
    }();
    return library;
}

EffectLibrary EffectLibrary::load(const QString& videoEffectsRoot, const QString& vertexShaderPath,
                                  const QString& transitionsRoot)
{
    EffectLibrary lib;
    lib.m_vertexShader = readShader(vertexShaderPath);
    if (!lib.m_vertexShader.isValid())
        lib.m_errors << QStringLiteral("vertex shader not found: %1").arg(vertexShaderPath);
    lib.loadFolder(videoEffectsRoot, kVideoEffectKind, lib.m_effects);
    if (!transitionsRoot.isEmpty())
        lib.loadFolder(transitionsRoot, kTransitionKind, lib.m_transitions);
    return lib;
}

void EffectLibrary::loadFolder(const QString& rootPath, const char* kind, std::vector<LoadedEffect>& into)
{
    const QDir root(rootPath);
    for (const QString& folder : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        try {
            LoadedEffect effect = loadEffect(root.filePath(folder));
            if (effect.manifest.kind != kind)
                fail("kind \"" + effect.manifest.kind + "\" does not belong in this folder (expected \"" + kind + "\")");
            if (std::ranges::any_of(into, [&](const LoadedEffect& e) { return e.manifest.id == effect.manifest.id; }))
                fail("duplicate id \"" + effect.manifest.id + "\"");
            into.push_back(std::move(effect));
        } catch (const std::exception& e) {
            m_errors << folder + QStringLiteral(": ") + QString::fromUtf8(e.what());
        }
    }
}

const LoadedEffect* EffectLibrary::find(std::string_view id) const
{
    auto it = std::ranges::find_if(m_effects, [&](const LoadedEffect& e) { return e.manifest.id == id; });
    return it == m_effects.end() ? nullptr : &*it;
}

const LoadedEffect* EffectLibrary::findTransition(std::string_view id) const
{
    auto it = std::ranges::find_if(m_transitions, [&](const LoadedEffect& e) { return e.manifest.id == id; });
    return it == m_transitions.end() ? nullptr : &*it;
}

} // namespace tve
