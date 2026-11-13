# TVE feature guide

A static website listing every TechieVideoEditor feature: what it does, a screenshot, where to
find it in the app and the steps to use it. Plain HTML, CSS and JavaScript with no build step
and no backend.

- `index.html`: the editor window as a map; choose an area to see its features.
- `features.html`: every feature by topic, with search and area filters.
- `feature.html#<id>`: one feature.
- `shortcuts.html` and `start.html`: keyboard shortcuts, and building and running the app.

Open `index.html` straight from the folder, or serve the folder from any static host (GitHub
Pages included); every link is relative.

The features live in `assets/features.js`, written from the project's README; when a feature
changes, change both. Images are rebuilt with `tools/make_web_images.sh` (see `images/README.md`).
