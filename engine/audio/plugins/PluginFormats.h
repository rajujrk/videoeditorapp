#pragma once

// The plug-in formats behind AudioPlugin (internal to the engine).

#include "audio/plugins/AudioPlugin.h"

namespace tve::plugins {

std::unique_ptr<AudioPlugin> openAudioUnit(const std::string& id, std::string* error);
std::unique_ptr<AudioPlugin> openVst3(const std::string& id, std::string* error);
std::vector<PluginDescription> listVst3();
// What a VST3 bundle holds, as JSON ({"plugins": [{id, name, vendor, version}]} or
// {"error": …}); loads the bundle, so tve-pluginscan runs it in a process of its own.
std::string describeVst3Bundle(const std::string& bundle);

// Runs tve-pluginscan (TVE_PLUGIN_SCANNER, or beside the app) with `args` in a process of its
// own, for up to 20 seconds: what it printed, or empty with `problem` (missing, crashed, hung).
std::string runPluginScanner(const std::vector<std::string>& args, std::string* problem);

// Normalized 0..1 to a parameter's own value and back, logarithmic where it is shown so.
double fromNormalized(double normalized, double min, double max, bool logarithmic);
double toNormalized(double value, double min, double max, bool logarithmic);

} // namespace tve::plugins
