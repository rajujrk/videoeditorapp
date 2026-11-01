# Publishing content packs

The app's **Store** tab installs packs — templates, music and sounds, stickers, effects and
transitions — from a static host. By default it reads
`https://rajujrk.github.io/videoeditorapp/packs/catalogue.json` (GitHub Pages of this
repository); the address can be changed in the Store tab.

Nothing is installed unless the catalogue is signed by a key the app trusts, the pack's
`pack.json` is the one the catalogue lists (SHA-256), and every file is the one `pack.json`
lists (size and SHA-256). A catalogue older than one already seen from the same address is
refused, so an old one can't be replayed to hide an update.

## 1. Make the signing key (once)

```sh
tools/packs/make_signing_key.sh ~/Keys/tve-packs
```

This writes `pack-signing-key.pem` there — the **private key**. Keep it safe and private,
outside this repository (the script refuses to put it inside). Anyone with it can sign
catalogues the app will trust. The script prints the public key's entry: add it to the
`keys` list in `effects/store/trusted-keys.json` and build the app. Until a key is there,
the Store says it can't check packs.

## 2. Make a pack

A pack is a folder whose sub-folders say what it holds:

| Folder | Holds |
|---|---|
| `sounds/` | a sound pack: `manifest.json` (`"format": "tve-sound-pack"`) and its WAV files |
| `templates/` | templates as `.tvet` folders (made with **Save as template…**) |
| `stickers/` | `stickers.json` (`{"stickers": [{"id", "title", "category", "file"}]}`) and Lottie files |
| `effects/` | one folder per effect: `manifest.json` and its compiled `.frag.qsb` shaders |
| `transitions/` | one folder per transition, the same way |

Effects and transitions from packs load when the app starts.

## 3. Publish into the site folder

The site folder is what the host serves beside `catalogue.json` (for GitHub Pages, the
`packs/` folder of the published site).

```sh
tve-packtool publish <pack folder> <site> --id studio.lofi --version 1 --title "Lo-fi beats" \
    --author "…" --license "…" --description "…" --preview preview.png
tve-packtool catalogue <site>
tools/packs/sign_catalogue.sh <site> ~/Keys/tve-packs/pack-signing-key.pem
tve-packtool verify <site> effects/store/trusted-keys.json
```

`tve-packtool` is built with the app (`build/macos-release/tools/tve-packtool`).
`publish` copies the pack into `<site>/<id>/<version>/` with its `pack.json`; a published
version never changes — publish a new version to update a pack. `catalogue` lists each pack's
newest version, with a sequence one more than the last catalogue's. `verify` checks the
signature and every pack's files the way the app will. Then upload the site folder as it is.

Each pack's licence is shown in the Store; publish only content you have the right to share.
