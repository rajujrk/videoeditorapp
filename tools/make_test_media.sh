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

# Audio test media (48 kHz stereo, exact amplitudes so mixing can be checked numerically).
make test_tone_440.wav -f lavfi -i "aevalsrc=0.5*sin(2*PI*440*t)|0.5*sin(2*PI*440*t):s=48000:d=3" \
    -c:a pcm_s16le
# 10 ms full-scale bursts starting exactly at 0.5 s, 1.5 s and 2.5 s: timing markers.
make test_clicks.wav -f lavfi -i "aevalsrc=if(between(mod(t\,1)\,0.5\,0.51)\,0.9\,0)|if(between(mod(t\,1)\,0.5\,0.51)\,0.9\,0):s=48000:d=3" \
    -c:a pcm_s16le
# Video with sound: 2 s of test pattern plus a 1 kHz tone.
make test_av_1080p30.mp4 -f lavfi -i testsrc2=size=1920x1080:rate=30 \
    -f lavfi -i "aevalsrc=0.5*sin(2*PI*1000*t)|0.5*sin(2*PI*1000*t):s=48000:d=2" \
    -frames:v 60 -c:v h264_videotoolbox -b:v 8M -pix_fmt yuv420p -c:a aac -b:a 192k -shortest

# 20-second 4K clips for the playback spike.
make 4k30_h264.mp4 -f lavfi -i testsrc2=size=3840x2160:rate=30 -t 20 \
    -c:v h264_videotoolbox -b:v 60M -pix_fmt yuv420p
make 4k60_h264.mp4 -f lavfi -i testsrc2=size=3840x2160:rate=60 -t 20 \
    -c:v h264_videotoolbox -b:v 100M -pix_fmt yuv420p
make 4k60_hevc_10bit.mp4 -f lavfi -i testsrc2=size=3840x2160:rate=60 -t 20 \
    -c:v hevc_videotoolbox -profile:v main10 -b:v 80M -pix_fmt p010le -tag:v hvc1

# Short 10-bit HEVC (P010 frames) for code that converts decoded pictures, e.g. proxies.
make test_hevc10_1080p30.mp4 -f lavfi -i testsrc2=size=1920x1080:rate=30 -frames:v 60 \
    -c:v hevc_videotoolbox -profile:v main10 -b:v 8M -pix_fmt p010le -tag:v hvc1

# Footage that looks unlike the test pattern, so transitions between the two are visible
# (screenshots, manual checks): 10 s of slowly moving colour gradients.
make gradients_1080p30_h264.mp4 -f lavfi -i gradients=size=1920x1080:rate=30:speed=0.02:nb_colors=4 -t 10 \
    -c:v h264_videotoolbox -b:v 12M -pix_fmt yuv420p

# An animated GIF (20 frames, 2 s): imported as video, unlike a still.
make test_anim.gif -f lavfi -i testsrc2=size=320x240:rate=10 -t 2

echo "done: $out"
