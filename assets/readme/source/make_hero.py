#!/usr/bin/env python3
"""The README's hero: TechieVideoEditor's own timeline. The name is a title clip, the
features are clips on the video track, the music is a waveform, and the red playhead and
timecode are the app's.

    python3 assets/readme/source/make_hero.py          # assets/readme/hero.svg (still)
    python3 assets/readme/source/make_hero.py --gif    # and assets/readme/hero.gif (the
                                                       # playhead playing through the clips)

The still and every frame of the animation come from the same drawing. Frames are rendered
with macOS Quick Look (WebKit, as a browser draws SVG) and encoded with ffmpeg.
"""
import math
import pathlib
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[3]
OUT = ROOT / "assets" / "readme"

W, H = 1200, 444
SANS = "-apple-system, BlinkMacSystemFont, 'Segoe UI', 'Helvetica Neue', Arial, sans-serif"
MONO = "ui-monospace, SFMono-Regular, Menlo, Consolas, monospace"

# The app's dark theme (ui/theme/Theme.qml).
CANVAS, SURFACE, RAISED, BORDER = "#0F1115", "#171A20", "#1E222A", "#262B34"
TEXT, MUTED, FAINT = "#E8EAEE", "#A3AAB6", "#6B7380"
VIDEO, AUDIO, TITLE, PLAYHEAD = "#3D6FD9", "#2FA77A", "#A35CD1", "#FF5A5F"

LEFT, RIGHT = 64, 1136
SECONDS = 25.0                      # the ruler's span
RULER = 174                         # the ruler's line
BADGE = 112                         # the playhead's timecode
FPS = 25

TITLE_LANE = (190, 270)
VIDEO_LANE = (286, 338)
AUDIO_LANE = (354, 390)
FEATURES = ["Edit", "Grade", "Titles", "Captions", "Track", "Record", "Export"]


def x_at(seconds):
    return LEFT + (RIGHT - LEFT) * seconds / SECONDS


def timecode(x):
    frames = round((x - LEFT) / (RIGHT - LEFT) * SECONDS * FPS)
    frames = max(0, frames)
    s, f = divmod(frames, FPS)
    m, s = divmod(s, 60)
    return f"00:{m:02d}:{s:02d}:{f:02d}"


def feature_clips():
    clips, x = [], LEFT
    for word in FEATURES:
        width = 34 + len(word) * 13.2
        clips.append((x, x + width, word))
        x += width + 6
    return clips


TITLE_CLIP = (LEFT, 760)
AUDIO_CLIP = (LEFT, 1052)


def waveform(x0, x1, top, bottom):
    mid = (top + bottom) / 2
    half = (bottom - top) / 2 - 4
    bars = []
    x = x0 + 10
    i = 0
    while x < x1 - 10:
        # Music-like: a kick every eight bars that decays, phrases that swell and drop,
        # and some unevenness from bar to bar.
        kick = math.exp(-(i % 8) / 2.5)
        phrase = 0.45 + 0.55 * abs(math.sin(i / 19.0))
        detail = 0.55 + 0.45 * abs(math.sin(i * 2.3) * math.cos(i * 0.7))
        h = max(2.0, half * min(1.0, (0.35 + 0.65 * kick) * phrase * (0.6 + 0.6 * detail)))
        bars.append(f'<rect x="{x:.1f}" y="{mid - h:.1f}" width="3" height="{2 * h:.1f}" rx="1.5"/>')
        x += 6
        i += 1
    return "\n      ".join(bars)


def svg(playhead, radius=24):
    """The whole hero with the playhead at x = playhead (corners rounded by radius)."""
    t = timecode(playhead)
    # The timecode rides on the playhead, as when scrubbing (kept inside the ruler).
    badge = min(max(playhead - BADGE / 2, LEFT), RIGHT - BADGE)

    def selected(x0, x1):
        return x0 <= playhead <= x1

    ruler = []
    for s in range(0, int(SECONDS) + 1):
        x = x_at(s)
        major = s % 5 == 0
        ruler.append(f'<rect x="{x - 0.5:.1f}" y="{RULER - (14 if major else 6)}" width="1" height="{14 if major else 6}" fill="{FAINT if major else BORDER}"/>')
        if major and s < SECONDS:
            ruler.append(f'<text x="{x + 6:.1f}" y="{RULER - 5}" font-family="{MONO}" font-size="15" fill="{FAINT}">00:{s:02d}</text>')

    video = []
    for x0, x1, word in feature_clips():
        on = selected(x0, x1)
        stroke = f' stroke="#FFFFFF" stroke-width="2"' if on else ""
        video.append(
            f'<rect x="{x0:.1f}" y="{VIDEO_LANE[0]}" width="{x1 - x0:.1f}" height="{VIDEO_LANE[1] - VIDEO_LANE[0]}" rx="7" fill="{VIDEO}"{stroke}/>'
            f'<text x="{x0 + 17:.1f}" y="{VIDEO_LANE[0] + 34}" font-family="{SANS}" font-size="22" font-weight="600" fill="#FFFFFF">{word}</text>')

    title_on = selected(*TITLE_CLIP)
    audio_on = selected(*AUDIO_CLIP)
    return f'''<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" role="img" aria-labelledby="title desc">
  <title id="title">TechieVideoEditor</title>
  <desc id="desc">A video editor for macOS on Apple silicon, drawn as its own timeline: the name is a title clip, Edit, Grade, Titles, Captions, Track, Record and Export are clips on the video track, music runs below, and the red playhead stands at {t}.</desc>
  <defs>
    <clipPath id="frame"><rect width="{W}" height="{H}" rx="{radius}"/></clipPath>
  </defs>
  <g clip-path="url(#frame)">
    <rect width="{W}" height="{H}" fill="{CANVAS}"/>

    <g id="heading">
      <text x="{LEFT}" y="58" font-family="{MONO}" font-size="17" letter-spacing="1.6" fill="{MUTED}">VIDEO EDITOR · macOS · APPLE SILICON</text>
      <text x="{LEFT}" y="100" font-family="{SANS}" font-size="27" fill="{TEXT}">Edit, grade, caption and record video, natively on your Mac.</text>
    </g>

    <g id="ruler">
      <rect x="{LEFT}" y="{RULER}" width="{RIGHT - LEFT}" height="1" fill="{BORDER}"/>
      {"".join(ruler)}
    </g>

    <g id="lanes" fill="{SURFACE}">
      <rect x="{LEFT}" y="{TITLE_LANE[0] - 4}" width="{RIGHT - LEFT}" height="{TITLE_LANE[1] - TITLE_LANE[0] + 8}" rx="10"/>
      <rect x="{LEFT}" y="{VIDEO_LANE[0] - 4}" width="{RIGHT - LEFT}" height="{VIDEO_LANE[1] - VIDEO_LANE[0] + 8}" rx="10"/>
      <rect x="{LEFT}" y="{AUDIO_LANE[0] - 4}" width="{RIGHT - LEFT}" height="{AUDIO_LANE[1] - AUDIO_LANE[0] + 8}" rx="10"/>
    </g>

    <g id="title-clip">
      <rect x="{TITLE_CLIP[0]}" y="{TITLE_LANE[0]}" width="{TITLE_CLIP[1] - TITLE_CLIP[0]}" height="{TITLE_LANE[1] - TITLE_LANE[0]}" rx="9" fill="{TITLE}"{' stroke="#FFFFFF" stroke-width="2"' if title_on else ""}/>
      <text x="{TITLE_CLIP[0] + 18}" y="{TITLE_LANE[0] + 24}" font-family="{MONO}" font-size="15" fill="#F3E8FA" fill-opacity="0.85">Title</text>
      <text x="{TITLE_CLIP[0] + 16}" y="{TITLE_LANE[0] + 70}" font-family="{SANS}" font-size="54" font-weight="800" letter-spacing="-0.5" fill="#FFFFFF">TechieVideoEditor</text>
    </g>

    <g id="video-clips">
      {"".join(video)}
    </g>

    <g id="audio-clip">
      <rect x="{AUDIO_CLIP[0]}" y="{AUDIO_LANE[0]}" width="{AUDIO_CLIP[1] - AUDIO_CLIP[0]}" height="{AUDIO_LANE[1] - AUDIO_LANE[0]}" rx="7" fill="{AUDIO}"{' stroke="#FFFFFF" stroke-width="2"' if audio_on else ""}/>
      <g fill="#1B6B4E">
      {waveform(*AUDIO_CLIP, *AUDIO_LANE)}
      </g>
    </g>

    <g id="footer" font-family="{MONO}" font-size="16" fill="{FAINT}">
      <text x="{LEFT}" y="424">C++20 · Qt Quick · Metal · on-device AI · your own keys for the cloud</text>
      <text x="{RIGHT}" y="424" text-anchor="end">11 languages</text>
    </g>

    <g id="playhead">
      <rect x="{badge:.1f}" y="118" width="{BADGE}" height="24" rx="6" fill="{PLAYHEAD}"/>
      <text x="{badge + BADGE / 2:.1f}" y="135" text-anchor="middle" font-family="{MONO}" font-size="15" font-weight="700" fill="#FFFFFF">{t}</text>
      <rect x="{playhead - 1:.1f}" y="142" width="2" height="{AUDIO_LANE[1] + 6 - 142}" fill="{PLAYHEAD}"/>
      <path d="M{playhead - 7:.1f} 142 h14 l-7 8 z" fill="{PLAYHEAD}"/>
    </g>
  </g>
</svg>
'''


def ease(t):
    return t * t * (3 - 2 * t)


def positions():
    """The playhead, frame by frame: a moment at the start, playing through the clips, a
    moment at the end, then scrubbed back so the loop closes on its first frame."""
    start, end = 71.0, 1040.0
    spans = [(0.8, None), (3.2, (start, end)), (1.0, None), (0.8, (end, start))]
    xs, at = [], start
    for seconds, move in spans:
        n = round(seconds * FPS)
        for i in range(n):
            if move:
                a, b = move
                at = a + (b - a) * ease((i + 1) / n)
            xs.append(round(at, 1))
    return xs


# Quick Look draws SVG into a viewport about 770 points wide (cropping the rest) at twice
# its size: each frame is declared 750 wide, drawn at 1500 and scaled to 1200 when encoding.
QL_WIDTH = 750
QL_PIXELS = 2 * QL_WIDTH
QL_CROP = f"crop={QL_PIXELS}:{QL_PIXELS * H // W}:0:0,scale={W}:{H}:flags=lanczos"


def render(svg_text, png_path, workdir):
    source = workdir / (png_path.stem + ".svg")
    source.write_text(svg_text.replace(f'width="{W}" height="{H}" viewBox',
                                       f'width="{QL_WIDTH}" height="{QL_WIDTH * H / W}" viewBox', 1),
                      encoding="utf-8")
    subprocess.run(["qlmanage", "-t", "-s", str(QL_PIXELS), "-o", str(workdir), str(source)],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    shutil.move(str(workdir / (source.name + ".png")), png_path)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "hero.svg").write_text(svg(840.0), encoding="utf-8")
    print("wrote", OUT / "hero.svg")
    if "--gif" not in sys.argv:
        return
    for tool in ("qlmanage", "ffmpeg"):
        if not shutil.which(tool):
            sys.exit(f"{tool} is needed for the GIF")
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        frames = tmp / "frames"
        frames.mkdir()
        rendered = {}
        for index, x in enumerate(positions()):
            if x not in rendered:
                rendered[x] = tmp / f"at-{len(rendered):04d}.png"
                render(svg(x, radius=0), rendered[x], tmp)
            shutil.copy(rendered[x], frames / f"{index:04d}.png")
        # Opaque, square-cornered frames: the GIF then keeps only what changes from one
        # frame to the next (the playhead's strip), which transparency would prevent.
        subprocess.run([
            "ffmpeg", "-v", "error", "-y", "-framerate", str(FPS), "-i", str(frames / "%04d.png"),
            "-filter_complex",
            f"[0]{QL_CROP},split[a][b];[a]palettegen=max_colors=192:stats_mode=full[p];"
            f"[b][p]paletteuse=dither=none:diff_mode=rectangle",
            "-loop", "0", str(OUT / "hero.gif")], check=True)
    print("wrote", OUT / "hero.gif", f"({(OUT / 'hero.gif').stat().st_size / 1e6:.2f} MB)")


if __name__ == "__main__":
    main()
