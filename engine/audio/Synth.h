#pragma once

#include <optional>
#include <string>
#include <vector>

namespace tve {

// The music and sound-effects library's sounds, made by our own synthesiser from recipes
// (effects/sounds/*.json, embedded at build time): music from a key, a tempo, a chord
// progression and parts (pad, pluck, bass, lead, keys and drums, each with a pattern), and
// effects from a generator (whoosh, riser, impact, pop, ding…) with its settings. The same
// recipe always gives the same sound. 48 kHz stereo float.

struct SoundRecipe {
    std::string id;       // e.g. "tve.music.sunrise"
    std::string kind;     // "music" or "sfx"
    std::string title;
    std::string category; // a mood or genre for music; a family for effects
    std::string json;     // the recipe itself
};

// The built-in recipes, music first, by category then title.
const std::vector<SoundRecipe>& builtInSounds();
const SoundRecipe* findSound(const std::string& id);

// Renders a recipe; nullopt (with `error`) if it is not one the synthesiser can play.
std::optional<std::vector<float>> renderSound(const SoundRecipe& recipe, std::string* error = nullptr);

// The rendered length in seconds, without rendering (for lists).
double soundDuration(const SoundRecipe& recipe);

} // namespace tve
