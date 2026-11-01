#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace tve {

// Audio plug-ins hosted as effects: Audio Units (AUv2 and AUv3) and VST3. Each runs at
// 48 kHz in stereo on interleaved float samples (the engine's format). Their parameters are
// set as 0..1 (normalized) whatever their own range, so a project's values and keyframes
// mean the same for any plug-in; their own state is saved as bytes.
//
// Ids: "au:<type>/<subtype>/<manufacturer>" (four-character codes, or 0x-prefixed hex when a
// code isn't printable) and "vst3:<class id in hex>".

struct PluginDescription {
    std::string id;
    std::string name;
    std::string vendor;
    std::string format; // "AU" or "VST3"
    std::string version;
    std::string path;   // a VST3's bundle; empty for Audio Units
    bool hasEditor = false;
    std::string problem; // why it can't be used (failed to load in the scan…), or empty
};

struct PluginParameter {
    std::string id; // stable within the plug-in
    std::string name;
    std::string unit;
    double defaultValue = 0.0; // normalized
    int steps = 0;             // 0: continuous; else the number of values it takes
};

class AudioPlugin {
public:
    virtual ~AudioPlugin() = default;

    // A new instance of the plug-in, ready to process; nullptr with `error` if it can't be
    // made. Safe on any thread (an AUv3 is loaded out of process).
    static std::unique_ptr<AudioPlugin> open(const std::string& id, std::string* error = nullptr);

    virtual const PluginDescription& description() const = 0;
    virtual std::vector<PluginParameter> parameters() const = 0;
    virtual void setParameter(const std::string& id, double normalized) = 0;
    virtual double parameter(const std::string& id) const = 0;
    // How the plug-in shows a value of a parameter ("-6.0 dB").
    virtual std::string displayValue(const std::string& id, double normalized) const = 0;
    virtual std::string saveState() const = 0;
    virtual bool loadState(const std::string& state) = 0;

    // In place, any number of frames.
    virtual void process(float* stereo, int frames) = 0;
    virtual void reset() = 0;
    // Samples its output lags its input by.
    virtual int latency() const = 0;
    // Seconds it may keep sounding after its input stops.
    virtual double tail() const = 0;

    // Its own window's content (an NSView*, retained for the caller), given to `done` on the
    // main thread; nullptr when it has none of its own (a generic one is made for Audio
    // Units). Main thread only.
    virtual void requestEditor(std::function<void(void* view)> done) = 0;
    // Its window was closed (before the view goes).
    virtual void editorClosed() {}
    // Called (on the main thread) when the plug-in's own window changes a parameter.
    virtual void setChangeHandler(std::function<void()> changed) = 0;
};

// Plug-in states travel in projects as base64.
std::string encodeBase64(std::string_view bytes);
std::string decodeBase64(std::string_view text); // empty if it isn't base64

// The plug-ins on this Mac: Audio Unit effects (from the system's list) and VST3 effects
// (scanned out of process, see scanVst3), by name.
std::vector<PluginDescription> listPlugins();

// Audio Unit effects only.
std::vector<PluginDescription> listAudioUnits();

} // namespace tve
