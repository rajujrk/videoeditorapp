# Images

Every picture here is a screenshot taken by TechieVideoEditor itself (its `--screenshot`
option), in its dark and light themes. Nothing is generated, drawn or taken from elsewhere.

- `full/`: `docs/images/<name>-<theme>.png` converted to WebP, 1600 px wide.
- `focus/`: one per feature, the part of its screenshot that shows it (the focus box in
  `assets/features.js`), cropped and converted, at most 800 px wide.
- `map-editor-<theme>.webp`: the editor window with a 4K test clip, for the home page's map.

`tools/make_web_images.sh` rebuilds them all.
