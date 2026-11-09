# Making a Dafin title: what to hand back

For ChatGPT (or any designer) creating a title template for Dafin. The format, every preset,
binding and range is in `docs/template-authoring.md`; this page is only the handoff.

## What you return for each title

**1. The file.** One complete JSON file in a single code block, nothing else inside the block, named
after its id (`chatgpt.neon-slide` → `neon-slide.json`). Never a fragment or a diff, also after a
correction.

**2. A design note**, at most ten lines:

- Name, category, `kind` (`title`, `lower-third` or `caption`) and the sample text.
- What it is for (e.g. "gaming highlight over busy footage").
- The motion as a viewer sees it: what the first frame shows, when the text is readable (ms), what
  happens during the hold, how it leaves.
- The fonts it needs (all of them are also in `requires.fonts`).
- Its quick controls, one line each: what the user changes with it.
- Anything you are unsure about and want the review to look at.

**3. Your self-check**, ticked:

- [ ] `format` 1; `id` starts with `chatgpt.`; lowercase; not already used by another template.
- [ ] Every font is one listed in the guide (3.3) and named in `requires.fonts`.
- [ ] Every layer has a unique `id`; colours are `#rrggbb` or `#aarrggbb` (alpha first).
- [ ] Gradients on text that may wrap use `"space": "line"`.
- [ ] Readable within 200 ms (captions 300 ms); `timing.maxReadMs` set when it must be guaranteed.
- [ ] A loop keeps the text readable (`breathe` amount 0.05–0.15).
- [ ] Exits that should visibly fade use `easeIn` or `linear` (guide 4.2).
- [ ] 4 to 8 quick controls, each with a type matching its binding and a sensible range.
- [ ] Clip effects only from the guide's list, with `effectPadding` when they draw around the text.
- [ ] No texture paints, images, own shaders or Lottie layers (guide section 10).

## What you get back

The reviewer installs nothing yet. They run

```
dafin --check-template neon-slide.json --out check
```

and send you `check/neon-slide-report.md` and five contact sheets (30 fps, 60 fps, a 0.4 s clip at
24 fps, two lines, long text; dark background on top, bright below). The report says whether the
file installs in normal and portable mode, whether each quick control works, and the timing at
24/30/60 fps. **Passing it is not approval**: the sheets are reviewed by eye.

## Corrections

Answer each problem in the report or the review with one line saying what you changed, then the
whole corrected file with the same `id`. Change only what was asked, unless you say why.

## Approved

An approved file goes into `presets/text-styles/` unchanged and ships with Dafin. The review notes
which values were changed during the review, if any, and why.
