---
version: 1
slug: "web-index-html"
primary_target: "web/index.html"
related_targets: ["web/features.html","web/feature.html","web/shortcuts.html","web/start.html"]
---

# Surface: TVE feature guide (web/)

Scope: static guide in `web/` (index map, all features, feature pages, shortcuts, getting started). Mode: Read.
Audience: people the author shares TVE with, app open beside the guide. Job: find a feature, learn where it lives and the steps.
Content: every feature in README.md, with the app's own dark/light screenshots. Constraints: plain HTML/CSS/JS, works from file:// and a GitHub Pages sub-path, English only, nothing the README does not say.

## Direction contract

THESIS: The guide is organised by where things live in the editor window. The home page is the editor itself with numbered areas; it refuses the category default of a marketing hero over a generic feature grid.

OWN-WORLD: TVE's own design system: canvas #0F1115 / surface #171A20 / sunken #0B0D10, light twin #F3F4F7 / #FFFFFF, one Techie blue accent (#356AE6 fill, #6E95FF text), SF system type with Menlo for paths and keys, 8 px radii, 1 px subtle borders, numbered blue pins with a soft ring.

STORY: Readers see the real window, understand its five areas, pick one, scan its features as cards, open one to read what it does, where it is (a breadcrumb like Inspector > Color), and numbered steps with keys.

FIRST VIEWPORT: Left two-thirds: title "Where everything is", one-line lede, then the current editor screenshot with pins 1–5 on top bar, library, player, inspector, timeline; hovering or choosing a pin outlines that area. Right column: legend of areas with counts (plus Home screen and Menus & keys). Below the fold edge: the chosen area's feature cards.

FORM: Find it by where it lives (annotated app map), dealt structure 3 of 7. seed:0d684de24d0520e1

Signature interaction: pin and legend are one control; choosing an area highlights its region on the screenshot, updates the URL hash, and filters the cards, with a 200 ms outline transition (none under reduced motion).

FINISH: unreviewed and undocumented is unfinished; this build ends with the finish review, the verdict, DESIGN.md, and every shipping raster carrying its provenance
