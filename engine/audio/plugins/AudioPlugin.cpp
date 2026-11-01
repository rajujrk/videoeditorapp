#include "audio/plugins/AudioPlugin.h"

#include "audio/plugins/PluginFormats.h"

#include <algorithm>
#include <cstdint>
#include <string>

namespace tve {

std::unique_ptr<AudioPlugin> AudioPlugin::open(const std::string& id, std::string* error)
{
    if (id.starts_with("au:"))
        return plugins::openAudioUnit(id, error);
    if (id.starts_with("vst3:"))
        return plugins::openVst3(id, error);
    if (error)
        *error = "not a plug-in id";
    return nullptr;
}

namespace {
constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
}

std::string encodeBase64(std::string_view bytes)
{
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const unsigned a = std::uint8_t(bytes[i]);
        const unsigned b = i + 1 < bytes.size() ? std::uint8_t(bytes[i + 1]) : 0;
        const unsigned c = i + 2 < bytes.size() ? std::uint8_t(bytes[i + 2]) : 0;
        const unsigned n = (a << 16) | (b << 8) | c;
        out += kAlphabet[(n >> 18) & 63];
        out += kAlphabet[(n >> 12) & 63];
        out += i + 1 < bytes.size() ? kAlphabet[(n >> 6) & 63] : '=';
        out += i + 2 < bytes.size() ? kAlphabet[n & 63] : '=';
    }
    return out;
}

std::string decodeBase64(std::string_view text)
{
    std::string out;
    unsigned value = 0;
    int bits = 0;
    for (char c : text) {
        if (c == '=')
            break;
        const char* at = std::char_traits<char>::find(kAlphabet, 64, c);
        if (!at)
            return {};
        value = (value << 6) | unsigned(at - kAlphabet);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += char((value >> bits) & 0xff);
        }
    }
    return out;
}

std::vector<PluginDescription> listPlugins()
{
    std::vector<PluginDescription> list = listAudioUnits();
    std::vector<PluginDescription> vst3 = plugins::listVst3();
    list.insert(list.end(), vst3.begin(), vst3.end());
    std::ranges::stable_sort(list, [](const PluginDescription& a, const PluginDescription& b) { return a.name < b.name; });
    return list;
}

} // namespace tve
