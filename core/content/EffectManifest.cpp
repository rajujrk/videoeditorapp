#include "content/EffectManifest.h"

#include <algorithm>
#include <cmath>
#include <regex>
#include <set>

#include <nlohmann/json.hpp>

namespace tve {

namespace {

using nlohmann::json;

const std::regex kEffectIdPattern(R"(^[a-z0-9]+(\.[a-z0-9_]+)+$)");
const std::regex kIdentifierPattern(R"(^[A-Za-z_][A-Za-z0-9_]*$)");
const std::regex kShaderFilePattern(R"(^[A-Za-z0-9_\-]+\.frag$)");

std::string joinProblems(const std::vector<std::string>& problems)
{
    std::string text = "invalid effect manifest:";
    for (const auto& problem : problems)
        text += "\n  - " + problem;
    return text;
}

class Parser {
public:
    explicit Parser(const json& root) : m_root(root) {}

    EffectManifest parse()
    {
        if (!m_root.is_object())
            fail("manifest must be a JSON object");
        else
            parseRoot();
        if (!m_problems.empty())
            throw EffectManifestError(m_problems);
        return m_manifest;
    }

private:
    void fail(std::string problem) { m_problems.push_back(std::move(problem)); }

    void parseRoot()
    {
        m_manifest.id = requiredString(m_root, "id", "id");
        if (!m_manifest.id.empty() && !std::regex_match(m_manifest.id, kEffectIdPattern))
            fail("id \"" + m_manifest.id + "\" must look like \"vendor.group.name\" (lowercase)");

        if (const json* version = field(m_root, "version", "version"); version) {
            if (!version->is_number_integer() || version->get<int>() < 1)
                fail("version must be a positive integer");
            else
                m_manifest.version = version->get<int>();
        }

        m_manifest.kind = requiredString(m_root, "kind", "kind");
        if (!m_manifest.kind.empty() && m_manifest.kind != "video-effect")
            fail("kind \"" + m_manifest.kind + "\" is not supported (expected \"video-effect\")");

        m_manifest.title = localized(m_root, "title", "title");
        m_manifest.license = requiredString(m_root, "license", "license");

        if (const json* categories = m_root.contains("category") ? &m_root["category"] : nullptr) {
            if (!categories->is_array())
                fail("category must be an array of strings");
            else
                for (const json& c : *categories) {
                    if (c.is_string())
                        m_manifest.categories.push_back(c.get<std::string>());
                    else
                        fail("category entries must be strings");
                }
        }

        parseParams();
        parsePasses();
    }

    void parseParams()
    {
        if (!m_root.contains("params"))
            return; // an effect may have no parameters
        const json& params = m_root["params"];
        if (!params.is_array()) {
            fail("params must be an array");
            return;
        }
        std::set<std::string> seen;
        for (std::size_t i = 0; i < params.size(); ++i) {
            const std::string where = "params[" + std::to_string(i) + "]";
            if (!params[i].is_object()) {
                fail(where + " must be an object");
                continue;
            }
            EffectParamSpec spec = parseParam(params[i], where);
            if (!spec.id.empty() && !seen.insert(spec.id).second)
                fail(where + ": duplicate param id \"" + spec.id + "\"");
            m_manifest.params.push_back(std::move(spec));
        }
    }

    EffectParamSpec parseParam(const json& p, const std::string& where)
    {
        EffectParamSpec spec;
        spec.id = requiredString(p, "id", where + ".id");
        if (!spec.id.empty()) {
            if (!std::regex_match(spec.id, kIdentifierPattern))
                fail(where + ": id \"" + spec.id + "\" must be a valid shader identifier");
            else if (spec.id.starts_with("tve_"))
                fail(where + ": id \"" + spec.id + "\" uses the reserved prefix tve_");
        }
        spec.title = localized(p, "title", where + ".title");
        spec.keyframable = p.value("keyframable", false);

        const std::string type = requiredString(p, "type", where + ".type");
        const json* def = field(p, "default", where + ".default");
        if (type == "float" || type == "int") {
            spec.type = type == "float" ? ParamType::Float : ParamType::Int;
            const json* min = field(p, "min", where + ".min");
            const json* max = field(p, "max", where + ".max");
            if (!min || !max || !def)
                return spec;
            if (!min->is_number() || !max->is_number() || !def->is_number()) {
                fail(where + ": min, max and default must be numbers");
                return spec;
            }
            spec.min = min->get<double>();
            spec.max = max->get<double>();
            const double value = def->get<double>();
            if (spec.type == ParamType::Int
                && (!min->is_number_integer() || !max->is_number_integer() || !def->is_number_integer()))
                fail(where + ": int params need integer min, max and default");
            if (spec.min >= spec.max)
                fail(where + ": min must be less than max");
            else if (value < spec.min || value > spec.max)
                fail(where + ": default " + def->dump() + " is outside [" + min->dump() + ", " + max->dump() + "]");
            spec.defaultValue = {static_cast<float>(value)};
        } else if (type == "bool") {
            spec.type = ParamType::Bool;
            if (def && !def->is_boolean())
                fail(where + ": default must be true or false");
            else if (def)
                spec.defaultValue = {def->get<bool>() ? 1.0f : 0.0f};
            spec.max = 1.0;
        } else if (type == "enum") {
            spec.type = ParamType::Enum;
            parseEnum(p, def, spec, where);
        } else if (type == "color") {
            spec.type = ParamType::Color;
            if (def && (!def->is_array() || (def->size() != 3 && def->size() != 4)
                        || !std::ranges::all_of(*def, [](const json& v) {
                               return v.is_number() && v.get<double>() >= 0.0 && v.get<double>() <= 1.0;
                           }))) {
                fail(where + ": color default must be [r, g, b] or [r, g, b, a] with components in 0..1");
            } else if (def) {
                for (const json& v : *def)
                    spec.defaultValue.push_back(v.get<float>());
                if (spec.defaultValue.size() == 3)
                    spec.defaultValue.push_back(1.0f);
            }
        } else if (!type.empty()) {
            fail(where + ": unknown type \"" + type + "\" (float, int, bool, enum, color)");
        }
        return spec;
    }

    void parseEnum(const json& p, const json* def, EffectParamSpec& spec, const std::string& where)
    {
        const json* values = field(p, "values", where + ".values");
        if (!values)
            return;
        if (!values->is_array() || values->empty()
            || !std::ranges::all_of(*values, [](const json& v) { return v.is_string(); })) {
            fail(where + ": values must be a non-empty array of strings");
            return;
        }
        for (const json& v : *values)
            spec.enumValues.push_back(v.get<std::string>());
        if (std::set<std::string>(spec.enumValues.begin(), spec.enumValues.end()).size() != spec.enumValues.size())
            fail(where + ": values must be unique");
        spec.min = 0.0;
        spec.max = static_cast<double>(spec.enumValues.size() - 1);
        if (!def)
            return;
        const auto it = def->is_string() ? std::ranges::find(spec.enumValues, def->get<std::string>())
                                         : spec.enumValues.end();
        if (it == spec.enumValues.end())
            fail(where + ": default must be one of values");
        else
            spec.defaultValue = {static_cast<float>(it - spec.enumValues.begin())};
    }

    void parsePasses()
    {
        const json* passes = field(m_root, "passes", "passes");
        if (!passes)
            return;
        if (!passes->is_array() || passes->empty()) {
            fail("passes must be a non-empty array");
            return;
        }
        for (std::size_t i = 0; i < passes->size(); ++i) {
            const std::string where = "passes[" + std::to_string(i) + "]";
            const json& pass = (*passes)[i];
            if (!pass.is_object()) {
                fail(where + " must be an object");
                continue;
            }
            EffectPassSpec spec{requiredString(pass, "fragment", where + ".fragment")};
            if (!spec.fragment.empty() && !std::regex_match(spec.fragment, kShaderFilePattern))
                fail(where + ": fragment \"" + spec.fragment + "\" must be a file name ending in .frag");
            m_manifest.passes.push_back(std::move(spec));
        }
    }

    // Helpers that record a problem for missing/mistyped required fields.
    const json* field(const json& object, const char* key, const std::string& where)
    {
        if (!object.contains(key)) {
            fail(where + " is required");
            return nullptr;
        }
        return &object[key];
    }

    std::string requiredString(const json& object, const char* key, const std::string& where)
    {
        const json* value = field(object, key, where);
        if (!value)
            return {};
        if (!value->is_string() || value->get<std::string>().empty()) {
            fail(where + " must be a non-empty string");
            return {};
        }
        return value->get<std::string>();
    }

    std::map<std::string, std::string> localized(const json& object, const char* key, const std::string& where)
    {
        std::map<std::string, std::string> result;
        const json* value = field(object, key, where);
        if (!value)
            return result;
        if (!value->is_object()) {
            fail(where + " must be an object of language → text");
            return result;
        }
        for (const auto& [language, text] : value->items()) {
            if (text.is_string())
                result[language] = text.get<std::string>();
            else
                fail(where + "." + language + " must be a string");
        }
        if (!result.contains("en"))
            fail(where + ".en is required");
        return result;
    }

    const json& m_root;
    EffectManifest m_manifest;
    std::vector<std::string> m_problems;
};

} // namespace

EffectManifestError::EffectManifestError(std::vector<std::string> problems)
    : std::runtime_error(joinProblems(problems)), m_problems(std::move(problems))
{
}

const EffectParamSpec* EffectManifest::findParam(std::string_view paramId) const
{
    auto it = std::ranges::find(params, paramId, &EffectParamSpec::id);
    return it == params.end() ? nullptr : &*it;
}

std::string EffectManifest::displayTitle(const std::string& language) const
{
    if (auto it = title.find(language); it != title.end())
        return it->second;
    auto en = title.find("en");
    return en != title.end() ? en->second : id;
}

EffectManifest parseEffectManifest(const nlohmann::json& json)
{
    return Parser(json).parse();
}

EffectManifest parseEffectManifest(std::string_view jsonText)
{
    nlohmann::json json;
    try {
        json = nlohmann::json::parse(jsonText);
    } catch (const nlohmann::json::parse_error& e) {
        throw EffectManifestError({std::string("not valid JSON: ") + e.what()});
    }
    return parseEffectManifest(json);
}

std::string_view toString(ParamType type)
{
    switch (type) {
    case ParamType::Float: return "float";
    case ParamType::Int: return "int";
    case ParamType::Bool: return "bool";
    case ParamType::Enum: return "enum";
    case ParamType::Color: return "color";
    }
    return "unknown";
}

} // namespace tve
