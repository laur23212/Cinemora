# Dafin title templates — authoring guide

For anyone making Dafin titles without the source code: an AI such as ChatGPT, or a designer.
Everything here is checked by the validator and the tests; where something is not supported it
says so.

This guide is the contract. Two things complete it on the user's machine:

- `dafin --template-guide` prints the live lists: every animation preset and its params, every
  clip effect and its parameters, the text shader paints, and the fonts that are installed.
- `dafin --validate-template my-template.json` (add `--json` for machine-readable output) checks
  a file exactly the way installing it does.

## 1. What a template is

One JSON file: a text style (font, shading layers, animation), optionally clip effects (sparks,
glow passes…), and the few **quick controls** users usually change. Dafin renders it with its own
text engine, so everything stays editable: there is no baked video.

Users install a file from **Titles › Text styles › Install a title template (.json)**. If the
check finds problems, nothing is installed and Dafin shows a report with a **Copy report** button;
the user pastes that report back to you unchanged. Fix the file and keep the same `id`:
installing it again replaces the old version.

Kinds: `title` (default), `lower-third`, `caption` (applied to subtitle clips; every cue is its own
animation window, and a `text` control does nothing there).

## 2. The file

```json
{
  "format": 1,
  "id": "author.neon-pop",
  "label": "Neon Pop",
  "description": "Pops in within 120 ms, neon glow keeps pulsing.",
  "category": "fast-titles",
  "tags": ["neon", "fast"],
  "kind": "title",
  "sampleText": "LET'S GO",
  "requires": { "fonts": ["Inter"], "minAppVersion": "1.0.0" },
  "style": { },
  "effects": [ ],
  "controls": [ ]
}
```

| Field | Required | Notes |
|---|---|---|
| `format` | yes | The number `1` |
| `id` | yes | 2–64 chars: lowercase letters, digits, `.` `-` `_`. Prefix it with your name: `chatgpt.neon-pop`. Never `user:` |
| `label` | yes | The name users see |
| `description`, `category`, `tags`, `sampleText` | no | `sampleText` is drawn on the card thumbnail |
| `kind` | no | `title` · `lower-third` · `caption` |
| `requires.fonts` | for portable files | Every font family the style uses |
| `requires.minAppVersion` | no | Refused by older Dafin builds |
| `style` | yes | Section 3 |
| `effects` | no | Section 5 |
| `controls` | no | Section 6 |

Unknown fields are ignored with a warning. Anything you leave out of `style` takes the default
listed in section 3.

## 3. The style

Units: lengths are pixels at the project's height (a 1080 p project), so `pixelSize: 96` is 96 px
tall on a 1080 p frame. Positive `offsetY` moves down. Colours are `"#rrggbb"` or `"#aarrggbb"`
(alpha **first**, as Dafin writes them); `"#80000000"` is black at 50 %.

| Field | Default | Values |
|---|---|---|
| `fontFamily` | `"Inter"` | A family from `--template-guide`. Inter is always there (weights 400–700) |
| `fontWeight` | `700` | 100–900; the nearest installed weight is used |
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
| `underlineReveal` | `1` | 0–1, how much of each line's underline is drawn (keyframe it for a draw-on) |
| `effectPadding` | `0` | px of room kept around the text for clip effects (section 5) |
| `layers` | one white fill | The shading stack, below |
| `animation` | none | Section 4 |
| `keyframes` | none | Section 4.4 |
| `wordHighlight`, `accent` | off | Per-word pill, accent words; copy them from an exported Dafin style if you need them |

### 3.1 Shading layers

`layers[0]` is drawn first (at the bottom). Every stroke is drawn under every fill. Ids must be
unique, lowercase, and start with a letter; controls address layers by id, so keep them stable.

```json
"layers": [
  { "id": "glow",   "kind": "glow",   "paint": { "kind": "solid", "color": "#22e3ff" }, "blur": 26, "spread": 2, "opacity": 0.85 },
  { "id": "stroke", "kind": "stroke", "paint": { "kind": "solid", "color": "#062433" }, "width": 5, "strokeAlign": "outside" },
  { "id": "fill",   "kind": "fill",   "paint": { "kind": "solid", "color": "#ffffff" } }
]
```

| Field | Default | Notes |
|---|---|---|
| `kind` | `fill` | `fill` · `stroke` · `shadow` · `glow` · `extrude` |
| `enabled` | `true` | |
| `opacity` | `1` | 0–1 |
| `offsetX`, `offsetY` | `0` | px |
| `blur` | `0` | Shadow blur / glow radius, px |
| `spread` | `0` | Shadow / glow dilation, px |
| `width` | `2` | Stroke width / extrude depth, px |
| `strokeAlign` | `outside` | `center` · `inside` · `outside` |
| `blend` | `normal` | |
| `paint` | white solid | Below |

Paints:

- `{ "kind": "solid", "color": "#ffffff" }`
- `{ "kind": "gradient", "gradient": { "kind": "linear", "angle": 90, "stops": [ { "pos": 0, "color": "#ff8a00" }, { "pos": 1, "color": "#e52e71" } ] } }`
  (`kind` linear · radial · sweep; `space` block · line · word · glyph; `offsetSpeed` moves it)
- `{ "kind": "effect", "effect": { "id": "shine", "params": { } } }`: a built-in text shader:
  `shine`, `shimmer`, `neon-pulse`, `glitch`, `chrome`, `dissolve` (params in `--template-guide`).
  You cannot ship your own text shaders.
- `texture` paints are **not supported** in installable templates yet (the file would need an
  image beside it).

## 4. Animation and readability

### 4.1 Three stages

```json
"animation": {
  "in":   { "preset": "pop",     "params": { "duration": 0.12, "from": 70, "ease": "back" } },
  "loop": { "preset": "breathe", "params": { "amount": 0.2 } },
  "out":  { "preset": "fade",    "params": { "duration": 0.1 } },
  "timing": { "maxReadMs": 250 }
}
```

- **in**: the entrance. **loop**: decoration while the text is readable (glow pulses, gentle
  motion). **out**: the exit. Leave a slot out to disable it; `"enabled": false` keeps it but turns it off.
- `preset` must be an id that plays in that slot (the list is in `--template-guide`).
- Every in/out preset takes `duration` (s, 0–5), `stagger` (s, 0–2, delay between units), `unit`
  (`block` · `character` · `word` · `line`), `order` (`forward` · `backward` · `centerOut` ·
  `random`) and `ease` (`linear` · `easeIn` · `easeOut` · `easeInOut` · `back` · `bounce` ·
  `smooth`), plus its own params.
- Params can be plain values (`0.12`, `"word"`, `"#ff0000"`, `true`) or Dafin's typed form
  (`{ "type": "scalar", "value": 0.12 }`). Types and ranges are checked.

### 4.2 Time to read

Dafin computes when the text becomes readable: **the moment the in slot (its delay, stagger and
duration together) has brought every letter to rest**. Loop motion, shader paints and clip
effects are not counted, so they must not hide the text. It is an animation-based estimate: a
blur or a dark effect drawn over finished text is your responsibility. A `random` order makes it
an upper bound. The editor shows it ("Readable after 0.12 s").

Make it fast. Most titles should be readable within **0–200 ms**; captions within 250–300 ms.
Long staggers are the usual mistake: `typewriter` at 0.06 s per letter takes 2.5 s on a
42-letter caption.

### 4.3 Short clips (automatic)

1. **Fit**: the in + out together never take more than half of the clip (a subtitle cue is its own
   clip). Both shrink by the same factor; a clip long enough plays the authored timing exactly.
   `"timing": { "fit": false }` turns this off.
2. **Cap**: `"timing": { "maxReadMs": N }` is a hard cap. The whole in slot is sped up so the text
   is at rest by N ms. Use it for captions.
3. Shortened durations snap down to whole frames. An in shorter than one frame becomes instant;
   an out shorter than one frame is dropped. The text always gets the frames.

### 4.4 Keyframes (optional)

Some scalars can be keyframed: `pixelSize`, `letterSpacing`, `lineHeight`, `boxPadding`,
`pathBend`, `underlineWidth`, `underlineOffset`, `underlineReveal` and `layer.<id>.<field>` (the
same fields as in section 6). Times are microseconds from the clip's start, absolute: they do not
stretch with the clip.

```json
"keyframes": { "underlineReveal": { "keyframes": [ { "timeUs": 0, "value": 0 }, { "timeUs": 350000, "value": 1 } ] } }
```

## 5. Clip effects

```json
"effects": [ { "key": "sparks", "effect": "bling_sparkle", "parameters": { "intensity": 0.75 } } ],
"style": { "effectPadding": 48 }
```

- `key`: your name for the effect (lowercase, unique). Controls address it as `effect.<key>.…`,
  so reordering never re-targets a control.
- `effect`: an id from the clip effects list in `--template-guide`; `parameters` only names
  parameters that effect has, in its range. Missing ones take the effect's defaults.
- Applying a template replaces the effects an earlier template added and **keeps the user's own
  effects**. Re-applying never duplicates them.
- Composition order: the shading layers are drawn into the text's own image, then the clip
  effects run on that image, then the entrance's whole-block motion (opacity, scale, position)
  applies. **An effect can only draw inside that image**: the text box plus the reach of its
  strokes, shadows and glows. Sparks, flames or particles around the letters need room, so set
  `effectPadding` (px); the validator warns when a template has effects and no padding.
- Card thumbnails show the text style only, not the clip effects.

## 6. Quick controls

```json
"controls": [
  { "id": "color",    "label": "Text colour", "type": "color",  "bind": "layer.fill.color" },
  { "id": "entrance", "label": "Entrance",    "type": "scalar", "bind": "animation.in.duration", "min": 0, "max": 0.5, "step": 0.01, "unit": "s" },
  { "id": "sparks",   "label": "Sparkles",    "type": "bool",   "bind": "effect.sparks.enabled", "group": "Effects" }
]
```

Each control becomes one widget at the top of the title's properties, with a reset button.
`type` must equal the binding's type. `scalar` needs `min` < `max` (and may give `step`, `unit`);
`enum` needs `options` (values) and may give `optionLabels`. `group` folds controls into a
section; `description` is the tooltip. Several controls may bind the same path.

| `bind` | type |
|---|---|
| `text` | text |
| `font.family` | font |
| `font.weight` (100–900), `font.size` | scalar |
| `font.italic` | bool |
| `align` | enum: left · center · right |
| `pixelSize`, `letterSpacing`, `lineHeight`, `boxPadding`, `pathBend`, `underlineWidth`, `underlineOffset`, `underlineReveal`, `effectPadding` | scalar |
| `layer.<id>.enabled` | bool |
| `layer.<id>.color` | color (solid, texture or effect paints; not gradients) |
| `layer.<id>.gradient.stop.<n>.color` | color |
| `layer.<id>.opacity` · `offsetX` · `offsetY` · `blur` | scalar |
| `layer.<id>.width` (stroke, extrude) · `spread` (shadow, glow) · `trimStart` `trimEnd` `dashOffset` (stroke) | scalar |
| `layer.<id>.gradient.angle` · `.offset` · `.scale` · `layer.<id>.effect.<param>` | scalar |
| `box.enabled` · `underline.enabled` | bool |
| `box.color` · `underline.color` | color |
| `box.radius` | scalar |
| `animation.<in\|out\|loop>.enabled` | bool |
| `animation.<slot>.preset` | enum (preset ids) |
| `animation.<slot>.duration` | scalar, seconds (the period for loop) |
| `animation.<slot>.param.<id>` | the param's own type |
| `timing.fit` | bool |
| `timing.maxReadMs` | scalar |
| `effect.<key>.enabled` | bool |
| `effect.<key>.<parameter>` | scalar, bool or color, as the effect declares |

The validator rejects a binding that does not resolve on *this* style ("no layer \"glw\"; its
layers are glow, stroke, fill") and suggests the nearest real path for a typo.

## 7. The validator

```
dafin --validate-template my-template.json [--json] [--portable] [--out report.txt]
```

Exit code 0 = installable, 1 = errors, 2 = bad arguments. Each issue has a severity (`error` stops
installation, `warning` does not), a code, a JSON path and the accepted values:

```
error [binding] controls[3].bind: unknown property "layer.glow.bluur"; did you mean "layer.glow.blur"? 
warning [missing_dependency] style.fontFamily: font "Baloo 2" is not installed here; Dafin will draw the text in a fallback font
```

Codes: `schema` (shape and types), `range`, `binding`, `unsupported` (a feature Dafin does not
have), `missing_dependency` (effect or font not installed), `compatibility` (format or app
version), `render` (it will install but likely look wrong). `--portable` checks a file meant for
other machines more strictly (fonts must be listed in `requires.fonts`). On Windows, add
`--out report.txt` if the console shows nothing.

## 8. Complete examples

The five templates Dafin ships are real files that pass these checks; read them in
`presets/text-styles/`:

| File | Shows |
|---|---|
| `fast-pop.json` | Readable in 120 ms; outline + soft shadow; controls for text, font, size, colours, entrance |
| `instant-underline.json` | Readable from frame 0; keyframed underline draw-on |
| `fast-reveal-sparks.json` | Fast blur-in by word, glow, loop decoration, a clip effect with `effectPadding` and its controls |
| `static-title.json` | No animation; font, weight, size, colour, outline and shadow controls |
| `readable-caption.json` | A caption template with a box and `maxReadMs` |

## 9. Not supported yet

- Your own shaders inside a title template (GPU effects are separate packages: see
  `custom-effects.md`), textures and images, Lottie layers inside a title.
- Per-caption overrides inside a subtitle clip (the whole clip shares one style).
- Hue rotation of the whole stack (bind the colours instead).
- Packages with several templates (one file per template for now).
