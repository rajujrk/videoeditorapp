#pragma once

#include "audio/Synth.h"

#include <optional>
#include <string>
#include <vector>

namespace tve {

// Sound packs on disk: a folder with a manifest.json and its sounds as WAV files, each with
// its SHA-256 (a pack whose file does not match leaves that sound out), and an optional short
// preview of each. The format, version 1:
//   {"format": "tve-sound-pack", "version": 1, "id": "...", "title": "...", "license": "...",
//    "sounds": [{"id", "kind": "music"|"sfx", "title", "category", "file", "sha256",
//                "duration", "preview"?, "previewSha256"?}]}
// File names are relative to the pack's folder and may not leave it.

struct PackSound {
    std::string id;
    std::string kind;
    std::string title;
    std::string category;
    std::string path;    // absolute
    std::string preview; // absolute, or empty (the sound itself previews)
    double duration = 0;
};

struct SoundPack {
    std::string id;
    std::string title;
    std::string license;
    std::string folder;
    std::vector<PackSound> sounds;
    std::vector<std::string> problems; // sounds left out, and why
};

// Reads a pack, checking every file's SHA-256; nullopt (with `error`) if the manifest is not
// one.
std::optional<SoundPack> loadSoundPack(const std::string& folder, std::string* error = nullptr);

// Every pack in the folders directly under `root`.
std::vector<SoundPack> loadSoundPacks(const std::string& root);

// Renders recipes into a new pack at `folder` (made if missing): WAV files, previews of up
// to `previewSeconds` for longer sounds, and the manifest.
bool writeSoundPack(const std::string& folder, const std::string& id, const std::string& title,
                    const std::vector<SoundRecipe>& recipes, double previewSeconds = 15,
                    std::string* error = nullptr);

// Writes interleaved stereo 48 kHz samples as a float WAV.
bool writeSoundFile(const std::string& path, const std::vector<float>& samples, std::string* error = nullptr);

// The SHA-256 of a file in lower-case hex; empty if it cannot be read.
std::string fileSha256(const std::string& path);

} // namespace tve
