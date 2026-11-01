#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tve {

// Video plug-ins: an OpenFX 1.5 host of our own (the API's headers only; BSD-3-Clause) for
// image effects, in the filter context (or the general one, with just Source and Output).
// Plug-in bundles (<name>.ofx.bundle) come from /Library/OFX/Plugins, or TVE_OFX_PATH
// (folders, colon-separated); each is described in a process of its own (tve-pluginscan
// --ofx) and cached like VST3 bundles. A plug-in renders a picture in place: with Metal,
// its images Metal buffers in shared memory on the system GPU, when it says it can; on the
// CPU otherwise (TVE_OFX_METAL=0 keeps every plug-in on the CPU).
//
// Ids: "ofx:<plug-in identifier>". A plug-in's parameters are the project's values by name;
// one with 2 or 3 numbers is stored a number each, "<name>#0", "<name>#1"….

struct OfxParameterInfo {
    std::string name;
    std::string label;
    // "double", "int", "bool", "choice", "rgb", "rgba", "double2d", "int2d", "double3d",
    // "int3d"; others ("string", "custom", "group"…) aren't offered.
    std::string type;
    int components = 1;
    std::vector<double> defaults;
    double min = 0.0; // shown range (for each component)
    double max = 1.0;
    std::vector<std::string> options; // a choice's
    bool animates = true;
    std::string hint;
};

struct OfxPluginInfo {
    std::string id; // "ofx:<identifier>"
    std::string identifier;
    std::string name;
    std::string group; // the plug-in's own grouping ("Filter/Blur")
    std::string path;  // its bundle
    int versionMajor = 1;
    int versionMinor = 0;
    bool metal = false; // can render with Metal
    std::vector<OfxParameterInfo> parameters;
    std::string problem; // why it can't be used, or empty
};

inline bool isOfxEffect(std::string_view id)
{
    return id.starts_with("ofx:");
}

// The OpenFX image effects on this Mac, by name (bundles new or changed since last time are
// described out of process first).
std::vector<OfxPluginInfo> listOfxPlugins();
// One of them, by id (from the list).
std::optional<OfxPluginInfo> findOfxPlugin(const std::string& id);

// What a bundle holds, as JSON: {"plugins": [{id, identifier, name, group, major, minor,
// metal, params: [...]}]} or {"error": …}. Loads the bundle, so tve-pluginscan runs it.
std::string describeOfxBundle(const std::string& bundle);

// An instance of a plug-in, rendering one clip's pictures.
class OfxEffect {
public:
    virtual ~OfxEffect() = default;
    static std::unique_ptr<OfxEffect> open(const std::string& id, std::string* error = nullptr);

    virtual const OfxPluginInfo& info() const = 0;
    // `rgba` (width × height, top row first, straight alpha, 0..1) through the plug-in in
    // place, with `values` at `seconds` into the clip; with Metal when it and `metal` allow.
    // False (the picture unchanged) if it failed.
    virtual bool render(std::vector<float>& rgba, int width, int height, double seconds,
                        const std::map<std::string, std::vector<float>>& values, bool metal = true) = 0;
    // Whether the last render ran with Metal.
    virtual bool usedMetal() const = 0;
};

} // namespace tve
