# Dafin title templates — authoring guide

For anyone making Dafin titles without the source code: an AI such as ChatGPT, or a designer.
This file is meant to be enough on its own. Everything in it is checked by the validator and by
Dafin's tests; where something is not supported it says so.

Three commands complete it on the user's machine:

- `dafin --validate-template my-template.json` checks a file exactly the way installing it does
  (`--json` for machine-readable output, `--portable` for files meant for other machines).
- `dafin --template-preview my-template.json --out sheet.png` renders the motion as a contact sheet:
  frames at the first frame, mid-entrance, the readable moment, through the hold and the loop,
  mid-exit and the last frame, over a dark and a bright background, each labelled with its time
  and frame number. Ask the user for it whenever you need to see your template.
- `dafin --template-guide` prints the live lists for that installation: presets, effects and the
  fonts actually installed.

On Windows, add `--out report.txt` to the first and last if the console shows nothing.

## 1. What a template is

One JSON file: a text style (font, shading layers, animation), optionally clip effects, and the few
**quick controls** users usually change. Dafin draws it with its own text engine, so everything
stays editable; nothing is baked into video.

Users install a file from **Titles › Text styles › Install a title template (.json)**. If the
check finds problems, nothing is installed and Dafin shows a report with a **Copy report** button;
the user pastes that report back to you unchanged. Fix the file and keep the same `id`:
installing it again replaces the old version.

Kinds: `title` (default), `lower-third`, `caption` (applied to subtitle clips; every cue is its own
animation window, and a `text` control does nothing there).

## 2. The file

| Field | Required | Notes |
|---|---|---|
| `format` | yes | The number `1` |
| `id` | yes | 2–64 chars: lowercase letters, digits, `.` `-` `_`. Prefix it with your name: `chatgpt.gold-pop`. Never `user:` |
| `label` | yes | The name users see |
| `description`, `category`, `tags`, `sampleText` | no | `sampleText` is drawn on the card thumbnail and on the preview sheet |
| `kind` | no | `title` · `lower-third` · `caption` |
| `requires.fonts` | for portable files | Every font family the style uses |
| `requires.minAppVersion` | no | `"X.Y.Z"`; older Dafin refuses the file |
| `style` | yes | Section 3 |
| `effects` | no | Section 6 |
| `controls` | no | Section 7 |

Unknown fields are ignored with a warning. Anything you leave out of `style` takes the default in
section 3 — including `layers`: a style without `layers` gets one white fill and keeps everything
else you wrote. Complete, working files are in section 9.

## 3. The style

Units: lengths are pixels at the project's height (a 1080 p project), so `pixelSize: 96` is 96 px
tall in a 1080 p frame and scales with the output. Positive `offsetY` moves down.

Colours are `"#rrggbb"` or `"#aarrggbb"` — **alpha first**, as Dafin writes them: `"#80000000"` is
black at 50 %, `"#ff22e3ff"` is opaque cyan.

| Field | Default | Values |
|---|---|---|
| `fontFamily` | `"Inter"` | Section 3.3 |
| `fontWeight` | `700` | 100–900; the nearest installed weight is drawn |
| `pixelSize` | `64` | 4–1000 |
| `italic` | `false` | |
| `align` | `"center"` | `left` · `center` · `right` |
| `valign` | `"center"` | `top` · `center` · `bottom` |
| `wordWrap` | `true` | |
| `lineHeight` | `1.2` | Multiple of the font's line spacing |
| `letterSpacing` | `0` | px |
| `textCase` | `"none"` | `none` · `upper` · `lower` |
| `pathBend` | `0` | −100…100, bends a single line into an arc |
| `boxEnabled`, `boxColor`, `boxPadding`, `boxRadius` | `false`, `"#80000000"`, `8`, `0` | Background box behind the block |
| `underlineEnabled`, `underlineColor`, `underlineWidth`, `underlineOffset` | `false`, `"#ffe62828"`, `6`, `4` | |
| `underlineReveal` | `1` | 0–1, how much of each line's underline is drawn, from the left (keyframe it for a draw-on) |
| `effectPadding` | `0` | px of room around the text for clip effects (section 6) |
| `layers` | one white fill | Section 3.1 |
| `animation` | none | Section 4 |
| `keyframes` | none | Section 5 |
| `wordHighlight`, `accent` | off | Per-word pill and accent words; not covered here |

### 3.1 Shading layers

The stack is drawn in order: `layers[0]` at the bottom. Strokes are always drawn under fills,
wherever they sit in the list. Ids must be unique, lowercase, and start with a letter; controls
address layers by id, so reordering the list never re-targets a control.

| Field | Default | Notes |
|---|---|---|
| `id` | — | Required |
| `kind` | `fill` | `fill` · `stroke` · `shadow` · `glow` · `extrude` |
| `enabled` | `true` | |
| `paint` | white solid | Section 3.2 |
| `opacity` | `1` | 0–1, multiplied with the paint colour's own alpha (below) |
| `offsetX`, `offsetY` | `0` | px |
| `blur` | `0` | Gaussian blur, px (sigma ≈ blur + 0.5). Shadow softness, glow radius |
| `spread` | `0` | Shadow / glow dilation before the blur, px |
| `width` | `2` | Stroke width / extrude depth, px |
| `strokeAlign` | `outside` | `outside`: the whole `width` grows outward from the letters (the classic outline). `center`: half in, half out. `inside`: clipped to the letters |
| `blend` | `normal` | |

How the kinds draw:

- **fill**: the letters in the paint.
- **stroke**: an outline of `width` px, aligned as above, in the paint.
- **shadow** / **glow**: a silhouette of the letters **grown by the first stroke's width** (so a
  shadow sits under the outline, not only under the fill), in the paint's **solid colour**, moved by
  `offsetX/Y`, dilated by `spread`, blurred by `blur`. A gradient paint on a shadow uses its
  `color` field, not the gradient. Glow is a shadow you normally leave unmoved.
- **extrude**: copies stepped behind the letters, `width` px deep.

**Opacity and alpha multiply.** A layer's effective opacity is the colour's alpha × `opacity`:
`"color": "#80000000"` with `"opacity": 0.5` draws black at 25 %. Use one or the other; the
simplest is an opaque colour (`#rrggbb`) and `opacity` for the strength, e.g. a subtle shadow:
`{ "kind": "shadow", "paint": { "kind": "solid", "color": "#000000" }, "offsetY": 5, "blur": 10, "opacity": 0.5 }`.

### 3.2 Paints

**Solid**: `{ "kind": "solid", "color": "#ffffff" }`

**Gradient** (fills and strokes):

```json
{ "kind": "gradient",
  "gradient": { "kind": "linear", "angle": 90,
                "stops": [ { "pos": 0, "color": "#fff3b0" }, { "pos": 1, "color": "#c98a00" } ] } }
```

| Field | Default | Notes |
|---|---|---|
| `kind` | `linear` | `linear` · `radial` · `sweep` |
| `angle` | `90` | Degrees, **clockwise**. `0`: stop 0 on the left → stop 1 on the right. `90`: stop 0 at the **top** → stop 1 at the bottom. `180`: right → left. `270`: bottom → top |
| `stops` | — | 2 or more, `pos` 0–1 ascending, each with a `color` |
| `space` | `block` | The box the gradient spans: `block` (the whole text) · `line` · `word` · `glyph`. For titles that may wrap, use `line`: with `block` one gradient spreads over all lines, so the top line comes out light and the bottom line dark |
| `offset` | `0` | Slides the gradient along its direction, in box widths |
| `offsetSpeed` | `0` | Box widths per second: a moving gradient (sheen) |
| `scale` | `1` | |
| `center` | `[0.5, 0.5]` | Radial and sweep centre, in the box (0–1) |
| `repeat` | `false` | Repeat instead of clamping past the ends |
| `oklab` | `false` | Blend in OKLab (cleaner mid-tones) |

The angle is applied in the box's own unit square, then stretched to the box. `0`, `90`, `180`
and `270` are exact; other angles tilt less than their number on a wide box. A gold title is
`angle: 90`, `space: "line"`, with the light stop first: highlight at the top of every line,
deeper gold at the bottom (Gold Pop in section 9).

**Effect**: `{ "kind": "effect", "effect": { "id": "shine", "params": { "speed": 0.5 } } }`, one of
these built-in text shaders (you cannot ship your own):

| id | params |
|---|---|
| `shine` | `speed` scalar 0–4 cyc/s, default 0.5; `width` scalar 0.02–1, default 0.25; `angle` scalar -180–180 °, default 20; `intensity` scalar 0–2, default 0.9; `colorA` color, default #ffffffff |
| `shimmer` | `speed` scalar 0–4 cyc/s, default 0.3; `scale` scalar 0.5–12, default 3; `intensity` scalar 0–1, default 0.6 |
| `neon-pulse` | `speed` scalar 0–8 Hz, default 1; `intensity` scalar 0–1, default 0.5; `flicker` scalar 0–1, default 0.3; `colorA` color, default #ffff3cdc |
| `glitch` | `speed` scalar 0–30 Hz, default 8; `amount` scalar 0–60 px, default 12; `block` scalar 1–64 px, default 8 |
| `chrome` | `speed` scalar 0–2 cyc/s, default 0; `bands` scalar 0.5–6, default 2; `colorA` color, default #fff0f5ff; `colorB` color, default #ff283246 |
| `dissolve` | `scale` scalar 0.002–0.2, default 0.03; `edge` scalar 0–0.5, default 0.1; `progress` scalar 0–1, default 1; `seed` scalar 0–100, default 1 |

**Texture** paints are **not supported** in installable templates yet: the file would need an
image beside it.

### 3.3 Fonts

Text is drawn with fonts Dafin's text engine can find: the **Fonts Essentials add-on** (installed
from Dafin's add-on manager), fonts the user added, or the system's. Dafin's own built-in styles are
made for these families from that add-on: Inter, Montserrat, Poppins, DM Sans, Outfit, Plus Jakarta
Sans, League Spartan, Ubuntu, Anton, Archivo Black, Bebas Neue, Oswald, Rubik One, Fredoka,
Pacifico, Playfair Display, Bodoni Moda, Cormorant Garamond, Cinzel. `dafin --template-guide` lists
what is installed on a given machine.

Pick one of those, name it in `requires.fonts`, and use a weight the family has (`--template-guide`
lists each installed family's weights); the nearest weight is drawn otherwise.
If a font is missing, Dafin draws a fallback font and the validator warns (`missing_dependency`);
it is a warning, not an error, because the user can install the add-on.

## 4. Animation and readability

### 4.1 Three stages

```json
"animation": {
  "in":   { "preset": "pop",     "params": { "duration": 0.12, "from": 70 } },
  "loop": { "preset": "breathe", "params": { "amount": 0.1, "speed": 0.5 } },
  "out":  { "preset": "fade",    "params": { "duration": 0.15 } },
  "timing": { "maxReadMs": 150 }
}
```

- **in**: the entrance. **loop**: decoration while the text is readable. **out**: the exit. Leave a
  slot out to have none; `"enabled": false` keeps a slot but turns it off.
- The loop works on the text as the in and out leave it: its opacity *scales* theirs. A breathing
  loop never shows the title before its entrance and never holds it up during the exit fade.
- `preset` must be one that plays in that slot (table in 4.5).
- Params can be plain values (`0.12`, `"word"`, `"#ff0000"`, `true`) or Dafin's typed form
  (`{ "type": "scalar", "value": 0.12 }`). Types and ranges are checked.

### 4.2 Common params of every in / out preset

| Param | Range | Default | |
|---|---|---|---|
| `duration` | 0–5 s | 0.4 (some presets change it, see 4.5) | Length of one unit's motion |
| `stagger` | 0–2 s | 0.06 | Delay between units; irrelevant with `unit: block` |
| `unit` | `block` · `character` · `word` · `line` | `block` | What moves together |
| `order` | `forward` · `backward` · `centerOut` · `random` | `forward` | |
| `ease` | `linear` · `easeIn` · `easeOut` · `easeInOut` · `back` · `bounce` · `smooth` | `easeOut` | `back` overshoots past full size before settling, inside the duration |

**pop** in full: `from` (0–200 %, default 60) is the scale each unit starts at; opacity goes 0 → 1
with it; `duration` defaults to 0.35 s and `ease` to `back`. With `unit: block` the whole title pops
as one and the time to read equals `duration`.

**Loop slots** take the preset's own params, plus `period` (seconds, 0 = one pass over the whole
clip). `period` only matters for loops that follow the clip's progress (`slow-zoom`,
`tracking-drift`); the others run on their own clock and use their `speed` / `frequency` param.

**breathe** in full: every `1 / speed` seconds (`speed` 0.05–2 Hz, default 0.4) the whole title
eases down to `(1 − amount)` opacity (`amount` 0–1, default 0.3) and 98 % scale and back. It dims
the text, so keep `amount` small for readable titles: **0.05–0.15** (never below 85 % opacity);
0.3 is a visible pulse.

### 4.3 Time to read

Dafin computes when the text becomes readable: **the moment the in slot (its delay, stagger and
duration together) has brought every letter to rest**. With `unit: block` that is the `duration`;
with words or letters it is `(units − 1) × stagger + duration`. Loop motion, shader paints and clip
effects are not counted, so they must not hide the text. It is an animation-based estimate: a dark
effect drawn over finished text is your responsibility; a `random` order makes it an upper bound.
The editor shows it ("Readable after 0.12 s") and the preview sheet prints it.

Make it fast: most titles within **0–200 ms**, captions within 250–300 ms. `typewriter` at its
default 0.06 s per letter takes 2.5 s on a 42-letter caption.

### 4.4 Short clips (automatic)

1. **Fit**: in + out together never take more than half of the clip (a subtitle cue is its own
   clip). Both shrink by the same factor; a clip long enough plays the authored timing exactly.
   `"timing": { "fit": false }` turns this off.
2. **Cap**: `"timing": { "maxReadMs": N }` is a hard cap: the whole in slot is sped up so the text is
   at rest by N ms. A cap longer than the entrance changes nothing.
3. Shortened durations snap down to whole frames. An in shorter than one frame becomes instant; an
   out shorter than one frame is dropped. The text always gets the frames.

### 4.5 Every preset

Generated from Dafin's preset files. "Its own params" are in addition to the common ones (in / out)
or `period` (loop).

| id | slots | its own params | common-param defaults it changes |
|---|---|---|---|
| `fade` | in, out | — | — |
| `slide-down` | in, out | `distance` scalar 0–2 box, default 0.35 | — |
| `slide-left` | in, out | `distance` scalar 0–2 box, default 0.35 | — |
| `slide-right` | in, out | `distance` scalar 0–2 box, default 0.35 | — |
| `slide-up` | in, out | `distance` scalar 0–2 box, default 0.35 | — |
| `pop` | in, out | `from` scalar 0–200 %, default 60 | `duration` 0.35; `ease` "back" |
| `blur-in` | in, out | `blur` scalar 0–48 px, default 24 | `duration` 0.7 |
| `wave` | loop | `amount` scalar 0–0.5 box, default 0.12; `frequency` scalar 0.1–5 Hz, default 1.1; `phase` scalar 0–180 °, default 34.4 | — |
| `typewriter` | in, out | `caret` bool, default false; `lead` scalar 0–2 s, default 0.2; `blink` scalar 0.5–8 Hz, default 2; `hold` scalar 0–5 s, default 0.5; `collapse` bool, default false | `unit` "character"; ignores duration |
| `rise` | in, out | `distance` scalar 0–2 box, default 0.35 | — |
| `bounce` | in, out | `distance` scalar 0–2 box, default 0.35 | `duration` 0.6; ignores ease |
| `type-on-blur` | in, out | `blur` scalar 0–48 px, default 12; `scale` scalar 50–200 %, default 115 | `duration` 0.35; `stagger` 0.04; `unit` "character"; `ease` "smooth" |
| `rise-by-word` | in, out | `rise` scalar 0–3 em, default 0.6; `blur` scalar 0–48 px, default 8 | `duration` 0.5; `stagger` 0.12; `unit` "word" |
| `fade-up-char` | in, out | `rise` scalar 0–3 em, default 0.35 | `stagger` 0.03; `unit` "character"; `ease` "smooth" |
| `snap-together` | in | `spread` scalar 0–1 em, default 0.06; `blur` scalar 0–48 px, default 6 | `duration` 0.6; ignores unit, order |
| `zoom-out` | in, out | `from` scalar 100–400 %, default 160; `blur` scalar 0–48 px, default 12 | `duration` 0.7; `ease` "smooth"; ignores unit, order |
| `zoom-in` | in, out | `from` scalar 0–100 %, default 50 | `duration` 0.5 |
| `color-wave` | in | `arrive` color, default #ffc52896; `decay` scalar 0.05–2 s, default 0.35; `rise` scalar 0–2 em, default 0.3 | `duration` 0.6; `stagger` 0.09; `unit` "word" |
| `wipe` | in, out | `angle` scalar -180–180 °, default -90; `softness` scalar 0–1, default 0.3 | `duration` 0.6; ignores unit, order |
| `drop` | in | `height` scalar 0–4 em, default 1.2 | `duration` 0.7; `stagger` 0.1; `unit` "word"; `ease` "bounce"; ignores ease |
| `spin-in` | in, out | `angle` scalar -360–360 °, default -90; `from` scalar 0–200 %, default 50 | `duration` 0.5; `stagger` 0.04; `unit` "character"; `ease` "back" |
| `flip` | in, out | — | `duration` 0.45; `stagger` 0.08; `unit` "word"; `ease` "back" |
| `rain` | in, out | `height` scalar 0–6 em, default 3; `blur` scalar 0–48 px, default 6 | `duration` 0.6; `stagger` 0.03; `unit` "character"; `order` "random" |
| `converge` | in, out | `spread` scalar 0–4 em, default 1; `blur` scalar 0–48 px, default 8 | `duration` 0.6; `stagger` 0.02; `unit` "character"; `order` "centerOut" |
| `stamp` | in | `from` scalar 100–500 %, default 250; `tilt` scalar -45–45 °, default -8 | `duration` 0.35; `ease` "easeIn"; ignores unit, order |
| `blur-in-words` | in, out | `blur` scalar 0–48 px, default 20 | `duration` 0.5; `stagger` 0.08; `unit` "word" |
| `glitch-in` | in, out | `offset` scalar 0–1 box, default 0.3; `skew` scalar -60–60 °, default 25 | `duration` 0.3; `stagger` 0.02; `unit` "character"; `order` "random"; `ease` "linear" |
| `skew-slide` | in, out | `distance` scalar 0–2 box, default 0.3; `skew` scalar -60–60 °, default -20 | `unit` "word" |
| `squash` | in | `stretch` scalar 100–400 %, default 260 | `duration` 0.5; `stagger` 0.08; `unit` "word"; `ease` "back" |
| `fade-shift` | out | `rise` scalar -3–3 em, default 0.5; `blur` scalar 0–48 px, default 0 | `duration` 0.45; `ease` "easeIn" |
| `fade-scale` | out | `to` scalar 50–300 %, default 130; `blur` scalar 0–48 px, default 10 | `duration` 0.5; `ease` "easeIn"; ignores unit, order |
| `remove-by-word` | out | `drop` scalar -3–3 em, default 0.4; `blur` scalar 0–48 px, default 6 | `stagger` 0.1; `unit` "word"; `order` "backward"; `ease` "easeIn" |
| `shrink` | out | — | `duration` 0.35; `unit` "word"; `ease` "easeIn" |
| `fall` | out | `height` scalar 0–8 em, default 4; `spin` scalar -90–90 °, default 25 | `duration` 0.8; `stagger` 0.03; `unit` "character"; `order` "random"; `ease` "easeIn" |
| `pulse` | loop | `amount` scalar 0–50 %, default 5; `speed` scalar 0.1–5 Hz, default 1 | — |
| `breathe` | loop | `amount` scalar 0–1, default 0.3; `speed` scalar 0.05–2 Hz, default 0.4 | — |
| `float` | loop | `amount` scalar 0–1 em, default 0.15; `speed` scalar 0.1–3 Hz, default 0.5 | — |
| `jitter` | loop | `amount` scalar 0–10 px, default 1.5; `angle` scalar 0–15 °, default 2; `speed` scalar 1–30 Hz, default 12 | — |
| `sway` | loop | `angle` scalar 0–15 °, default 2; `lift` scalar 0–0.2 box, default 0.02; `speed` scalar 0.1–3 Hz, default 0.6 | — |
| `swing` | loop | `angle` scalar 0–30 °, default 3; `speed` scalar 0.1–3 Hz, default 0.6 | — |
| `flicker` | loop | `intensity` scalar 0–1, default 0.5; `speed` scalar 1–30 Hz, default 8 | — |
| `flicker-in` | in | — | `duration` 0.9; `ease` "linear"; ignores unit, order, ease |
| `colour-pulse` | loop | `colorA` color, default #ffff2bd6; `colorB` color, default #ff2bd6ff; `speed` scalar 0.1–3 Hz, default 0.5 | — |
| `tracking-drift` | loop | `from` scalar -20–50 %, default 2; `to` scalar -20–50 %, default 21; `zoom` scalar 90–130 %, default 100 | — |
| `slow-zoom` | loop | `to` scalar 50–200 %, default 108 | — |
| `karaoke-pop` | loop | `scale` scalar 100–200 %, default 112 | — |
| `karaoke-bounce` | loop | `lift` scalar 0–1 em, default 0.1; `scale` scalar 100–200 %, default 110 | — |

## 5. Keyframes (optional)

These style scalars can be keyframed: `pixelSize`, `letterSpacing`, `lineHeight`, `boxPadding`,
`pathBend`, `underlineWidth`, `underlineOffset`, `underlineReveal`, and `layer.<id>.<field>` (the
fields in section 7). Times are microseconds from the clip's start and absolute: they do not
stretch with the clip.

```json
"keyframes": { "underlineReveal": { "keyframes": [ { "timeUs": 0, "value": 0 }, { "timeUs": 350000, "value": 1 } ] } }
```

## 6. Clip effects

```json
"effects": [ { "key": "sparks", "effect": "bling_sparkle", "parameters": { "intensity": 0.75 } } ],
"style": { "effectPadding": 48 }
```

- `key`: your name for the effect (lowercase, unique). Controls address it as `effect.<key>.…`.
- `effect`: an id from `dafin --template-guide`; `parameters` only names parameters that effect
  has, in range. Missing ones take the effect's defaults.
- Applying a template replaces the effects an earlier template added and **keeps the user's own
  effects**; re-applying never duplicates them.
- Order: the shading layers are drawn into the text's own image, the clip effects run on that
  image, then the entrance's whole-title motion (opacity, scale, position) applies. **An effect can
  only draw inside that image**: the text box plus the reach of its strokes, shadows and glows. Set
  `effectPadding` (px) for sparks or particles around the letters; the validator warns when a
  template has effects and no padding.
- Card thumbnails and the preview sheet show the text only, not clip effects.

## 7. Quick controls

```json
"controls": [
  { "id": "color",    "label": "Text colour", "type": "color",  "bind": "layer.fill.color" },
  { "id": "entrance", "label": "Entrance",    "type": "scalar", "bind": "animation.in.duration", "min": 0, "max": 0.5, "step": 0.01, "unit": "s" },
  { "id": "motion",   "label": "Subtle motion", "type": "bool", "bind": "animation.loop.enabled", "group": "Motion" }
]
```

Each control becomes one widget at the top of the title's properties, with a reset button that
restores the template's own value. `type` must equal the binding's type. `scalar` needs
`min` < `max` and the template's value inside that range; it may give `step` and `unit`. `enum`
needs `options` (values) and may give `optionLabels`. `group` folds controls into a section;
`description` is the tooltip. Several controls may bind the same path.

| `bind` | type |
|---|---|
| `text` | text |
| `font.family` | font |
| `font.weight` (100–900), `font.size` | scalar |
| `font.italic` | bool |
| `align` | enum: left · center · right |
| `pixelSize`, `letterSpacing`, `lineHeight`, `boxPadding`, `pathBend`, `underlineWidth`, `underlineOffset`, `underlineReveal`, `effectPadding` | scalar |
| `layer.<id>.enabled` | bool |
| `layer.<id>.color` | color (solid, texture or effect paints; for a gradient use the stops) |
| `layer.<id>.gradient.stop.<n>.color` | color (`n` from 0) |
| `layer.<id>.opacity` · `offsetX` · `offsetY` · `blur` | scalar |
| `layer.<id>.width` (stroke, extrude) · `spread` (shadow, glow) · `trimStart` `trimEnd` `dashOffset` (stroke) | scalar |
| `layer.<id>.gradient.angle` · `.offset` · `.scale` · `layer.<id>.effect.<param>` | scalar |
| `box.enabled` · `underline.enabled` | bool |
| `box.color` · `underline.color` | color |
| `box.radius` | scalar |
| `animation.<in\|out\|loop>.enabled` | bool |
| `animation.<slot>.preset` | enum (preset ids) |
| `animation.<in\|out>.duration` | scalar, seconds — refused for presets that ignore duration (`typewriter`) |
| `animation.loop.duration` | scalar, seconds: the period — refused for loops on their own clock; bind `animation.loop.param.speed` |
| `animation.<slot>.param.<id>` | the param's own type |
| `timing.fit` | bool |
| `timing.maxReadMs` | scalar |
| `effect.<key>.enabled` | bool |
| `effect.<key>.<parameter>` | scalar, bool or color, as the effect declares |

The validator rejects a binding that does not resolve on *this* style or would move nothing, says
why, and suggests the nearest real path for a typo.

## 8. The validator

```
dafin --validate-template my-template.json [--json] [--portable] [--out report.txt]
```

Exit code 0 = installable, 1 = errors, 2 = bad arguments. Each issue has a severity (`error` stops
installation, `warning` does not), a code, a JSON path and the accepted values:

```
error [binding] controls[3].bind: unknown property "layer.glow.bluur"; did you mean "layer.glow.blur"?
error [range] style.animation.loop.params.amount: 3 is out of range — expected: 0..1
warning [missing_dependency] style.fontFamily: font "Baloo 2" is not installed here; Dafin will draw the text in a fallback font
```

Codes: `schema` (shape and types), `range`, `binding`, `unsupported`, `missing_dependency`,
`compatibility` (format or app version), `render` (installs, but likely looks wrong). `--portable`
is stricter about dependencies (fonts must be listed in `requires.fonts`).

## 9. Complete examples

These ship with Dafin and pass every check.

**Fast Pop**: readable in 120 ms; outline and soft shadow; controls for text, font, size, colours,
entrance and exit.

```json
{
  "format": 1,
  "id": "dafin.fast-pop",
  "label": "Fast Pop",
  "description": "Pops in within 120 ms and stays perfectly still: readable almost at once.",
  "category": "fast-titles",
  "tags": ["fast", "youtube", "bold", "pop"],
  "sampleText": "LET'S GO",
  "requires": { "fonts": ["Inter"] },
  "style": {
    "fontFamily": "Inter",
    "fontWeight": 700,
    "pixelSize": 110,
    "layers": [
      { "id": "shadow", "kind": "shadow", "paint": { "kind": "solid", "color": "#000000" },
        "offsetX": 0, "offsetY": 6, "blur": 14, "opacity": 0.55 },
      { "id": "stroke", "kind": "stroke", "paint": { "kind": "solid", "color": "#111111" },
        "width": 7, "strokeAlign": "outside" },
      { "id": "fill", "kind": "fill", "paint": { "kind": "solid", "color": "#ffffff" } }
    ],
    "animation": {
      "in":  { "preset": "pop",  "params": { "duration": 0.12, "from": 70, "ease": "back" } },
      "out": { "preset": "fade", "params": { "duration": 0.1 } }
    }
  },
  "controls": [
    { "id": "text", "label": "Text", "type": "text", "bind": "text" },
    { "id": "font", "label": "Font", "type": "font", "bind": "font.family" },
    { "id": "size", "label": "Size", "type": "scalar", "bind": "font.size", "min": 24, "max": 300, "step": 1, "unit": "px" },
    { "id": "color", "label": "Text colour", "type": "color", "bind": "layer.fill.color" },
    { "id": "outline", "label": "Outline colour", "type": "color", "bind": "layer.stroke.color" },
    { "id": "entrance", "label": "Entrance", "type": "scalar", "bind": "animation.in.duration", "min": 0, "max": 0.5, "step": 0.01, "unit": "s",
      "description": "How long the pop takes. 0.1–0.15 s keeps the text readable almost at once." },
    { "id": "exit", "label": "Fade out", "type": "bool", "bind": "animation.out.enabled", "group": "Timing" }
  ]
}
```

**Fast Reveal + Sparks**: words snap in from a blur in 150 ms; glow; a gentle loop; a clip effect
with room to draw and its controls.

```json
{
  "format": 1,
  "id": "dafin.fast-reveal-sparks",
  "label": "Fast Reveal + Sparks",
  "description": "Words snap in from a blur in 150 ms, then a cyan glow and sparkles keep it alive.",
  "category": "fast-decorated",
  "tags": ["fast", "sparks", "glow", "neon", "gaming"],
  "sampleText": "NEW RECORD",
  "requires": { "fonts": ["Inter"] },
  "style": {
    "fontFamily": "Inter",
    "fontWeight": 700,
    "pixelSize": 104,
    "effectPadding": 48,
    "layers": [
      { "id": "glow", "kind": "glow", "paint": { "kind": "solid", "color": "#22e3ff" },
        "blur": 26, "spread": 2, "opacity": 0.85 },
      { "id": "stroke", "kind": "stroke", "paint": { "kind": "solid", "color": "#062433" },
        "width": 5, "strokeAlign": "outside" },
      { "id": "fill", "kind": "fill", "paint": { "kind": "solid", "color": "#ffffff" } }
    ],
    "animation": {
      "in":   { "preset": "blur-in", "params": { "duration": 0.15, "unit": "word", "stagger": 0 } },
      "loop": { "preset": "breathe", "params": { "amount": 0.12 } }
    }
  },
  "effects": [
    { "key": "sparks", "effect": "bling_sparkle",
      "parameters": { "intensity": 0.75, "threshold": 0.6, "density": 0.55 } }
  ],
  "controls": [
    { "id": "text", "label": "Text", "type": "text", "bind": "text" },
    { "id": "color", "label": "Text colour", "type": "color", "bind": "layer.fill.color" },
    { "id": "glow", "label": "Glow colour", "type": "color", "bind": "layer.glow.color" },
    { "id": "glowSize", "label": "Glow size", "type": "scalar", "bind": "layer.glow.blur", "min": 0, "max": 60, "step": 1, "unit": "px" },
    { "id": "sparks", "label": "Sparkles", "type": "bool", "bind": "effect.sparks.enabled" },
    { "id": "sparkAmount", "label": "Sparkle amount", "type": "scalar", "bind": "effect.sparks.intensity", "min": 0, "max": 1, "step": 0.01 },
    { "id": "entrance", "label": "Entrance", "type": "scalar", "bind": "animation.in.duration", "min": 0, "max": 0.5, "step": 0.01, "unit": "s", "group": "Timing" }
  ]
}
```

**Gold Pop**: written by ChatGPT from this guide alone and accepted after one visual change
(`space: "line"`). A two-stop gold gradient with both stops as controls, an outline, a soft
shadow, a 120 ms pop under a 150 ms cap, a subtle breathe and a short fade.

```json
{
  "format": 1,
  "id": "chatgpt.gold-pop",
  "label": "Gold Pop",
  "description": "Bold YouTube title with a luminous gold gradient, dark outline, soft shadow, a 120 ms pop entrance, and subtle breathing motion.",
  "category": "fast-titles",
  "tags": [
    "youtube",
    "gold",
    "bold",
    "pop",
    "gradient",
    "fast"
  ],
  "kind": "title",
  "sampleText": "LEVEL UP!",
  "requires": {
    "fonts": [
      "Inter"
    ]
  },
  "style": {
    "fontFamily": "Inter",
    "fontWeight": 700,
    "pixelSize": 110,
    "italic": false,
    "align": "center",
    "valign": "center",
    "wordWrap": true,
    "lineHeight": 1.1,
    "letterSpacing": 1,
    "textCase": "upper",
    "layers": [
      {
        "id": "shadow",
        "kind": "shadow",
        "paint": {
          "kind": "solid",
          "color": "#000000"
        },
        "offsetX": 0,
        "offsetY": 6,
        "blur": 12,
        "opacity": 0.45
      },
      {
        "id": "outline",
        "kind": "stroke",
        "paint": {
          "kind": "solid",
          "color": "#17111f"
        },
        "width": 7,
        "strokeAlign": "outside"
      },
      {
        "id": "fill",
        "kind": "fill",
        "paint": {
          "kind": "gradient",
          "gradient": {
            "kind": "linear",
            "angle": 90,
            "space": "line",
            "stops": [
              {
                "pos": 0,
                "color": "#fff3b0"
              },
              {
                "pos": 1,
                "color": "#c98a00"
              }
            ]
          }
        }
      }
    ],
    "animation": {
      "in": {
        "preset": "pop",
        "params": {
          "duration": 0.12,
          "from": 70,
          "ease": "back",
          "unit": "block",
          "stagger": 0
        }
      },
      "loop": {
        "preset": "breathe",
        "params": {
          "amount": 0.08,
          "speed": 0.4
        }
      },
      "out": {
        "preset": "fade",
        "params": {
          "duration": 0.1,
          "unit": "block",
          "ease": "easeOut"
        }
      },
      "timing": {
        "fit": true,
        "maxReadMs": 150
      }
    }
  },
  "effects": [],
  "controls": [
    {
      "id": "title",
      "label": "Title text",
      "type": "text",
      "bind": "text"
    },
    {
      "id": "size",
      "label": "Text size",
      "type": "scalar",
      "bind": "font.size",
      "min": 40,
      "max": 220,
      "step": 1,
      "unit": "px"
    },
    {
      "id": "goldhighlight",
      "label": "Gold highlight",
      "type": "color",
      "bind": "layer.fill.gradient.stop.0.color",
      "group": "Colours"
    },
    {
      "id": "goldshadow",
      "label": "Deep gold",
      "type": "color",
      "bind": "layer.fill.gradient.stop.1.color",
      "group": "Colours"
    },
    {
      "id": "outlinecolor",
      "label": "Outline colour",
      "type": "color",
      "bind": "layer.outline.color",
      "group": "Colours"
    },
    {
      "id": "entrance",
      "label": "Entrance duration",
      "type": "scalar",
      "bind": "animation.in.duration",
      "min": 0,
      "max": 0.15,
      "step": 0.01,
      "unit": "s",
      "group": "Animation",
      "description": "Controls the pop entrance. The title becomes fully readable within 150 ms."
    },
    {
      "id": "subtlemotion",
      "label": "Subtle motion",
      "type": "bool",
      "bind": "animation.loop.enabled",
      "group": "Animation",
      "description": "Enable or disable the gentle breathing animation while the title is visible."
    }
  ]
}
```

**Readable Caption**: a caption template with a box and a 250 ms cap.

```json
{
  "format": 1,
  "id": "dafin.readable-caption",
  "label": "Readable Caption",
  "description": "Words rise in, but every caption is fully readable within 250 ms, on a soft dark box.",
  "category": "captions",
  "kind": "caption",
  "tags": ["caption", "subtitle", "readable", "shorts"],
  "sampleText": "this is how captions should read",
  "requires": { "fonts": ["Inter"] },
  "style": {
    "fontFamily": "Inter",
    "fontWeight": 700,
    "pixelSize": 64,
    "boxEnabled": true,
    "boxColor": "#99000000",
    "boxPadding": 14,
    "boxRadius": 12,
    "layers": [
      { "id": "fill", "kind": "fill", "paint": { "kind": "solid", "color": "#ffffff" } }
    ],
    "animation": {
      "in": { "preset": "rise-by-word", "params": { "duration": 0.18 } },
      "timing": { "maxReadMs": 250 }
    }
  },
  "controls": [
    { "id": "font", "label": "Font", "type": "font", "bind": "font.family" },
    { "id": "size", "label": "Size", "type": "scalar", "bind": "font.size", "min": 24, "max": 160, "step": 1, "unit": "px" },
    { "id": "color", "label": "Text colour", "type": "color", "bind": "layer.fill.color" },
    { "id": "box", "label": "Background", "type": "bool", "bind": "box.enabled" },
    { "id": "boxColor", "label": "Background colour", "type": "color", "bind": "box.color" },
    { "id": "read", "label": "Readable within", "type": "scalar", "bind": "timing.maxReadMs", "min": 0, "max": 1000, "step": 10, "unit": "ms" }
  ]
}
```

## 10. Not supported yet

- Your own shaders inside a title template (GPU effects are separate packages), textures and
  images, Lottie layers inside a title.
- Per-caption overrides inside a subtitle clip (the whole clip shares one style).
- Hue rotation of the whole stack (bind the colours instead).
- Packages with several templates (one file per template for now).
