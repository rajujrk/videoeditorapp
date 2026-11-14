#!/usr/bin/env bash
# Downloads the models the tests and development use into samples/models/ (git-ignored),
# from their official repositories, and checks each one's SHA-256. The app itself downloads
# models on first use (ui/player/SpeechModels.cpp); this is only for working on it.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
out="$root/samples/models"
mkdir -p "$out"

fetch() {
    local name="$1" sha="$2" url="$3"
    local file="$out/$name"
    if [[ -f "$file" ]] && [[ "$(shasum -a 256 "$file" | cut -d' ' -f1)" == "$sha" ]]; then
        echo "exists: $name"
        return
    fi
    echo "downloading: $name"
    curl -fL --retry 3 -o "$file.part" "$url"
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
fetch ggml-base.en-q5_1.bin 4baf70dd0d7c4247ba2b81fafd9c01005ac77c2f9ef064e00dcf195d0e2fdd2f \
    https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.en-q5_1.bin

# DeepFilterNet3 as ONNX (MIT / Apache-2.0): 7,983,136 bytes; also unpacked for the engine
# tests into deepfilternet3/ (the archive keeps its files under tmp/export/).
fetch DeepFilterNet3_onnx.tar.gz c94d91f70911001c946e0fabb4aa9adc37045f45a03b56008cb0c8244cb63616 \
    https://github.com/Rikorose/DeepFilterNet/raw/main/models/DeepFilterNet3_onnx.tar.gz
if [[ ! -f "$out/deepfilternet3/enc.onnx" ]]; then
    scratch="$(mktemp -d)"
    tar -xzf "$out/DeepFilterNet3_onnx.tar.gz" -C "$scratch"
    mkdir -p "$out/deepfilternet3"
    mv "$scratch"/tmp/export/{enc.onnx,erb_dec.onnx,df_dec.onnx,config.ini} "$out/deepfilternet3/"
    rm -rf "$scratch"
    echo "unpacked: deepfilternet3/"
fi

# Depth Anything V2 Small as Core ML (Apache-2.0), Apple's conversion at a fixed revision:
# 49,820,122 bytes in three files, into depth-anything-v2-small-f16/DepthAnythingV2SmallF16.mlpackage/.
depth="https://huggingface.co/apple/coreml-depth-anything-v2-small/resolve/cfef6f6f2a70783dedc0bfae40cecbc2052285d3/DepthAnythingV2SmallF16.mlpackage"
pkg="depth-anything-v2-small-f16/DepthAnythingV2SmallF16.mlpackage"
mkdir -p "$out/$pkg/Data/com.apple.CoreML/weights"
fetch "$pkg/Manifest.json" 2883ae290c48fe916dc5ececac03a7d847fa277165a49ef5652fa1d2b9cb55f7 "$depth/Manifest.json"
fetch "$pkg/Data/com.apple.CoreML/model.mlmodel" 44ac97a3efcfd52113183fb2862ff59cd0368e9ec2e30a90a54980dd11407042 \
    "$depth/Data/com.apple.CoreML/model.mlmodel"
fetch "$pkg/Data/com.apple.CoreML/weights/weight.bin" fa60d9b6a155734f59029ebb882fd54e549bfaee3539c1a9cbd2cbbab64a0fed \
    "$depth/Data/com.apple.CoreML/weights/weight.bin"
