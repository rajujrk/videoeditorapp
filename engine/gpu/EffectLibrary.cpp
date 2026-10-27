#include "gpu/EffectLibrary.h"

#include <QDir>
#include <QFile>
#include <rhi/qshaderdescription.h>

#include <algorithm>
#include <set>
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

    for (const HeaderMember& header : kHeader) {
        const auto member = std::ranges::find(block->members, QByteArray(header.name), &Desc::BlockVariable::name);
        if (member == block->members.end() || member->type != header.type || member->offset != header.offset)
            fail(where + ": EffectParams must start with tve_texelSize, tve_time, tve_flipY");
    }

    const auto samplers = description.combinedImageSamplers();
    const bool hasSource = std::ranges::any_of(samplers, [](const Desc::InOutVariable& s) {
        return s.name == "tve_source" && s.binding == 1 && s.type == Desc::Sampler2D;
    });
    if (!hasSource)
        fail(where + ": needs \"uniform sampler2D tve_source\" at binding 1");

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

const EffectLibrary& EffectLibrary::builtIn()
{
    static const EffectLibrary library = [] {
        EffectLibrary lib = load(QStringLiteral(":/effects/video"), QStringLiteral(":/effects/common/fullscreen.vert.qsb"));
        for (const QString& error : lib.errors())
            qWarning("Effect library: %s", qPrintable(error));
        return lib;
    }();
    return library;
}

EffectLibrary EffectLibrary::load(const QString& videoEffectsRoot, const QString& vertexShaderPath)
{
    EffectLibrary lib;
    lib.m_vertexShader = readShader(vertexShaderPath);
    if (!lib.m_vertexShader.isValid())
        lib.m_errors << QStringLiteral("vertex shader not found: %1").arg(vertexShaderPath);

    const QDir root(videoEffectsRoot);
    for (const QString& folder : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        try {
            LoadedEffect effect = loadEffect(root.filePath(folder));
            if (lib.find(effect.manifest.id))
                fail("duplicate effect id \"" + effect.manifest.id + "\"");
            lib.m_effects.push_back(std::move(effect));
        } catch (const std::exception& e) {
            lib.m_errors << folder + QStringLiteral(": ") + QString::fromUtf8(e.what());
        }
    }
    return lib;
}

const LoadedEffect* EffectLibrary::find(std::string_view id) const
{
    auto it = std::ranges::find_if(m_effects, [&](const LoadedEffect& e) { return e.manifest.id == id; });
    return it == m_effects.end() ? nullptr : &*it;
}

} // namespace tve
