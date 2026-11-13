#!/bin/sh
# Rebuilds the feature guide's images (web/images) from the app's own screenshots.
#
#   images/full/<name>-<theme>.webp       1600 px wide, the whole window, on a feature's page
#   images/focus/<feature>-<theme>.webp   800 px wide, the part showing the feature (its focus
#                                         box in web/assets/features.js), for cards and phones
#   images/map-editor-<theme>.webp        the editor window the home page's map is drawn on
#
# Sources: docs/images/<name>-<theme>.png (screenshots taken by the app with --screenshot); the
# map is a fresh screenshot of the editor. Needs cwebp and ffmpeg (Homebrew), Node to read
# features.js, and a built app for the map.
set -eu
cd "$(dirname "$0")/.."

out=web/images
mkdir -p "$out/full" "$out/focus"
tmp=$(mktemp -d)
trap 'rm -r "$tmp"' EXIT

# One line per feature: id image left top width height (percent).
node -e '
  global.window = {};
  require("./web/assets/features.js");
  const T = window.TVE;
  for (const f of T.features) {
    const box = f.focus || T.areas.find((a) => a.id === f.area).focus;
    console.log([f.id, f.image, ...box].join(" "));
  }' > "$tmp/features.txt"

for name in $(cut -d" " -f2 "$tmp/features.txt" | sort -u); do
  for theme in dark light; do
    src="docs/images/$name-$theme.png"
    [ -f "$src" ] || { echo "missing $src" >&2; exit 1; }
    cwebp -quiet -q 80 -resize 1600 0 "$src" -o "$out/full/$name-$theme.webp"
  done
done

while read -r id name x y w h; do
  for theme in dark light; do
    ffmpeg -v error -y -i "docs/images/$name-$theme.png" \
      -vf "crop=iw*$w/100:ih*$h/100:iw*$x/100:ih*$y/100,scale='min(800,iw)':-2:flags=lanczos" "$tmp/crop.png"
    cwebp -quiet -q 80 "$tmp/crop.png" -o "$out/focus/$id-$theme.webp"
  done
done < "$tmp/features.txt"

app=build/macos-release/app/TechieVideoEditor.app/Contents/MacOS/TechieVideoEditor
if [ -x "$app" ] && [ -f samples/4k30_h264.mp4 ]; then
  for theme in dark light; do
    TVE_AUTOSAVE_DIR="$tmp/autosave" TVE_WORKSPACES_FILE="$tmp/workspaces.json" \
      TVE_FAVOURITES_FILE="$tmp/favourites.json" TVE_KEYMAP_FILE="$tmp/keymap.json" \
      "$app" --theme "$theme" --effects tve.stylize.vignette --screenshot "$tmp/map-$theme.png" \
      --screenshot-at 4 --quit-after 5 samples/4k30_h264.mp4
    cwebp -quiet -q 82 -resize 1800 0 "$tmp/map-$theme.png" -o "$out/map-editor-$theme.webp"
  done
else
  echo "Skipped the map: build the app and run tools/make_test_media.sh first." >&2
fi
