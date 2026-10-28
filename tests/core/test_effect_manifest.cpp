#include "content/EffectManifest.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>

using namespace tve;
using nlohmann::json;

namespace {

json validManifest()
{
    return json::parse(R"({
        "id": "tve.test.sample",
        "version": 2,
        "kind": "video-effect",
        "category": ["test"],
        "title": { "en": "Sample", "fr": "Exemple" },
        "params": [
            { "id": "amount", "type": "float", "title": { "en": "Amount" }, "min": 0, "max": 10, "default": 2.5, "keyframable": true },
            { "id": "steps", "type": "int", "title": { "en": "Steps" }, "min": 1, "max": 8, "default": 4 },
            { "id": "invert", "type": "bool", "title": { "en": "Invert" }, "default": true },
            { "id": "mode", "type": "enum", "title": { "en": "Mode" }, "values": ["a", "b", "c"], "default": "b" },
            { "id": "tint", "type": "color", "title": { "en": "Tint" }, "default": [1, 0.5, 0] }
        ],
        "passes": [ { "fragment": "first.frag" }, { "fragment": "second_pass.frag" } ],
        "license": "TVE-Original"
    })");
}

// Returns the problems reported for a manifest, or an empty list if it parsed.
std::vector<std::string> problemsFor(const json& manifest)
{
    try {
        parseEffectManifest(manifest);
    } catch (const EffectManifestError& e) {
        return e.problems();
    }
    return {};
}

bool mentions(const std::vector<std::string>& problems, std::string_view text)
{
    return std::ranges::any_of(problems, [&](const std::string& p) { return p.find(text) != std::string::npos; });
}

} // namespace

TEST_CASE("a valid manifest parses with typed defaults")
{
    const EffectManifest m = parseEffectManifest(validManifest());
    CHECK(m.id == "tve.test.sample");
    CHECK(m.version == 2);
    CHECK(m.displayTitle("fr") == "Exemple");
    CHECK(m.displayTitle("de") == "Sample");
    REQUIRE(m.params.size() == 5);
    REQUIRE(m.passes.size() == 2);

    CHECK(m.findParam("amount")->defaultValue == std::vector<float>{2.5f});
    CHECK(m.findParam("amount")->keyframable);
    CHECK(m.findParam("steps")->type == ParamType::Int);
    CHECK(m.findParam("invert")->defaultValue == std::vector<float>{1.0f});
    CHECK(m.findParam("mode")->defaultValue == std::vector<float>{1.0f}); // index of "b"
    CHECK(m.findParam("mode")->max == 2.0);
    CHECK(m.findParam("tint")->defaultValue == std::vector<float>{1.0f, 0.5f, 0.0f, 1.0f});
    CHECK(m.findParam("tint")->componentCount() == 4);
}

TEST_CASE("every problem is reported at once")
{
    json m = validManifest();
    m.erase("license");
    m["id"] = "Not An Id";
    m["params"][0]["default"] = 99;
    const auto problems = problemsFor(m);
    CHECK(problems.size() == 3);
    CHECK(mentions(problems, "license"));
    CHECK(mentions(problems, "Not An Id"));
    CHECK(mentions(problems, "outside"));
}

TEST_CASE("parameter rules")
{
    json m = validManifest();

    SECTION("duplicate ids")
    {
        m["params"][1]["id"] = "amount";
        CHECK(mentions(problemsFor(m), "duplicate"));
    }
    SECTION("reserved prefix")
    {
        m["params"][0]["id"] = "tve_amount";
        CHECK(mentions(problemsFor(m), "reserved"));
    }
    SECTION("not a shader identifier")
    {
        m["params"][0]["id"] = "2fast";
        CHECK(mentions(problemsFor(m), "identifier"));
    }
    SECTION("unknown type")
    {
        m["params"][0]["type"] = "vec9";
        CHECK(mentions(problemsFor(m), "unknown type"));
    }
    SECTION("int params must be integral")
    {
        m["params"][1]["default"] = 2.5;
        CHECK(mentions(problemsFor(m), "integer"));
    }
    SECTION("min below max")
    {
        m["params"][0]["min"] = 10;
        CHECK(mentions(problemsFor(m), "min must be less than max"));
    }
    SECTION("enum default must be listed")
    {
        m["params"][3]["default"] = "z";
        CHECK(mentions(problemsFor(m), "one of values"));
    }
    SECTION("color components in range")
    {
        m["params"][4]["default"] = json::array({2, 0, 0});
        CHECK(mentions(problemsFor(m), "color default"));
    }
    SECTION("english title required")
    {
        m["params"][0]["title"] = json{{"fr", "Quantité"}};
        CHECK(mentions(problemsFor(m), "title.en"));
    }
}

TEST_CASE("pass rules")
{
    json m = validManifest();
    SECTION("at least one pass")
    {
        m["passes"] = json::array();
        CHECK(mentions(problemsFor(m), "non-empty"));
    }
    SECTION("no paths in shader names")
    {
        m["passes"][0]["fragment"] = "../../evil.frag";
        CHECK(mentions(problemsFor(m), "file name"));
    }
}

TEST_CASE("transitions are a kind with exactly one pass")
{
    json m = validManifest();
    m["kind"] = "transition";
    m["passes"] = json::array({{{"fragment", "a.frag"}}});
    CHECK(parseEffectManifest(m).kind == "transition");
    m["passes"].push_back({{"fragment", "b.frag"}});
    CHECK(mentions(problemsFor(m), "exactly one pass"));
}

TEST_CASE("unsupported kind and malformed JSON are rejected")
{
    json m = validManifest();
    m["kind"] = "audio-effect";
    CHECK(mentions(problemsFor(m), "not supported"));
    CHECK_THROWS_WITH(parseEffectManifest(std::string_view("{ not json")),
                      Catch::Matchers::ContainsSubstring("not valid JSON"));
}

TEST_CASE("video effects run on the picture or on the placed canvas")
{
    json m = validManifest();
    CHECK(parseEffectManifest(m).space == EffectSpace::Picture);
    m["space"] = "canvas";
    CHECK(parseEffectManifest(m).space == EffectSpace::Canvas);
    m["space"] = "picture";
    CHECK(parseEffectManifest(m).space == EffectSpace::Picture);
    m["space"] = "screen";
    CHECK(mentions(problemsFor(m), "space must be \"picture\" or \"canvas\""));
    m["space"] = "canvas";
    m["kind"] = "transition";
    m["passes"] = json::array({{{"fragment", "a.frag"}}});
    CHECK(mentions(problemsFor(m), "\"space\" is for video effects"));
}
