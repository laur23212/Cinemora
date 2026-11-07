#!/usr/bin/env python3
"""Generates the Dafin Open Library (asset-library/): original Lottie stickers and animated
title templates, dedicated to the public domain (CC0 1.0), plus index.json.

Run from the repository root:  python3 tools/asset-library/generate.py
Titles expose their words as Lottie text slots ("title", "subtitle"), which Dafin's Properties
panel edits. Fonts: Arial Bold (Windows) / its fontconfig alias (Linux)."""
import json, math, os

OUT = "asset-library"
FPS = 30
CC0 = ("CC0 1.0", "https://creativecommons.org/publicdomain/zero/1.0/")

def static(v): return {"a": 0, "k": v}
def kf(frames, ease=(0.33, 1.0)):
    """frames: [(t, value), ...] -> animated property with an ease-out on each step."""
    out = []
    for i, (t, v) in enumerate(frames):
        v = v if isinstance(v, list) else [v]
        k = {"t": t, "s": v}
        if i < len(frames) - 1:
            k["o"] = {"x": [0.33], "y": [0.0]}
            k["i"] = {"x": [ease[0]], "y": [ease[1]]}
        out.append(k)
    return {"a": 1, "k": out}

def ks(p=(0, 0), s=100, r=0, o=100, a=(0, 0)):
    def prop(v, two):
        if isinstance(v, dict): return v
        if two: return static(list(v) + [0])
        return static(v)
    sc = s if isinstance(s, dict) else static([s, s, 100])
    return {"o": o if isinstance(o, dict) else static(o), "r": r if isinstance(r, dict) else static(r),
            "p": prop(p, True), "a": static(list(a) + [0]), "s": sc}

def tr(p=(0, 0), s=100, r=0, o=100):
    return {"ty": "tr", "p": p if isinstance(p, dict) else static(list(p)), "a": static([0, 0]),
            "s": s if isinstance(s, dict) else static([s, s]), "r": r if isinstance(r, dict) else static(r),
            "o": o if isinstance(o, dict) else static(o)}

def fill(rgb, o=100): return {"ty": "fl", "c": static(list(rgb) + [1]), "o": static(o), "r": 1}
def stroke(rgb, w, o=100): return {"ty": "st", "c": static(list(rgb) + [1]), "o": static(o), "w": static(w), "lc": 2, "lj": 2}
def ellipse(w, h, p=(0, 0)): return {"ty": "el", "p": static(list(p)), "s": static([w, h])}
def rect(w, h, rad=0, p=(0, 0)):
    return {"ty": "rc", "p": static(list(p)), "s": w if isinstance(w, dict) else static([w, h]), "r": static(rad)}
def path(points, closed=True, ins=None, outs=None):
    n = len(points)
    return {"ty": "sh", "ks": static({"c": closed, "v": points, "i": ins or [[0, 0]] * n, "o": outs or [[0, 0]] * n})}
def star(points, inner, outer, rot=0):
    return {"ty": "sr", "sy": 1, "d": 1, "pt": static(points), "p": static([0, 0]), "r": static(rot),
            "ir": static(inner), "is": static(0), "or": static(outer), "os": static(0)}
def trim(end):
    return {"ty": "tm", "s": static(0), "e": end, "o": static(0), "m": 1}
def group(items, t=None): return {"ty": "gr", "it": items + [t or tr()]}

def layer(ind, name, shapes, op, transform):
    return {"ddd": 0, "ind": ind, "ty": 4, "nm": name, "sr": 1, "ks": transform, "ao": 0,
            "shapes": shapes, "ip": 0, "op": op, "st": 0, "bm": 0}

def doc(w, h, op, layers, extra=None):
    d = {"v": "5.7.4", "fr": FPS, "ip": 0, "op": op, "w": w, "h": h, "nm": "Dafin", "ddd": 0,
         "assets": [], "layers": layers}
    if extra: d.update(extra)
    return d

# ── colours ──
RED = (0.94, 0.27, 0.36); PINK = (1.0, 0.45, 0.6); GOLD = (1.0, 0.78, 0.18); TEAL = (0.18, 0.86, 0.78)
GREEN = (0.22, 0.78, 0.45); WHITE = (1, 1, 1); INK = (0.08, 0.09, 0.11); BLUE = (0.25, 0.55, 1.0)
PURPLE = (0.55, 0.38, 0.95)

POP = kf([(0, [0, 0, 100]), (10, [118, 118, 100]), (16, [94, 94, 100]), (22, [100, 100, 100])])

def heart_shape(size):
    """A symmetric heart: bottom tip, two round lobes, a soft dip between them."""
    s = size / 200.0
    sc = lambda pts: [[x * s, y * s] for x, y in pts]
    v = [[0, 90], [-95, -5], [-50, -80], [0, -45], [50, -80], [95, -5]]
    i = [[40, -30], [0, 45], [-30, 0], [-10, -25], [-30, 0], [0, -40]]
    o = [[-40, -30], [0, -40], [30, 0], [10, -25], [30, 0], [0, 45]]
    return path(sc(v), True, sc(i), sc(o))

stickers = []

def add_sticker(sid, title, tags, d, preview_at=0.5):
    stickers.append({"id": sid, "kind": "sticker", "title": title, "tags": tags, "data": d, "previewAt": preview_at})

# 1. Heart pop
op = 60
pulse = kf([(0, [0, 0, 100]), (10, [118, 118, 100]), (16, [94, 94, 100]), (22, [100, 100, 100]),
            (40, [100, 100, 100]), (46, [110, 110, 100]), (52, [100, 100, 100])])
add_sticker("heart-pop", "Heart pop", "heart love like pop",
            doc(512, 512, op, [layer(1, "heart", [group([heart_shape(300), fill(RED)])], op, ks(p=(256, 280), s=pulse))]))

# 2. Star burst
op = 45
rays = []
for k in range(8):
    ang = k * math.pi / 4
    far = (math.cos(ang) * 190, math.sin(ang) * 190)
    rays.append(group([ellipse(22, 22), fill(GOLD)],
                      tr(p=kf([(4, [math.cos(ang) * 60, math.sin(ang) * 60]), (24, list(far))]),
                         o=kf([(4, 100), (28, 0)]))))
add_sticker("star-burst", "Star burst", "star sparkle win burst gold",
            doc(512, 512, op, [
                layer(1, "star", [group([star(5, 50, 120), fill(GOLD), stroke((0.95, 0.6, 0.1), 6)])], op,
                      ks(p=(256, 256), s=POP, r=kf([(0, -40), (22, 0)]))),
                layer(2, "rays", rays, op, ks(p=(256, 256)))]), 0.6)

# 3. Check mark
op = 45
add_sticker("check-draw", "Check mark", "check done tick yes correct",
            doc(512, 512, op, [
                layer(1, "tick", [group([path([[-70, 0], [-20, 50], [80, -60]], False), trim(kf([(8, 0), (24, 100)])),
                                         stroke(WHITE, 34)])], op, ks(p=(256, 260))),
                layer(2, "disc", [group([ellipse(330, 330), fill(GREEN)])], op, ks(p=(256, 256), s=POP))]), 0.8)

# 4. Arrow nudge
op = 40
add_sticker("arrow-nudge", "Pointing arrow", "arrow point here look direction",
            doc(512, 512, op, [layer(1, "arrow", [group([
                path([[-150, -35], [40, -35], [40, -95], [160, 0], [40, 95], [40, 35], [-150, 35]]),
                fill(TEAL), stroke(INK, 10)])], op,
                ks(p=kf([(0, [236, 256, 0]), (10, [276, 256, 0]), (20, [236, 256, 0]), (30, [276, 256, 0]), (40, [236, 256, 0])])))]))

# 5. Sparkles
op = 60
def twinkle(delay):
    return kf([(delay, [0, 0, 100]), (delay + 10, [100, 100, 100]), (delay + 25, [0, 0, 100])])
spark = lambda size: [group([star(4, size * 0.22, size), fill(WHITE), stroke(GOLD, 6)])]
add_sticker("sparkles", "Sparkles", "sparkle shine magic new clean",
            doc(512, 512, op, [
                layer(1, "s1", spark(110), op, ks(p=(230, 240), s=twinkle(0))),
                layer(2, "s2", spark(60), op, ks(p=(370, 140), s=twinkle(12))),
                layer(3, "s3", spark(45), op, ks(p=(360, 360), s=twinkle(24)))]), 0.25)

# 6. Circle highlight (hand-drawn ring)
op = 40
add_sticker("ring-highlight", "Circle highlight", "circle highlight mark attention ring",
            doc(512, 512, op, [layer(1, "ring", [group([ellipse(400, 300), trim(kf([(0, 0), (20, 100)])),
                                                         stroke(RED, 16)])], op, ks(p=(256, 256), r=static(-12)))]), 0.9)

# 7. Speech bubble with typing dots
op = 60
dots = []
for k in range(3):
    dots.append(group([ellipse(30, 30, (-60 + k * 60, -10)), fill(INK)],
                      tr(o=kf([(12 + k * 6, 20), (20 + k * 6, 100), (32 + k * 6, 20)]))))
add_sticker("speech-bubble", "Speech bubble", "speech talk chat comment bubble typing",
            doc(512, 512, op, [
                layer(1, "dots", dots, op, ks(p=(256, 236))),
                layer(2, "bubble", [group([rect(320, 200, 60, (0, -20)), path([[-60, 70], [-110, 140], [10, 70]]),
                                           fill(WHITE), stroke(INK, 10)])], op, ks(p=(256, 256), s=POP))]), 0.6)

# 8. Confetti pop
op = 50
bits = []
colours = [RED, GOLD, TEAL, BLUE, PURPLE, GREEN]
for k in range(14):
    ang = (k / 14.0) * 2 * math.pi + 0.2
    dist = 150 + (k % 3) * 40
    shape = rect(26, 14, 3) if k % 2 else ellipse(20, 20)
    bits.append(group([shape, fill(colours[k % len(colours)])],
                      tr(p=kf([(0, [0, 0]), (22, [math.cos(ang) * dist, math.sin(ang) * dist + 30])]),
                         r=kf([(0, 0), (40, 360 if k % 2 else -270)]), o=kf([(18, 100), (40, 0)]))))
add_sticker("confetti-pop", "Confetti pop", "confetti party celebrate congratulations",
            doc(512, 512, op, [layer(1, "confetti", bits, op, ks(p=(256, 256)))]), 0.4)

# 9. Lightning flash
op = 36
add_sticker("lightning", "Lightning bolt", "lightning bolt energy power fast flash",
            doc(512, 512, op, [layer(1, "bolt", [group([
                path([[30, -190], [-100, 20], [-10, 20], [-50, 190], [100, -40], [10, -40]]),
                fill(GOLD), stroke(INK, 10)])], op,
                ks(p=(256, 256), s=POP, o=kf([(14, 100), (17, 35), (20, 100), (23, 50), (26, 100)])))]), 0.5)

# ── titles (1920x1080, text slots) ─────────────────────────────────────────────────────────
FONT = {"fName": "Arial-BoldMT", "fFamily": "Arial", "fStyle": "Bold", "ascent": 71.6}

def text_doc(text, size, colour, justify=0):
    return {"s": size, "f": FONT["fName"], "t": text, "j": justify, "tr": 0, "lh": size * 1.2, "ls": 0,
            "fc": list(colour)}

def text_layer(ind, name, slot, text, size, colour, op, transform, justify=0):
    return {"ddd": 0, "ind": ind, "ty": 5, "nm": name, "sr": 1, "ks": transform, "ao": 0,
            "t": {"d": {"sid": slot, "k": [{"s": text_doc(text, size, colour, justify), "t": 0}]},
                  "p": {}, "m": {"g": 1, "a": static([0, 0])}, "a": []},
            "ip": 0, "op": op, "st": 0, "bm": 0}

def title_doc(op, layers, slots):
    return doc(1920, 1080, op, layers, {
        "fonts": {"list": [FONT]},
        "slots": {sid: {"p": {"k": [{"s": text_doc(t, sz, c, j), "t": 0}]}} for sid, (t, sz, c, j) in slots.items()}})

titles = []
def add_title(tid, title, tags, d, preview_at=0.5):
    titles.append({"id": tid, "kind": "title", "title": title, "tags": tags, "data": d, "previewAt": preview_at,
                   "editableText": True})

# 1. Lower third — slide in, hold, slide out (5 s)
op = 150
slide_in_out = kf([(0, [-900, 0, 0]), (14, [0, 0, 0]), (130, [0, 0, 0]), (144, [-900, 0, 0])])
fade = kf([(8, 0), (20, 100), (126, 100), (138, 0)])
add_title("lower-third-slide", "Lower third: slide", "lower third name caption interview",
          title_doc(op, [
              text_layer(1, "Title", "title", "Your Name", 64, WHITE, op, ks(p=(150, 862), o=fade)),
              text_layer(2, "Subtitle", "subtitle", "Your role or caption", 36, (0.85, 0.87, 0.9), op, ks(p=(150, 918), o=fade)),
              layer(3, "accent", [group([rect(14, 150, 0, (-20, 0)), fill(TEAL)]),
                                  group([rect(820, 150, 10, (400, 0)), fill(INK, 82)])], op,
                    ks(p=slide_in_out, a=(0, 0)) | {"p": slide_in_out}),
          ], {"title": ("Your Name", 64, WHITE, 0), "subtitle": ("Your role or caption", 36, (0.85, 0.87, 0.9), 0)}), 0.3)
titles[-1]["data"]["layers"][2]["ks"]["p"] = {"a": 1, "k": [
    {"t": 0, "s": [-760, 880, 0], "o": {"x": [0.33], "y": [0]}, "i": {"x": [0.2], "y": [1]}},
    {"t": 14, "s": [130, 880, 0], "o": {"x": [0.33], "y": [0]}, "i": {"x": [0.33], "y": [1]}},
    {"t": 130, "s": [130, 880, 0], "o": {"x": [0.6], "y": [0]}, "i": {"x": [0.33], "y": [1]}},
    {"t": 144, "s": [-760, 880, 0]}]}

# 2. Title card — fade up with growing underline (4 s)
op = 120
up = kf([(0, [960, 600, 0]), (18, [960, 540, 0])], ease=(0.2, 1))
add_title("title-fade-up", "Title card: fade up", "title intro opening big heading",
          title_doc(op, [
              text_layer(1, "Title", "title", "Big Title", 140, WHITE, op, ks(p=up, o=kf([(0, 0), (18, 100), (104, 100), (118, 0)])), 2),
              text_layer(2, "Subtitle", "subtitle", "a short line under it", 44, (0.85, 0.87, 0.9), op,
                         ks(p=kf([(6, [960, 700, 0]), (24, [960, 650, 0])], ease=(0.2, 1)), o=kf([(6, 0), (24, 100), (104, 100), (118, 0)])), 2),
              layer(3, "underline", [group([rect(kf([(10, [0, 8]), (34, [520, 8])]), 0, 4), fill(TEAL)])], op,
                    ks(p=(960, 590), o=kf([(104, 100), (118, 0)]))),
          ], {"title": ("Big Title", 140, WHITE, 2), "subtitle": ("a short line under it", 44, (0.85, 0.87, 0.9), 2)}), 0.45)

# 3. Chapter heading — line reveal (4 s)
op = 120
add_title("chapter-line", "Chapter heading", "chapter section part heading divider",
          title_doc(op, [
              text_layer(1, "Label", "subtitle", "CHAPTER 1", 40, TEAL, op, ks(p=(960, 470), o=kf([(10, 0), (26, 100), (104, 100), (118, 0)])), 2),
              text_layer(2, "Title", "title", "The Beginning", 110, WHITE, op,
                         ks(p=kf([(14, [960, 610, 0]), (32, [960, 590, 0])], ease=(0.2, 1)), o=kf([(14, 0), (32, 100), (104, 100), (118, 0)])), 2),
              layer(3, "line", [group([rect(kf([(0, [0, 4]), (22, [900, 4])]), 0, 2), fill(WHITE)])], op,
                    ks(p=(960, 500), o=kf([(104, 100), (118, 0)]))),
          ], {"title": ("The Beginning", 110, WHITE, 2), "subtitle": ("CHAPTER 1", 40, TEAL, 2)}), 0.5)

# 4. Pop title — bouncy pill (3 s)
op = 90
add_title("pop-title", "Pop title", "pop fun wow bounce youtube shorts",
          title_doc(op, [
              text_layer(1, "Title", "title", "WOW!", 120, INK, op, ks(p=(960, 582), s=POP), 2),
              layer(2, "pill", [group([rect(640, 220, 110), fill(GOLD), stroke(INK, 10)])], op,
                    ks(p=(960, 540), s=POP, r=kf([(0, -8), (22, -3)]))),
          ], {"title": ("WOW!", 120, INK, 2)}), 0.4)

# 5. Corner tag — slides in from the top-left (4 s)
op = 120
add_title("corner-tag", "Corner tag", "tag label live new corner badge",
          title_doc(op, [
              text_layer(1, "Title", "title", "NEW VIDEO", 48, WHITE, op,
                         ks(p=kf([(0, [-300, 132, 0]), (14, [96, 132, 0]), (104, [96, 132, 0]), (118, [-300, 132, 0])], ease=(0.2, 1)))),
              layer(2, "tag", [group([rect(400, 90, 16), fill(PURPLE)])], op,
                    ks(p=kf([(0, [-180, 115, 0]), (12, [266, 115, 0]), (106, [266, 115, 0]), (118, [-180, 115, 0])], ease=(0.2, 1)))),
          ], {"title": ("NEW VIDEO", 48, WHITE, 0)}), 0.5)

# ── write ──
index = {"version": 1, "name": "Dafin Open Library",
         "note": "Dafin originals are dedicated to the public domain (CC0 1.0). Third-party items keep their own licence.",
         "items": []}
for group_items, folder in ((stickers, "stickers"), (titles, "titles")):
    os.makedirs(os.path.join(OUT, folder), exist_ok=True)
    for it in group_items:
        rel = f"{folder}/{it['id']}.json"
        with open(os.path.join(OUT, rel), "w") as f:
            json.dump(it["data"], f, separators=(",", ":"))
        entry = {"id": it["id"], "kind": it["kind"], "title": it["title"], "tags": it["tags"], "file": rel,
                 "license": CC0[0], "licenseUrl": CC0[1], "author": "Dafin", "previewAt": it["previewAt"],
                 "duration": round(it["data"]["op"] / FPS, 2)}
        if it.get("editableText"):
            entry["editableText"] = True
        index["items"].append(entry)

# Permissively licensed third-party samples, fetched from their own repository (not bundled).
AIRBNB = "https://raw.githubusercontent.com/airbnb/lottie-android/master/sample/src/main/assets/"
for sid, title, fname, tags in [("airbnb-android-wave", "Android wave", "AndroidWave.json", "wave hello"),
                                ("airbnb-hamburger-arrow", "Menu to arrow", "HamburgerArrow.json", "menu icon arrow")]:
    index["items"].append({"id": sid, "kind": "sticker", "title": title, "tags": tags, "file": AIRBNB + fname,
                           "license": "Apache-2.0", "licenseUrl": "https://www.apache.org/licenses/LICENSE-2.0",
                           "author": "Airbnb (lottie-android samples)", "page": "https://github.com/airbnb/lottie-android",
                           "remoteOnly": True})

with open(os.path.join(OUT, "index.json"), "w") as f:
    json.dump(index, f, indent=1)
print(f"{len(stickers)} stickers, {len(titles)} titles -> {OUT}/")
