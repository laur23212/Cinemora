<p align="center">
  <img src="Dafin_icon.png" alt="Dafin icon" width="128" height="128">
</p>

<h1 align="center">Dafin</h1>

<p align="center">
  <strong>The free, open-source video editor that lets you edit without a subscription.”</strong>
</p>

Dafin is a video editor for Linux, Windows and macOS based on Drift from CutWire Studios. Drop in clips, add effects, captions, stickers,
and music, then export a polished video — with **no subscription, no watermark, and no account**.

It is built for the edits people actually make: Reels and Shorts, game clips, school projects,
tutorials, product demos, memes, and anything you want to look sharp without living in a browser
or paying a monthly fee.

What you see in the preview is what you export. One compositor, one look, no surprises.

## Download

<p align="center">
  <a href="https://flathub.org/apps/org.cutwire.Dafin">
    <img src="https://flathub.org/api/badge?locale=en" alt="Get it on Flathub" height="80">
  </a>
  <a href="https://apps.microsoft.com/detail/9PHHBZ07FRSZ">
    <img src="https://get.microsoft.com/images/en-us%20dark.svg" alt="Get it from Microsoft" width="293">
  </a>
  <a href="https://play.google.com/store/apps/details?id=org.cutwire.Dafin">
    <img src="docs/btn-gplay-en.png" alt="Get it on Google Play" height="80">
  </a>
</p>

**Linux** — install from Flathub:

```bash
flatpak install flathub org.cutwire.Dafin
flatpak run org.cutwire.Dafin
```
**macOS** — install via Homebrew:

```bash
brew install --cask cutwire-studios/tap/Dafin
```

> **First launch on macOS:** Dafin is signed ad-hoc (not notarized by Apple). The quarantine attribute is automatically removed when installing via Homebrew. If you install manually via `.dmg` or if Gatekeeper still blocks opening it, run:
> ```bash
> xattr -dr com.apple.quarantine /Applications/Dafin.app
> ```

Or grab a build for your platform from the
[latest release](https://github.com/CutWire-Studios/Dafin/releases/latest):

| Platform | Package |
|----------|---------|
| Linux | [Flathub](https://flathub.org/apps/org.cutwire.Dafin) · [AppImage (x86_64, aarch64)](https://github.com/CutWire-Studios/Dafin/releases/latest) |
| Windows | [Microsoft Store](https://apps.microsoft.com/detail/9PHHBZ07FRSZ) · [Installer (.exe, x64 / arm64)](https://github.com/CutWire-Studios/Dafin/releases/latest) · [Portable zip (x64 / arm64)](https://github.com/CutWire-Studios/Dafin/releases/latest) |
| macOS | [Homebrew Tap](https://github.com/CutWire-Studios/homebrew-tap) · [Disk image (.dmg, Apple Silicon / Intel)](https://github.com/CutWire-Studios/Dafin/releases/latest) |
Studios/Dafin/releases/latest) ·


See [all releases](https://github.com/CutWire-Studios/Dafin/releases) for previous versions and full changelogs.

## Screenshot

<p align="center">
  <img src="docs/screenshots/main-window.png" alt="The Dafin editor: media bin on the left, video preview in the middle, clip inspector on the right, multi-track timeline below" width="900">
</p>

## Features

**An agent can edit the open project.** Import, cut, grade, and export in the project you have open.
The same editor can run with no window.

**3D on the timeline.** Tilt clips in space, light them, and park a title behind the subject. Drop
in a `.glb` model or a gaussian splat and it plays in the same cut. A scene camera dollies, cranes,
and orbits on its own lane, and the preview flips to a free 3D view with gizmos when you need to
place things.

**Lottie animations.** Motion graphics and vector art stay sharp at any size. Recolour or reword
them in place.

**Advanced keyframing.** Animate position, scale, rotation, 3D depth, masks, lights, and any effect
parameter. Shape the motion in a graph editor with Bezier handles, elastic and bounce easing, and
presets you save yourself.

**Color grading.** Wheels for shadows, midtones and highlights, master and per-channel curves, HSL,
and `.cube` LUTs. Read the shot on a histogram, waveform, parade or vectorscope. Grade one clip,
then apply it to the rest.

**Masks that follow the shot.** Shape, freeform and image masks with feather and invert. Point and
planar trackers pin a mask, a title, or a sticker to something moving, or steady the clip. Blur or
pixelate any masked area.

**150+ transitions and 40+ effects.** Every look previews on your footage. One click can drop a
whole stack — Beat Drop, Glitch Cut, Neon Cutout. Trails of earlier frames, and grades that stick
to one clip.

**On this machine.** Click a subject and lift it off the shot. Add depth of field and lights that
belong to the clip. Automatic captions, voices, video upscale, and live face retouch — all on your
device. Models download once, for free, the first time you use them.

**Voices that speak your script.** Type it, or hand over the subtitles, and Dafin reads it back in
21 languages on your own machine. Clone a voice from a few seconds of audio. Cloud voices are there
if you bring your own key.

**Edit the words.** Speech becomes text on the clip. Cut phrases, drop fillers, strip silence, pick
the best takes, label speakers, and build captions from those times.

**Titles with a real look.** Thirty-three packs — karaoke, Hormozi-style, neon, chrome, holographic,
handwritten. Copy one style onto every subtitle on the track.

**Move a whole stack at once.** One layer can drag, scale, rotate, tilt, and fade everything under
it. Nest layers, or open a composite when the stack needs its own timeline.

**Cut to the music.** The timeline snaps to the beat. Clips split and land on the bar. Music ducks
under speech and comes back to its own level. Reframe a landscape take into a vertical video that
follows the face.

**Workspaces for the job.** Edit, Color, Audio, Keyframes, Masking & Tracking, Subtitles, Source
Lab and more. Dock panels where you want them, float them onto a second screen, and save your own
layouts.

**Prep footage before it hits the cut.** Source Lab scrubs a clip frame by frame, marks the good and
bad ranges, and splits at scene changes. Crop, reframe, normalize, denoise, stabilise, or upscale it,
then send it to the bin or export a fast trim.

**A timeline that behaves.** Multi-track filmstrips, ripple, snap, masks, freeze frame, bookmarks,
a mixer. Split a clip and the grade stays. A crash leaves the last save intact.

**Audio that sounds finished.** Clean up speech, meter loudness, EQ and compress, keep pitch when
you change speed, record a voiceover.

**Find shots. Switch cameras.** Search footage for what’s on screen. Watch every angle at once and
punch the cut.

**Stabilise, export, take the project with you.** Smooth shaky clips, upscale, play heavy files
smoothly. MP4, GIF, audio-only, or just a range. Pack the media with the edit so paths stay whole.

**Android is the same editor.** Timeline, effects, masks, cutouts, text, 3D models, voiceover, and
export on the phone. Share straight from the app. Tablets and Chromebooks get the full desktop
layout.

**Stock, voices, and a small install.** Search stock into the bin. Generate voiceover and sound
effects. Fonts, stickers, and extra models download when you use them. UI in Arabic, Bengali, Spanish (Spain
and Colombia), French (Canada), Italian, Japanese, Portuguese (Brazil and Portugal), Russian,
Sinhala, Tagalog, Vietnamese, and Simplified Chinese.

## Why people pick Dafin

Most “free” editors want an account, a watermark, or a subscription the moment the video starts
looking good. Dafin is the opposite: **yours, on your device, GPLv3, no login wall.**

It is fast enough for a 30-second social cut and deep enough for a real project — an agent on the
timeline, 3D and gaussian splats, color grading, keyframe curves, tracking, captions, voices,
effects, audio, cutouts, and multicam.


## For developers

Build, packaging, architecture, and agent protocol live in `docs/`:

- [Building, testing, packaging, and architecture](docs/BUILDING.md)
- [GPU effects](docs/gpu-effects.md)
- [GPU transitions](docs/gpu-transitions.md)
- [Agent access / MCP](docs/MCP.md)



## Star History

<a href="https://www.star-history.com/?repos=CutWire-Studios%2FDafin&type=date&legend=top-left">
 <picture>
   <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/chart?repos=CutWire-Studios/Dafin&type=date&theme=dark&legend=top-left&sealed_token=zHW_d2jon9Wn-HYP2SWQWLC7qDRaY7qwsvHS0Cp0Ywk1Rf1UvyxhWsakIrx2c11OijPJQ9o52W99jdigV7MOz5RuvLsyWQmBiMvMdk99mcfbgb591WtzNXQO8_K2YhgdbiPD9by00lwl69ZgCZnThFKBwhRbK7IQzIeFkIFnb2o0r5GhJh0HAX6Q8yTM" />
   <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/chart?repos=CutWire-Studios/Dafin&type=date&legend=top-left&sealed_token=zHW_d2jon9Wn-HYP2SWQWLC7qDRaY7qwsvHS0Cp0Ywk1Rf1UvyxhWsakIrx2c11OijPJQ9o52W99jdigV7MOz5RuvLsyWQmBiMvMdk99mcfbgb591WtzNXQO8_K2YhgdbiPD9by00lwl69ZgCZnThFKBwhRbK7IQzIeFkIFnb2o0r5GhJh0HAX6Q8yTM" />
   <img alt="Star History Chart" src="https://api.star-history.com/chart?repos=CutWire-Studios/Dafin&type=date&legend=top-left&sealed_token=zHW_d2jon9Wn-HYP2SWQWLC7qDRaY7qwsvHS0Cp0Ywk1Rf1UvyxhWsakIrx2c11OijPJQ9o52W99jdigV7MOz5RuvLsyWQmBiMvMdk99mcfbgb591WtzNXQO8_K2YhgdbiPD9by00lwl69ZgCZnThFKBwhRbK7IQzIeFkIFnb2o0r5GhJh0HAX6Q8yTM" />
 </picture>
</a>
