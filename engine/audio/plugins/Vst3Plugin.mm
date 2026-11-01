// VST3 hosting with the VST3 SDK's hosting helpers (MIT; fetched by cmake/Vst3Sdk.cmake).
// Bundles are checked in a separate process (tve-pluginscan) so one that crashes or hangs
// can't take the app with it; what they hold is cached by path and modification time.
// Manual retain/release for the Cocoa parts.

#include "audio/plugins/PluginFormats.h"

#include "decode/AudioDecoder.h"

#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "public.sdk/source/vst/utility/stringconvert.h"

#include <nlohmann/json.hpp>

#import <AppKit/AppKit.h>
#include <mach-o/dyld.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <thread>

extern char** environ;

using namespace Steinberg;
using namespace Steinberg::Vst;
namespace fs = std::filesystem;

namespace tve::plugins {

namespace {

// --- Finding and checking bundles ---

std::vector<std::string> vst3Folders()
{
    if (const char* paths = std::getenv("TVE_VST3_PATH")) {
        std::vector<std::string> folders;
        std::string all = paths;
        for (std::size_t at = 0; at <= all.size();) {
            const std::size_t colon = std::min(all.find(':', at), all.size());
            if (colon > at)
                folders.push_back(all.substr(at, colon - at));
            at = colon + 1;
        }
        return folders;
    }
    const char* home = std::getenv("HOME");
    return {"/Library/Audio/Plug-Ins/VST3", std::string(home ? home : "") + "/Library/Audio/Plug-Ins/VST3"};
}

std::vector<std::string> vst3Bundles()
{
    std::vector<std::string> bundles;
    for (const std::string& folder : vst3Folders()) {
        std::error_code ec;
        for (auto it = fs::recursive_directory_iterator(folder, ec); !ec && it != fs::recursive_directory_iterator();
             it.increment(ec)) {
            if (it->is_directory() && it->path().extension() == ".vst3") {
                bundles.push_back(it->path().string());
                it.disable_recursion_pending();
            }
        }
    }
    std::ranges::sort(bundles);
    return bundles;
}

std::string scannerPath()
{
    if (const char* path = std::getenv("TVE_PLUGIN_SCANNER"))
        return path;
    char buffer[4096];
    uint32_t size = sizeof buffer;
    if (_NSGetExecutablePath(buffer, &size) != 0)
        return {};
    return (fs::path(buffer).parent_path() / "tve-pluginscan").string();
}

std::int64_t modified(const std::string& path)
{
    struct stat info{};
    return stat(path.c_str(), &info) == 0 ? std::int64_t(info.st_mtimespec.tv_sec) : 0;
}

// Runs the scanner on one bundle: its classes as JSON, or why not.
std::string runScanner(const std::string& bundle, std::string* problem)
{
    const std::string scanner = scannerPath();
    if (scanner.empty() || access(scanner.c_str(), X_OK) != 0) {
        *problem = "it couldn't be checked (the plug-in scanner is missing)";
        return {};
    }
    int out[2];
    if (pipe(out) != 0) {
        *problem = "it couldn't be checked";
        return {};
    }
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, out[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&actions, out[0]);
    char* argv[] = {const_cast<char*>(scanner.c_str()), const_cast<char*>(bundle.c_str()), nullptr};
    pid_t pid = 0;
    const int spawned = posix_spawn(&pid, scanner.c_str(), &actions, nullptr, argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    close(out[1]);
    if (spawned != 0) {
        close(out[0]);
        *problem = "it couldn't be checked";
        return {};
    }
    // Read while it runs; give it 20 seconds.
    std::string text;
    std::thread reader([&] {
        char chunk[4096];
        ssize_t n;
        while ((n = read(out[0], chunk, sizeof chunk)) > 0)
            text.append(chunk, std::size_t(n));
    });
    int status = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    bool exited = false;
    while (std::chrono::steady_clock::now() < deadline) {
        if (waitpid(pid, &status, WNOHANG) == pid) {
            exited = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (!exited) {
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
    }
    reader.join();
    close(out[0]);
    if (!exited) {
        *problem = "it hung while being checked";
        return {};
    }
    if (!WIFEXITED(status)) {
        *problem = "it crashed while being checked";
        return {};
    }
    return text;
}

struct ScanEntry {
    std::int64_t modified = 0;
    std::vector<PluginDescription> plugins;
};

std::mutex& scanMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::map<std::string, ScanEntry>& scanCache()
{
    static std::map<std::string, ScanEntry> cache;
    return cache;
}

std::string cacheFile()
{
    const char* path = std::getenv("TVE_PLUGIN_CACHE_FILE");
    return path ? path : std::string();
}

void loadCacheFile()
{
    static bool loaded = false;
    if (loaded || cacheFile().empty())
        return;
    loaded = true;
    std::ifstream in(cacheFile());
    const auto j = nlohmann::json::parse(in, nullptr, false);
    if (!j.is_object())
        return;
    for (const auto& [path, entry] : j.items()) {
        ScanEntry scanned{.modified = entry.value("modified", std::int64_t(0))};
        for (const auto& p : entry.value("plugins", nlohmann::json::array()))
            scanned.plugins.push_back({.id = p.value("id", ""), .name = p.value("name", ""), .vendor = p.value("vendor", ""),
                                       .format = "VST3", .version = p.value("version", ""), .path = path,
                                       .hasEditor = true, .problem = p.value("problem", "")});
        scanCache()[path] = std::move(scanned);
    }
}

void saveCacheFile()
{
    if (cacheFile().empty())
        return;
    nlohmann::json j = nlohmann::json::object();
    for (const auto& [path, entry] : scanCache()) {
        nlohmann::json plugins = nlohmann::json::array();
        for (const PluginDescription& p : entry.plugins)
            plugins.push_back({{"id", p.id}, {"name", p.name}, {"vendor", p.vendor}, {"version", p.version}, {"problem", p.problem}});
        j[path] = {{"modified", entry.modified}, {"plugins", plugins}};
    }
    std::error_code ec;
    fs::create_directories(fs::path(cacheFile()).parent_path(), ec);
    std::ofstream(cacheFile()) << j.dump(1);
}

// --- Loaded modules ---

VST3::Hosting::Module::Ptr loadModule(const std::string& path, std::string* error)
{
    static std::mutex mutex;
    static std::map<std::string, VST3::Hosting::Module::Ptr> modules;
    std::lock_guard lock(mutex);
    if (const auto it = modules.find(path); it != modules.end())
        return it->second;
    std::string why;
    auto module = VST3::Hosting::Module::create(path, why);
    if (!module) {
        if (error)
            *error = why.empty() ? "it couldn't be loaded" : why;
        return nullptr;
    }
    modules[path] = module;
    return module;
}

HostApplication& hostApplication()
{
    static HostApplication* host = [] {
        auto* made = new HostApplication;
        PluginContextFactory::instance().setPluginContext(made);
        return made;
    }();
    return *host;
}

std::string text(const String128 value)
{
    return VST3::StringConvert::convert(value);
}

// --- An instance ---

class Vst3Plugin;

// What the plug-in's own window tells its host.
class ComponentHandler final : public IComponentHandler {
public:
    explicit ComponentHandler(Vst3Plugin* plugin) : m_plugin(plugin) {}
    tresult PLUGIN_API beginEdit(ParamID) override { return kResultOk; }
    tresult PLUGIN_API performEdit(ParamID id, ParamValue value) override;
    tresult PLUGIN_API endEdit(ParamID) override { return kResultOk; }
    tresult PLUGIN_API restartComponent(int32) override;
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override
    {
        if (FUnknownPrivate::iidEqual(iid, IComponentHandler::iid) || FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
            *obj = this;
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

private:
    Vst3Plugin* m_plugin;
};

class Vst3Plugin final : public AudioPlugin {
public:
    Vst3Plugin(VST3::Hosting::Module::Ptr module, IPtr<PlugProvider> provider, PluginDescription description)
        : m_module(std::move(module)), m_provider(std::move(provider)), m_description(std::move(description)),
          m_handler(this)
    {
        m_component = m_provider->getComponentPtr();
        m_controller = m_provider->getControllerPtr();
        m_processor = FUnknownPtr<IAudioProcessor>(m_component);
        if (m_controller)
            m_controller->setComponentHandler(&m_handler);
    }

    ~Vst3Plugin() override
    {
        *m_changed = nullptr;
        if (m_view) {
            m_view->removed();
            m_view = nullptr;
        }
        if (m_controller)
            m_controller->setComponentHandler(nullptr);
        if (m_processor)
            m_processor->setProcessing(false);
        if (m_component)
            m_component->setActive(false);
        m_data.unprepare();
    }

    bool prepare(std::string* error)
    {
        if (!m_component || !m_processor) {
            *error = "it isn't an audio effect";
            return false;
        }
        if (m_component->getBusCount(kAudio, kInput) < 1 || m_component->getBusCount(kAudio, kOutput) < 1) {
            *error = "it has no sound in or out";
            return false;
        }
        SpeakerArrangement stereo = SpeakerArr::kStereo;
        SpeakerArrangement in = stereo, out = stereo;
        if (m_processor->setBusArrangements(&in, 1, &out, 1) != kResultTrue) {
            // Some take their own arrangement; it must still be stereo.
            m_processor->getBusArrangement(kInput, 0, in);
            m_processor->getBusArrangement(kOutput, 0, out);
            if (in != stereo || out != stereo) {
                *error = "it doesn't take stereo sound";
                return false;
            }
        }
        m_component->activateBus(kAudio, kInput, 0, true);
        m_component->activateBus(kAudio, kOutput, 0, true);
        ProcessSetup setup{kOffline, kSample32, kBlock, double(audio::kSampleRate)};
        if (m_processor->setupProcessing(setup) != kResultOk) {
            *error = "it can't run at 48 kHz";
            return false;
        }
        if (!m_data.prepare(*m_component, kBlock, kSample32)) {
            *error = "it couldn't get ready";
            return false;
        }
        m_component->setActive(true);
        m_processor->setProcessing(true);
        m_changes.setMaxParameters(256);
        return true;
    }

    const PluginDescription& description() const override { return m_description; }

    std::vector<PluginParameter> parameters() const override
    {
        std::vector<PluginParameter> list;
        if (!m_controller)
            return list;
        for (int32 i = 0; i < m_controller->getParameterCount(); ++i) {
            ParameterInfo info{};
            if (m_controller->getParameterInfo(i, info) != kResultOk || (info.flags & ParameterInfo::kIsReadOnly)
                || (info.flags & ParameterInfo::kIsBypass) || !(info.flags & ParameterInfo::kCanAutomate))
                continue;
            list.push_back({.id = std::to_string(info.id), .name = text(info.title), .unit = text(info.units),
                            .defaultValue = m_controller->getParamNormalized(info.id), .steps = info.stepCount > 0 ? int(info.stepCount) + 1 : 0});
        }
        return list;
    }

    void setParameter(const std::string& id, double normalized) override
    {
        if (!m_controller)
            return;
        const ParamID pid = ParamID(std::stoul(id));
        normalized = std::clamp(normalized, 0.0, 1.0);
        if (std::abs(m_controller->getParamNormalized(pid) - normalized) <= 1e-7 && !m_pending.contains(pid))
            return;
        m_controller->setParamNormalized(pid, normalized);
        m_pending[pid] = normalized;
    }

    double parameter(const std::string& id) const override
    {
        return m_controller ? m_controller->getParamNormalized(ParamID(std::stoul(id))) : 0.0;
    }

    std::string displayValue(const std::string& id, double normalized) const override
    {
        if (!m_controller)
            return {};
        String128 shown{};
        const ParamID pid = ParamID(std::stoul(id));
        std::string value = m_controller->getParamStringByValue(pid, std::clamp(normalized, 0.0, 1.0), shown) == kResultOk
                                ? text(shown)
                                : std::string();
        for (int32 i = 0; i < m_controller->getParameterCount(); ++i) {
            ParameterInfo info{};
            if (m_controller->getParameterInfo(i, info) == kResultOk && info.id == pid) {
                const std::string unit = text(info.units);
                if (!unit.empty())
                    value += " " + unit;
            }
        }
        return value;
    }

    // "TVE3", then the component's and the controller's states, each with its length.
    std::string saveState() const override
    {
        const auto read = [](auto* object) {
            std::string bytes;
            if (!object)
                return bytes;
            MemoryStream stream;
            if (object->getState(&stream) == kResultOk)
                bytes.assign(stream.getData(), std::size_t(stream.getSize()));
            return bytes;
        };
        const std::string component = read(m_component.get());
        const std::string controller = read(m_controller.get());
        std::string out = "TVE3";
        const auto put = [&](const std::string& part) {
            const auto n = std::uint32_t(part.size());
            out.append(reinterpret_cast<const char*>(&n), 4);
            out += part;
        };
        put(component);
        put(controller);
        return out;
    }

    bool loadState(const std::string& state) override
    {
        if (state.size() < 12 || state.compare(0, 4, "TVE3") != 0)
            return false;
        std::size_t at = 4;
        const auto take = [&](std::string& part) {
            if (at + 4 > state.size())
                return false;
            std::uint32_t n = 0;
            std::memcpy(&n, state.data() + at, 4);
            at += 4;
            if (at + n > state.size())
                return false;
            part = state.substr(at, n);
            at += n;
            return true;
        };
        std::string component, controller;
        if (!take(component) || !take(controller))
            return false;
        if (!component.empty()) {
            MemoryStream forComponent(const_cast<char*>(component.data()), TSize(component.size()));
            if (m_component->setState(&forComponent) != kResultOk)
                return false;
            if (m_controller) {
                MemoryStream forController(const_cast<char*>(component.data()), TSize(component.size()));
                m_controller->setComponentState(&forController);
            }
        }
        if (!controller.empty() && m_controller) {
            MemoryStream stream(const_cast<char*>(controller.data()), TSize(controller.size()));
            m_controller->setState(&stream);
        }
        m_pending.clear();
        return true;
    }

    void process(float* stereo, int frames) override
    {
        for (int done = 0; done < frames;) {
            const int count = std::min(kBlock, frames - done);
            float* block = stereo + std::size_t(done) * 2;
            float* inL = m_data.inputs[0].channelBuffers32[0];
            float* inR = m_data.inputs[0].channelBuffers32[1];
            for (int i = 0; i < count; ++i) {
                inL[i] = block[std::size_t(i) * 2];
                inR[i] = block[std::size_t(i) * 2 + 1];
            }
            // Parameter changes since the last block go with this one.
            m_changes.clearQueue();
            for (const auto& [pid, value] : m_pending) {
                int32 index = 0;
                if (IParamValueQueue* queue = m_changes.addParameterData(pid, index))
                    queue->addPoint(0, value, index);
            }
            m_pending.clear();
            m_data.inputParameterChanges = &m_changes;
            m_data.numSamples = count;
            if (m_processor->process(m_data) != kResultOk) {
                std::fill(block, block + std::size_t(count) * 2, 0.0f);
            } else {
                const float* outL = m_data.outputs[0].channelBuffers32[0];
                const float* outR = m_data.outputs[0].channelBuffers32[1];
                for (int i = 0; i < count; ++i) {
                    block[std::size_t(i) * 2] = outL[i];
                    block[std::size_t(i) * 2 + 1] = outR[i];
                }
            }
            done += count;
        }
    }

    void reset() override
    {
        m_processor->setProcessing(false);
        m_component->setActive(false);
        m_component->setActive(true);
        m_processor->setProcessing(true);
    }

    int latency() const override { return int(m_processor->getLatencySamples()); }

    double tail() const override
    {
        const uint32 samples = m_processor->getTailSamples();
        return samples == kInfiniteTail ? 30.0 : std::min(double(samples) / audio::kSampleRate, 30.0);
    }

    void requestEditor(std::function<void(void*)> done) override
    {
        NSView* container = nil;
        if (m_controller && !m_view) {
            IPtr<IPlugView> view = owned(m_controller->createView(ViewType::kEditor));
            if (view && view->isPlatformTypeSupported(kPlatformTypeNSView) == kResultTrue) {
                ViewRect size{};
                view->getSize(&size);
                container = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, std::max(size.getWidth(), 200), std::max(size.getHeight(), 100))];
                if (view->attached(container, kPlatformTypeNSView) == kResultOk) {
                    m_view = view;
                } else {
                    [container release];
                    container = nil;
                }
            }
        }
        done(container);
    }

    void editorClosed() override
    {
        if (m_view) {
            m_view->removed();
            m_view = nullptr;
        }
    }

    void setChangeHandler(std::function<void()> changed) override { *m_changed = std::move(changed); }

    // From the plug-in's own window: passed on to its processing, and to whoever listens.
    void edited(ParamID id, ParamValue value)
    {
        m_pending[id] = value;
        notify();
    }
    void notify()
    {
        std::shared_ptr<std::function<void()>> handler = m_changed;
        dispatch_async(dispatch_get_main_queue(), ^{
            if (*handler)
                (*handler)();
        });
    }

private:
    static constexpr int kBlock = 1024;

    VST3::Hosting::Module::Ptr m_module;
    IPtr<PlugProvider> m_provider;
    PluginDescription m_description;
    ComponentHandler m_handler;
    IPtr<IComponent> m_component;
    IPtr<IEditController> m_controller;
    FUnknownPtr<IAudioProcessor> m_processor{nullptr};
    HostProcessData m_data;
    ParameterChanges m_changes;
    std::map<ParamID, ParamValue> m_pending;
    IPtr<IPlugView> m_view;
    std::shared_ptr<std::function<void()>> m_changed = std::make_shared<std::function<void()>>();
};

tresult PLUGIN_API ComponentHandler::performEdit(ParamID id, ParamValue value)
{
    m_plugin->edited(id, value);
    return kResultOk;
}

tresult PLUGIN_API ComponentHandler::restartComponent(int32)
{
    m_plugin->notify();
    return kResultOk;
}

} // namespace

std::vector<PluginDescription> listVst3()
{
    std::lock_guard lock(scanMutex());
    loadCacheFile();
    std::vector<PluginDescription> list;
    bool scanned = false;
    const std::vector<std::string> bundles = vst3Bundles();
    for (const std::string& bundle : bundles) {
        const std::int64_t when = modified(bundle);
        auto& entry = scanCache()[bundle];
        if (entry.modified != when || entry.plugins.empty()) {
            entry = {.modified = when};
            std::string problem;
            const std::string output = runScanner(bundle, &problem);
            const auto j = nlohmann::json::parse(output, nullptr, false);
            if (problem.empty() && j.is_object() && j.contains("error"))
                problem = j.value("error", "it couldn't be loaded");
            if (problem.empty() && j.is_object())
                for (const auto& p : j.value("plugins", nlohmann::json::array()))
                    entry.plugins.push_back({.id = p.value("id", ""), .name = p.value("name", ""), .vendor = p.value("vendor", ""),
                                             .format = "VST3", .version = p.value("version", ""), .path = bundle,
                                             .hasEditor = true});
            if (entry.plugins.empty())
                entry.plugins.push_back({.id = "vst3:?" + bundle, .name = fs::path(bundle).stem().string(), .format = "VST3",
                                         .path = bundle,
                                         .problem = problem.empty() ? std::string("it holds no audio effects") : problem});
            scanned = true;
        }
        list.insert(list.end(), entry.plugins.begin(), entry.plugins.end());
    }
    // Bundles no longer there are forgotten.
    std::erase_if(scanCache(), [&](const auto& item) { return !std::ranges::binary_search(bundles, item.first); });
    if (scanned)
        saveCacheFile();
    return list;
}

std::unique_ptr<AudioPlugin> openVst3(const std::string& id, std::string* error)
{
    const auto fail = [&](const std::string& why) -> std::unique_ptr<AudioPlugin> {
        if (error)
            *error = why;
        return nullptr;
    };
    const std::vector<PluginDescription> all = listVst3();
    const auto found = std::ranges::find(all, id, &PluginDescription::id);
    if (found == all.end())
        return fail("the VST3 plug-in isn't installed on this Mac");
    if (!found->problem.empty())
        return fail("the VST3 plug-in can't be used: " + found->problem);
    std::string why;
    auto module = loadModule(found->path, &why);
    if (!module)
        return fail("the VST3 plug-in couldn't be loaded: " + why);
    hostApplication();
    const auto& classes = module->getFactory().classInfos();
    const auto info = std::ranges::find_if(classes, [&](const VST3::Hosting::ClassInfo& c) {
        return "vst3:" + c.ID().toString() == id;
    });
    if (info == classes.end())
        return fail("the VST3 plug-in isn't in its bundle any more");
    auto provider = owned(new PlugProvider(module->getFactory(), *info, true));
    if (!provider->initialize())
        return fail("the VST3 plug-in couldn't be started");
    auto plugin = std::make_unique<Vst3Plugin>(module, provider, *found);
    if (!plugin->prepare(&why))
        return fail("the VST3 plug-in can't be used: " + why);
    return plugin;
}

std::string describeVst3Bundle(const std::string& bundle)
{
    std::string why;
    auto module = VST3::Hosting::Module::create(bundle, why);
    if (!module)
        return nlohmann::json{{"error", why.empty() ? "it couldn't be loaded" : why}}.dump();
    nlohmann::json plugins = nlohmann::json::array();
    for (const auto& c : module->getFactory().classInfos()) {
        if (c.category() != kVstAudioEffectClass)
            continue;
        // Effects only: not instruments.
        const std::string sub = c.subCategoriesString();
        if (sub.find("Instrument") != std::string::npos)
            continue;
        plugins.push_back({{"id", "vst3:" + c.ID().toString()}, {"name", c.name()}, {"vendor", c.vendor()}, {"version", c.version()}});
    }
    return nlohmann::json{{"plugins", plugins}}.dump();
}

} // namespace tve::plugins
