#!/usr/bin/env bash
# Makes build/dmg/TechieVideoEditor-<version>.dmg: the app with everything it needs inside it
# (Qt's frameworks and QML modules; the rest is linked in), the licences of what it is made
# from, a link to Applications to drag it onto, and a read-me.
#
#   tools/make_dmg.sh
#
# It builds the macos-dist preset first: Apple Silicon, the app and its libraries for macOS
# 14.4 and later (the first run builds the libraries for that macOS, which takes a while).
# Qt's frameworks are copied in as they are, so the macOS they were built for sets the oldest
# the app opens on (written into its Info.plist and the read-me): Homebrew's are built for the
# macOS of the Mac that installed them; Qt's own installer's (qt.io) for much older ones —
# point QT_PREFIX at that Qt (e.g. ~/Qt/6.12.0/macos) to use it. Needs VCPKG_ROOT, Qt and the
# Xcode command line tools.
#
# Signing. Without anything set, the app is signed ad hoc: it runs on any Apple Silicon Mac,
# but macOS asks the person opening it to allow it once (Privacy & Security › Open Anyway),
# because Apple hasn't checked it. To have it open like any app from the web, sign it with
# an Apple Developer ID and have Apple notarize it:
#
#   TVE_SIGN_IDENTITY="Developer ID Application: Your Name (TEAMID)" \
#   TVE_NOTARY_PROFILE=<a profile saved with `xcrun notarytool store-credentials`> \
#   tools/make_dmg.sh
#
# The identity comes from your keychain and the notary profile holds your Apple ID's
# app-specific password; this script never sees or stores either.
set -euo pipefail

cd "$(dirname "$0")/.."
root="$PWD"
name=TechieVideoEditor
build="$root/build/macos-dist"
out="$root/build/dmg"
identity="${TVE_SIGN_IDENTITY:-}"
notary="${TVE_NOTARY_PROFILE:-}"

step() { printf '\n== %s\n' "$*"; }
fail() { printf 'make_dmg: %s\n' "$*" >&2; exit 1; }

[ -n "${VCPKG_ROOT:-}" ] || fail "set VCPKG_ROOT (see the README's Requirements)"
command -v brew > /dev/null || fail "Homebrew is needed (for Qt, CMake and Ninja)"
# Homebrew's tools first: some libraries build with meson, which needs a newer Python than
# the one macOS has in /usr/bin.
PATH="$(brew --prefix)/bin:$PATH"
qt="${QT_PREFIX:-$(brew --prefix qt 2>/dev/null)}" || fail "Qt is needed (brew install qt, or set QT_PREFIX)"
macdeployqt="$qt/bin/macdeployqt"
qml="$qt/share/qt/qml"           # Homebrew's layout
[ -d "$qml" ] || qml="$qt/qml"   # Qt's installer's
[ -x "$macdeployqt" ] || fail "no macdeployqt in $qt/bin"
[ -n "$notary" ] && [ -z "$identity" ] && fail "notarizing needs TVE_SIGN_IDENTITY too"

step "Building (macos-dist: Apple Silicon, macOS 14.4 and later)"
if [ ! -f "$build/CMakeCache.txt" ]; then
  cmake --preset macos-dist -DCMAKE_PREFIX_PATH="$qt"
fi
cmake --build --preset macos-dist
built="$build/app/$name.app"
[ -d "$built" ] || fail "the build made no $built"
version="$(plutil -extract CFBundleShortVersionString raw "$built/Contents/Info.plist")"
dmg="$out/$name-$version.dmg"

step "Putting Qt into the app"
rm -rf "$out"
mkdir -p "$out/stage"
app="$out/stage/$name.app"
ditto "$built" "$app"
# The build's qt.conf points at the build folder; macdeployqt writes the app's own.
rm -f "$app/Contents/Resources/qt.conf"
"$macdeployqt" "$app" \
  -qmldir="$root/app" -qmldir="$root/ui" \
  -qmlimport="$qml" \
  -executable="$app/Contents/MacOS/tve-pluginscan" \
  -no-codesign -always-overwrite -verbose=1

step "Checking nothing points outside the app"
machos=()
while IFS= read -r -d '' file; do
  if file -b "$file" | grep -q "Mach-O"; then
    machos+=("$file")
  fi
done < <(find "$app/Contents" -type f -print0)
outside=0
for file in "${machos[@]}"; do
  # (A library's own name comes first; it doesn't decide where anything is loaded from.)
  own="$(otool -D "$file" | tail -n +2)"
  while read -r dep; do
    [ "$dep" = "$own" ] && continue
    case "$dep" in
      /System/*|/usr/lib/*|@rpath/*|@executable_path/*|@loader_path/*) ;;
      *) echo "  ${file#"$app"/} uses $dep"; outside=1 ;;
    esac
  done < <(otool -L "$file" | tail -n +2 | awk '{print $1}')
done
[ "$outside" = 0 ] || fail "the files above would not open on another Mac"
echo "  ${#machos[@]} programs and libraries, all inside the app"
# The oldest macOS it opens on is the newest any part of it needs. The app and the libraries
# linked into it are built for 14.4; Homebrew builds Qt's frameworks for the macOS it runs
# on, so with Homebrew's Qt that decides (a Qt built for older Macs lowers it).
minimum=14.4
newest=""
for file in "${machos[@]}"; do
  minos="$(otool -l "$file" | awk '/LC_BUILD_VERSION/{found=1} found && /minos/{print $2; exit}')"
  if [ -n "$minos" ] && [ "$(printf '%s\n%s\n' "$minos" "$minimum" | sort -V | tail -1)" != "$minimum" ]; then
    minimum="$minos"
    newest="${file#"$app"/}"
  fi
done
plutil -replace LSMinimumSystemVersion -string "$minimum" "$app/Contents/Info.plist"
echo "  it opens on macOS $minimum and later${newest:+ (set by $newest)}"
if [ "$minimum" != 14.4 ]; then
  echo "  (for older Macs, build with a Qt made for macOS 14.4: see the README's \"Make a disk image\")"
fi

step "Licences"
licences="$app/Contents/Resources/Licenses"
mkdir -p "$licences/libraries"
cp "$root/licenses/"*.txt "$licences/"
# The libraries linked into the app (vcpkg's record of each one's licence).
installed="$root/build/vcpkg_installed_dist/arm64-osx-dist/share"
for dir in "$installed"/*/; do
  package="$(basename "$dir")"
  case "$package" in vcpkg-*|pkgconf|catch2) continue ;; esac
  [ -f "$dir/copyright" ] && cp "$dir/copyright" "$licences/libraries/$package.txt"
done
# The libraries macdeployqt copied in from Homebrew (Qt's own dependencies), by the licence
# files their Homebrew formula keeps.
for lib in "$app/Contents/Frameworks/"*.dylib; do
  [ -e "$lib" ] || continue
  formula="$(brew list --formula 2>/dev/null | while read -r f; do
    [ -e "$(brew --prefix "$f")/lib/$(basename "$lib")" ] && { echo "$f"; break; }; done)"
  [ -n "$formula" ] || continue
  cellar="$(brew --cellar "$formula")"
  for licence in "$cellar"/*/{LICENSE,LICENCE,COPYING,NOTICE,COPYRIGHT}*; do
    [ -f "$licence" ] && cp "$licence" "$licences/libraries/$formula-$(basename "$licence").txt"
  done
done
qt_version="$("$qt/bin/qtpaths" --qt-version 2>/dev/null || echo 6)"
cat > "$licences/README.txt" <<EOF
$name is made with these, under their licences:

- Qt $qt_version (the frameworks in Contents/Frameworks, unchanged) — GNU LGPL 3.0
  (LGPL-3.0.txt, with GPL-3.0.txt). Its source: https://download.qt.io/official_releases/qt/
- FFmpeg and LAME, linked into the app — GNU LGPL 2.1 or later (LGPL-2.1.txt). The app's
  own source and how to build it, with these libraries or your own changes to them, are at
  https://github.com/rajujrk/videoeditorapp
- The other libraries in libraries/ — each under the licence in its file (MIT, BSD, Apache 2.0
  and similar).

The models the app offers to download (Whisper, DeepFilterNet3, Depth Anything V2 Small) are
not part of it; each comes from its makers under its own licence (MIT, MIT / Apache 2.0,
Apache 2.0).
EOF
echo "  $(find "$licences" -type f | wc -l | tr -d ' ') files in Contents/Resources/Licenses"

step "Signing"
if [ -n "$identity" ]; then
  sign=(codesign --force --timestamp --options runtime --sign "$identity")
  echo "  with $identity (hardened runtime)"
else
  sign=(codesign --force --sign -)
  echo "  ad hoc (set TVE_SIGN_IDENTITY to sign with a Developer ID)"
fi
# Inside out: libraries and plug-ins, the frameworks, the helper, then the app.
for file in "${machos[@]}"; do
  case "$file" in
    "$app/Contents/MacOS/"*|*.framework/Versions/*/*) ;;
    *) "${sign[@]}" "$file" ;;
  esac
done
for framework in "$app/Contents/Frameworks/"*.framework; do
  [ -d "$framework" ] && "${sign[@]}" "$framework"
done
"${sign[@]}" --entitlements "$root/app/$name.entitlements" "$app/Contents/MacOS/tve-pluginscan"
"${sign[@]}" --entitlements "$root/app/$name.entitlements" "$app"
codesign --verify --deep --strict "$app"
echo "  verified"

step "Making the disk image"
ln -s /Applications "$out/stage/Applications"
unchecked=""
if [ -z "$notary" ]; then
  unchecked="
  The first time, macOS may say it can't check the app for malicious software (it isn't
  notarized by Apple). To open it anyway: try to open it once, then open System Settings ›
  Privacy & Security, scroll down to the message about $name and click \"Open Anyway\"
  (on older macOS you can also Control-click the app and choose Open)."
fi
cat > "$out/stage/Read me first.txt" <<EOF
$name $version — a video editor for Macs with Apple silicon (M1 or later), macOS $minimum or later.

Installing
  Drag $name onto Applications, then open it from there.
$unchecked

The first time it opens
  It offers the models that let captions, speech cleanup and depth of field work on your
  Mac — downloaded once from their makers (about 120 MB) and checked before they are used.
  You can skip this and get them later from "On-device models…" on the Home screen.

Projects
  $name projects (.tve) open in the app when you double-click them in Finder. Videos,
  sounds and pictures can be opened with it too (Open With, or drop them on its Dock icon).

AI with your own keys
  The cloud features (Claude, OpenAI, Google, ElevenLabs) are off until you add a key of
  your own in AI settings; it is kept in your Mac's keychain.

Licences: inside the app, in Contents/Resources/Licenses.
EOF
hdiutil create -quiet -volname "$name $version" -srcfolder "$out/stage" -fs HFS+ -format UDZO -imagekey zlib-level=9 -ov "$dmg"
if [ -n "$identity" ]; then
  codesign --force --timestamp --sign "$identity" "$dmg"
fi

if [ -n "$notary" ]; then
  step "Notarizing (Apple checks it; usually a few minutes)"
  xcrun notarytool submit "$dmg" --keychain-profile "$notary" --wait
  xcrun stapler staple "$dmg"
  spctl --assess --type open --context context:primary-signature -v "$dmg"
fi

rm -rf "$out/stage"
step "Done"
echo "  $dmg ($(du -h "$dmg" | cut -f1)), SHA-256 $(shasum -a 256 "$dmg" | cut -d' ' -f1)"
