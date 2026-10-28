#!/usr/bin/env python3
"""Generates TechieVideoEditor's built-in stickers: original vector designs as Lottie
(Bodymovin) animations, each design in four looping motions, plus the catalog the Stickers
library reads (effects/stickers/stickers.json).

    python3 tools/make_stickers.py effects/stickers
"""
import json
import math
import os
import random
import sys

SIZE = 512
FPS = 30
FRAMES = 60  # a two-second loop


def rgb(hex_colour):
    h = hex_colour.lstrip("#")
    return [round(int(h[i:i + 2], 16) / 255, 4) for i in (0, 2, 4)] + [1]


def static(value):
    return {"a": 0, "k": value}


# --- Shapes (centred on 0, 0; the layer sits in the middle of the 512 × 512 frame) ---

def ellipse(cx, cy, w, h):
    return {"ty": "el", "p": static([cx, cy]), "s": static([w, h])}


def rect(cx, cy, w, h, r=0):
    return {"ty": "rc", "p": static([cx, cy]), "s": static([w, h]), "r": static(r)}


def star(cx, cy, points, outer, inner, rotation=0, polygon=False):
    shape = {"ty": "sr", "sy": 2 if polygon else 1, "p": static([cx, cy]), "pt": static(points),
             "or": static(outer), "os": static(0), "r": static(rotation)}
    if not polygon:
        shape.update({"ir": static(inner), "is": static(0)})
    return shape


def path(vertices, closed=True, ins=None, outs=None):
    n = len(vertices)
    return {"ty": "sh", "ks": static({"c": closed, "v": vertices, "i": ins or [[0, 0]] * n,
                                       "o": outs or [[0, 0]] * n})}


def fill(colour):
    return {"ty": "fl", "c": static(rgb(colour)), "o": static(100), "r": 1}


def stroke(colour, width):
    return {"ty": "st", "c": static(rgb(colour)), "o": static(100), "w": static(width), "lc": 2, "lj": 2}


def group(items, paint, x=0, y=0, rotation=0):
    transform = {"ty": "tr", "p": static([x, y]), "a": static([0, 0]), "s": static([100, 100]),
                 "r": static(rotation), "o": static(100)}
    return {"ty": "gr", "it": items + paint + [transform]}


# --- Designs: name, title, category, groups (back to front as listed: first drawn on top) ---

def heart_path(scale=1.0):
    v = [[0, -50], [180, -40], [0, 190], [-180, -40]]
    i = [[-20, -110], [0, -130], [110, -70], [0, 90]]
    o = [[20, -110], [0, 90], [-110, -70], [0, -130]]
    s = lambda pts: [[p[0] * scale, p[1] * scale] for p in pts]
    return path(s(v), True, s(i), s(o))


def face(eyes, mouth):
    return [*eyes, mouth, group([ellipse(0, 0, 400, 400)], [fill("#FFD166"), stroke("#F4A261", 10)])]


def confetti():
    rng = random.Random(7)
    colours = ["#FF006E", "#FFBE0B", "#3A86FF", "#8338EC", "#06D6A0", "#FB5607"]
    pieces = []
    for k in range(16):
        a = rng.uniform(0, 2 * math.pi)
        r = rng.uniform(40, 200)
        pieces.append(group([rect(0, 0, rng.choice([26, 34]), rng.choice([60, 70]), 6)], [fill(colours[k % len(colours)])],
                            x=round(math.cos(a) * r), y=round(math.sin(a) * r), rotation=rng.randint(0, 180)))
    return pieces


DESIGNS = [
    ("star", "Star", "shapes", [group([star(0, 0, 5, 200, 85)], [fill("#FFC93C"), stroke("#E8961E", 10)])]),
    ("heart", "Heart", "shapes", [group([heart_path()], [fill("#FF4D6D"), stroke("#C9184A", 8)])]),
    ("sparkle", "Sparkle", "shapes", [group([star(140, -140, 4, 70, 18)], [fill("#FFF3B0")]),
                                       group([star(0, 0, 4, 210, 50)], [fill("#FFE066"), stroke("#F7B801", 6)])]),
    ("burst", "Burst", "shapes", [group([ellipse(0, 0, 300, 300)], [fill("#FFD23F")]),
                                   group([star(0, 0, 12, 215, 165)], [fill("#FF7A00")])]),
    ("gem", "Gem", "shapes", [group([path([[-150, -60], [150, -60]], False), path([[-60, -60], [0, 200], [60, -60]], False)],
                                     [stroke("#E0FBFC", 8)]),
                               group([path([[-90, -190], [90, -190], [160, -60], [0, 200], [-160, -60]])],
                                     [fill("#5BC0EB"), stroke("#3D8EB9", 8)])]),
    ("ring", "Highlight Ring", "shapes", [group([ellipse(0, 0, 380, 330)], [stroke("#FF3B3B", 22)], rotation=-8)]),
    ("arrow_right", "Arrow", "marks",
     [group([path([[-190, -50], [40, -50], [40, -130], [205, 0], [40, 130], [40, 50], [-190, 50]])],
            [fill("#3A86FF"), stroke("#1F5FCC", 8)])]),
    ("arrow_down", "Arrow Down", "marks",
     [group([path([[50, -190], [50, 40], [130, 40], [0, 205], [-130, 40], [-50, 40], [-50, -190]])],
            [fill("#8338EC"), stroke("#5A1FB0", 8)])]),
    ("check", "Check", "marks", [group([path([[-150, 10], [-40, 120], [160, -120]], False)], [stroke("#2EC4B6", 52)])]),
    ("cross", "Cross", "marks", [group([path([[-130, -130], [130, 130]], False), path([[130, -130], [-130, 130]], False)],
                                       [stroke("#E63946", 52)])]),
    ("exclamation", "Exclamation", "marks", [group([rect(0, -50, 64, 230, 32), ellipse(0, 145, 74, 74)], [fill("#FFB703")])]),
    ("question", "Question", "marks",
     [group([ellipse(0, 155, 64, 64)], [fill("#219EBC")]),
      group([path([[-95, -75], [0, -170], [95, -80], [0, 20], [0, 75]], False,
                  [[0, 0], [-60, 0], [0, -55], [0, -40], [0, 0]], [[0, -60], [60, 0], [0, 55], [0, 0], [0, 0]])],
            [stroke("#219EBC", 44)])]),
    ("pin", "Map Pin", "marks", [group([ellipse(0, -55, 110, 110)], [fill("#FFFFFF")]),
                                  group([ellipse(0, -55, 280, 280), path([[-122, 10], [0, 215], [122, 10]])],
                                        [fill("#E63946")])]),
    ("swoosh", "Swoosh", "marks", [group([path([[-200, 30], [0, -5], [200, 25]], False, [[0, 0], [-90, 0], [-80, 25]],
                                               [[80, -25], [90, 0], [0, 0]])], [stroke("#FF006E", 40)])]),
    ("sun", "Sun", "nature", [group([ellipse(0, 0, 300, 300)], [fill("#FFB703")]),
                               group([star(0, 0, 12, 235, 160)], [fill("#FB8500")])]),
    ("cloud", "Cloud", "nature", [group([ellipse(-80, 30, 220, 170), ellipse(60, -10, 250, 220), ellipse(155, 50, 160, 125),
                                         ellipse(-170, 65, 120, 100), rect(0, 85, 420, 90, 45)], [fill("#FFFFFF")]),
                                   group([ellipse(-80, 30, 220, 170), ellipse(60, -10, 250, 220), ellipse(155, 50, 160, 125),
                                          ellipse(-170, 65, 120, 100), rect(0, 85, 420, 90, 45)], [fill("#BBD7FF")], x=10, y=14)]),
    ("raindrop", "Raindrop", "nature", [group([path([[0, -210], [135, 65], [0, 200], [-135, 65]], True,
                                                    [[0, 0], [-40, -120], [75, 0], [0, 75]], [[0, 0], [0, 75], [-75, 0], [40, -120]])],
                                              [fill("#4CC9F0"), stroke("#3A9FCB", 8)])]),
    ("flame", "Flame", "nature",
     [group([path([[10, -60], [70, 90], [0, 160], [-70, 90]], True, [[0, 0], [-20, -60], [45, 0], [0, 45]],
                  [[0, 0], [0, 45], [-45, 0], [20, -60]])], [fill("#FFBD00")]),
      group([path([[30, -215], [150, 60], [0, 200], [-150, 60]], True, [[0, 0], [-30, -130], [85, 0], [0, 85]],
                  [[0, 0], [0, 85], [-85, 0], [10, -150]])], [fill("#FF5400")])]),
    ("lightning", "Lightning", "nature", [group([path([[70, -210], [-115, 35], [-10, 35], [-60, 215], [135, -45], [25, -45], [100, -210]])],
                                                [fill("#FFD60A"), stroke("#F48C06", 8)])]),
    ("smiley", "Smiley", "emoji", face([group([ellipse(-70, -50, 42, 72), ellipse(70, -50, 42, 72)], [fill("#3D2C2E")])],
                                       group([path([[-110, 40], [110, 40]], False, [[0, 0], [-60, 110]], [[60, 110], [0, 0]])],
                                             [stroke("#3D2C2E", 22)]))),
    ("surprised", "Surprised", "emoji", face([group([ellipse(-70, -50, 54, 54), ellipse(70, -50, 54, 54)], [fill("#3D2C2E")])],
                                             group([ellipse(0, 85, 80, 100)], [fill("#3D2C2E")]))),
    ("bubble", "Speech Bubble", "emoji",
     [group([ellipse(-100, -30, 50, 50), ellipse(0, -30, 50, 50), ellipse(100, -30, 50, 50)], [fill("#555B6E")]),
      group([rect(0, -30, 400, 280, 70), path([[-70, 100], [-130, 210], [20, 105]])], [fill("#FFFFFF")]),
      group([rect(0, -30, 400, 280, 70), path([[-70, 100], [-130, 210], [20, 105]])], [fill("#9AA5B8")], x=8, y=10)]),
    ("note", "Music Note", "emoji", [group([ellipse(-70, 130, 140, 105), rect(10, -40, 34, 330),
                                            path([[10, -205], [150, -130], [150, -60], [27, -120]])], [fill("#7209B7")], rotation=0)]),
    ("crown", "Crown", "party", [group([ellipse(-90, 40, 34, 34), ellipse(0, 10, 40, 40), ellipse(90, 40, 34, 34)], [fill("#E63946")]),
                                  group([rect(0, 150, 360, 50, 10)], [fill("#E09F00")]),
                                  group([path([[-180, 125], [-180, -60], [-90, 20], [0, -140], [90, 20], [180, -60], [180, 125]])],
                                        [fill("#FFC300"), stroke("#E09F00", 8)])]),
    ("confetti", "Confetti", "party", confetti()),
]


def keyframes(values, dims):
    """Eased keyframes over the loop: [(frame, value)…]; the last needs no easing."""
    ease_in = {"x": [0.42] * dims, "y": [1] * dims}
    ease_out = {"x": [0.58] * dims, "y": [0] * dims}
    out = []
    for n, (t, v) in enumerate(values):
        key = {"t": t, "s": v if isinstance(v, list) else [v]}
        if n < len(values) - 1:
            key.update({"i": ease_in, "o": ease_out})
        out.append(key)
    return {"a": 1, "k": out}


MOTIONS = {
    "pulse": ("Pulse", {"s": keyframes([(0, [100, 100, 100]), (30, [114, 114, 100]), (60, [100, 100, 100])], 3)}),
    "spin": ("Spin", {"r": {"a": 1, "k": [{"t": 0, "s": [0], "i": {"x": [1], "y": [1]}, "o": {"x": [0], "y": [0]}},
                                         {"t": 60, "s": [360]}]}}),
    "bounce": ("Bounce", {"p": keyframes([(0, [256, 256, 0]), (15, [256, 206, 0]), (30, [256, 256, 0]),
                                         (45, [256, 226, 0]), (60, [256, 256, 0])], 3),
                         "s": keyframes([(0, [108, 92, 100]), (8, [96, 104, 100]), (30, [108, 92, 100]),
                                         (38, [98, 102, 100]), (60, [108, 92, 100])], 3)}),
    "wobble": ("Wobble", {"r": keyframes([(0, -10), (15, 10), (30, -10), (45, 10), (60, -10)], 1)}),
}


def lottie(name, groups, motion):
    transform = {"o": static(100), "r": static(0), "p": static([256, 256, 0]), "a": static([0, 0, 0]),
                 "s": static([100, 100, 100])}
    transform.update(MOTIONS[motion][1])
    layer = {"ddd": 0, "ind": 1, "ty": 4, "nm": name, "sr": 1, "ks": transform, "ao": 0, "shapes": groups,
             "ip": 0, "op": FRAMES, "st": 0, "bm": 0}
    return {"v": "5.7.4", "fr": FPS, "ip": 0, "op": FRAMES, "w": SIZE, "h": SIZE, "nm": name, "ddd": 0,
            "assets": [], "layers": [layer]}


def main(root):
    os.makedirs(root, exist_ok=True)
    catalog = []
    for design, title, category, groups in DESIGNS:
        for motion, (motion_title, _) in MOTIONS.items():
            sticker = f"{design}_{motion}"
            with open(os.path.join(root, sticker + ".json"), "w") as f:
                json.dump(lottie(sticker, groups, motion), f, separators=(",", ":"))
                f.write("\n")
            catalog.append({"id": sticker, "title": f"{title} · {motion_title}", "category": category,
                            "file": sticker + ".json"})
    with open(os.path.join(root, "stickers.json"), "w") as f:
        json.dump({"stickers": catalog}, f, indent=1, ensure_ascii=False)
        f.write("\n")
    print(len(catalog), "stickers written")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "effects/stickers")
