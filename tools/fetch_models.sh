#!/usr/bin/env bash
# Downloads the speech-recognition model the tests and development use into samples/models/
# (git-ignored), from the official whisper.cpp model repository, and checks its SHA-256.
# The app itself downloads models on first use (ui/player/SpeechModels.cpp); this is only
# for working on it.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
out="$root/samples/models"
mkdir -p "$out"

fetch() {
    local name="$1" sha="$2"
    local file="$out/$name"
    if [[ -f "$file" ]] && [[ "$(shasum -a 256 "$file" | cut -d' ' -f1)" == "$sha" ]]; then
        echo "exists: $name"
        return
    fi
    echo "downloading: $name"
    curl -fL --retry 3 -o "$file.part" "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/$name"
    local got
    got="$(shasum -a 256 "$file.part" | cut -d' ' -f1)"
    if [[ "$got" != "$sha" ]]; then
        rm -f "$file.part"
        echo "error: $name has SHA-256 $got, expected $sha" >&2
        exit 1
    fi
    mv "$file.part" "$file"
}

# Whisper base, English, 5-bit (MIT): 59,721,011 bytes.
fetch ggml-base.en-q5_1.bin 4baf70dd0d7c4247ba2b81fafd9c01005ac77c2f9ef064e00dcf195d0e2fdd2f
