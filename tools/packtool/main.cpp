// tve-packtool: publishes content packs into a site folder for the pack store.
//
//   tve-packtool publish <pack folder> <site> --id <id> --version <n> --title <title>
//                [--description <text>] [--author <name>] [--license <text>] [--preview <png>]
//   tve-packtool catalogue <site> [--sequence <n>] [--published <date>]
//   tve-packtool verify <site> <trusted-keys.json>
//
// publish copies a pack's files to <site>/<id>/<version>/ with its pack.json (each file's
// size and SHA-256); catalogue writes <site>/catalogue.json listing each pack's newest
// version (the sequence one more than the last catalogue's unless given). Sign it with
// tools/packs/sign_catalogue.sh, then verify checks the signature and every pack's files as
// the app will.

#include "audio/SoundPacks.h"
#include "packs/Packs.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace tve;

namespace {

int usage()
{
    std::fprintf(stderr,
                 "usage: tve-packtool publish <pack folder> <site> --id <id> --version <n> --title <title>\n"
                 "                    [--description <text>] [--author <name>] [--license <text>] [--preview <png>]\n"
                 "       tve-packtool catalogue <site> [--sequence <n>] [--published <date>]\n"
                 "       tve-packtool verify <site> <trusted-keys.json>\n");
    return 2;
}

std::string readFile(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// Base64 (standard alphabet) to bytes; empty if it isn't.
std::string fromBase64(const std::string& text)
{
    static const std::string kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int bits = 0;
    unsigned value = 0;
    for (char c : text) {
        if (c == '=' || c == '\n' || c == '\r')
            continue;
        const auto at = kAlphabet.find(c);
        if (at == std::string::npos)
            return {};
        value = (value << 6) | unsigned(at);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += char((value >> bits) & 0xff);
        }
    }
    return out;
}

std::string today()
{
    const auto now = std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now());
    const std::chrono::year_month_day date{now};
    char text[16];
    std::snprintf(text, sizeof text, "%04d-%02u-%02u", int(date.year()), unsigned(date.month()), unsigned(date.day()));
    return text;
}

} // namespace

int main(int argc, char** argv)
{
    std::vector<std::string> positional;
    std::map<std::string, std::string> options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg.starts_with("--")) {
            if (i + 1 >= argc)
                return usage();
            options[arg.substr(2)] = argv[++i];
        } else {
            positional.push_back(arg);
        }
    }
    if (positional.empty())
        return usage();
    const std::string command = positional[0];

    if (command == "publish" && positional.size() == 3) {
        PackManifest meta{.id = options["id"], .version = std::atoi(options["version"].c_str()), .title = options["title"],
                          .description = options["description"], .author = options["author"], .license = options["license"]};
        if (meta.title.empty())
            return usage();
        if (auto error = publishPack(positional[1], positional[2], meta, options["preview"])) {
            std::fprintf(stderr, "tve-packtool: %s\n", error->c_str());
            return 1;
        }
        std::printf("Published %s version %d into %s\n", meta.id.c_str(), meta.version, positional[2].c_str());
        return 0;
    }

    if (command == "catalogue" && positional.size() == 2) {
        const fs::path site = positional[1];
        std::int64_t sequence = 1;
        if (options.contains("sequence")) {
            sequence = std::atoll(options["sequence"].c_str());
        } else if (const auto previous = readCatalogue(readFile(site / "catalogue.json"))) {
            sequence = previous->sequence + 1;
        }
        const Catalogue catalogue = siteCatalogue(site.string(), sequence, options.contains("published") ? options["published"] : today());
        std::ofstream(site / "catalogue.json", std::ios::binary) << catalogueJson(catalogue);
        // An old signature no longer matches.
        std::error_code ec;
        fs::remove(site / "catalogue.json.sig", ec);
        std::printf("Wrote %s: %zu packs, sequence %lld. Sign it next.\n", (site / "catalogue.json").c_str(),
                    catalogue.packs.size(), static_cast<long long>(sequence));
        return 0;
    }

    if (command == "verify" && positional.size() == 3) {
        const fs::path site = positional[1];
        const std::string data = readFile(site / "catalogue.json");
        const std::string signature = readFile(site / "catalogue.json.sig");
        const auto keys = nlohmann::json::parse(readFile(positional[2]), nullptr, false);
        bool trusted = false;
        if (keys.is_object() && keys.contains("keys") && keys["keys"].is_array())
            for (const auto& key : keys["keys"])
                if (key.contains("publicKey") && key["publicKey"].is_string())
                    trusted = trusted || verifySignature(data, signature, fromBase64(key["publicKey"].get<std::string>()));
        if (!trusted) {
            std::fprintf(stderr, "tve-packtool: the signature is not by a trusted key (or missing)\n");
            return 1;
        }
        std::string why;
        const auto catalogue = readCatalogue(data, &why);
        if (!catalogue) {
            std::fprintf(stderr, "tve-packtool: %s\n", why.c_str());
            return 1;
        }
        int problems = 0;
        for (const CatalogueEntry& entry : catalogue->packs) {
            const std::string json = readFile(site / entry.manifest);
            const auto manifest = readPackManifest(json, &why);
            std::optional<std::string> problem;
            if (bytesSha256(json) != entry.manifestSha256)
                problem = "pack.json is not the one the catalogue lists";
            else if (!manifest)
                problem = why;
            else
                problem = checkPackFolder((site / entry.manifest).parent_path().string(), *manifest);
            if (!problem && !entry.preview.empty() && fileSha256((site / entry.preview).string()) != entry.previewSha256)
                problem = "its preview is not the one the catalogue lists";
            std::printf("%s %s v%d%s%s\n", problem ? "FAIL" : "ok  ", entry.id.c_str(), entry.version,
                        problem ? ": " : "", problem ? problem->c_str() : "");
            problems += problem ? 1 : 0;
        }
        std::printf("Catalogue sequence %lld, signed by a trusted key; %d of %zu packs with problems\n",
                    static_cast<long long>(catalogue->sequence), problems, catalogue->packs.size());
        return problems == 0 ? 0 : 1;
    }
    return usage();
}
