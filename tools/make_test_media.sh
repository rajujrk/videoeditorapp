#!/usr/bin/env bash
# Generates synthetic test videos in samples/ (git-ignored). Needs an `ffmpeg` CLI with
# VideoToolbox encoders (e.g. Homebrew's). The CLI is only a test-media generator; the
# app itself links the LGPL FFmpeg built by vcpkg.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
out="$root/samples"
mkdir -p "$out"

make() {
    local name="$1"; shift
    if [[ -f "$out/$name" ]]; then
        echo "exists: $name"
        return
    fi
    echo "creating: $name"
    ffmpeg -hide_banner -loglevel error -y "$@" "$out/$name"
}

# Short clips for unit tests (60 frames each).
make test_1080p30_h264.mp4 -f lavfi -i testsrc2=size=1920x1080:rate=30 -frames:v 60 \
    -c:v h264_videotoolbox -b:v 8M -pix_fmt yuv420p
make test_720p30_mjpeg.mov -f lavfi -i testsrc2=size=1280x720:rate=30 -frames:v 60 \
    -c:v mjpeg -q:v 4 -pix_fmt yuvj420p

# 20-second 4K clips for the playback spike.
make 4k30_h264.mp4 -f lavfi -i testsrc2=size=3840x2160:rate=30 -t 20 \
    -c:v h264_videotoolbox -b:v 60M -pix_fmt yuv420p
make 4k60_h264.mp4 -f lavfi -i testsrc2=size=3840x2160:rate=60 -t 20 \
    -c:v h264_videotoolbox -b:v 100M -pix_fmt yuv420p
make 4k60_hevc_10bit.mp4 -f lavfi -i testsrc2=size=3840x2160:rate=60 -t 20 \
    -c:v hevc_videotoolbox -profile:v main10 -b:v 80M -pix_fmt p010le -tag:v hvc1

echo "done: $out"
