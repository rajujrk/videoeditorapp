# Content tools

Built. The tools live in `tools/` rather than here:

- `tools/packtool` — publishes packs (templates, music and sounds, stickers, effects, transitions)
  into a site folder for the Store, writes the catalogue and verifies a signed one as the app will.
- `tools/packs/` — the signing key and catalogue signing for the store (see its README).
- `tools/make_looks.py`, `tools/make_stickers.py` — the generators for the built-in looks and
  stickers.
- Effects are packages of a manifest and GLSL shaders in `effects/`, checked against the shaders
  when the app loads them (see `effects/README.md`).

This folder only points there.
