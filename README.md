<p align="center">
  <img src="Drift_icon.png" alt="Dafin icon: a D with a laurel leaf" width="128" height="128">
</p>

<h1 align="center">Dafin</h1>

<p align="center">
  <strong>A free, open-source video editor for Windows and Linux with a layout Filmora users will recognise.</strong>
</p>

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/github/license/laur23212/Dafin?style=flat&labelColor=28272f&color=2fb8a6" alt="License: GPL-3.0" height="24"></a>
  <img src="https://img.shields.io/badge/platform-Windows%20%7C%20Linux-2fb8a6?style=flat&labelColor=28272f" alt="Platform: Windows | Linux" height="24">
</p>

Dafin (Romanian for the laurel tree, the leaf in the icon) is a video editor built on
[Drift](https://github.com/CutWire-Studios/Drift) by CutWire Studios. Drift supplies the engine:
compositor, timeline, effects, export. Dafin changes the layout and the workflow, and adds a
library of free stock media, titles, stickers and sound effects that you browse, preview and drag
straight onto the timeline.

There is **no subscription, no watermark and no account**. Dafin is not affiliated with
Wondershare (Filmora) or CutWire Studios.

## Why Dafin exists

Filmora is a friendly editor, but it has no Linux version. Dafin started as one person's plan to
move to Linux without giving up that way of working: a media library on the left, properties next
to it, the preview on top and the timeline below, with titles, effects, templates and stickers
one click away. Windows is the main test platform today, and Linux is kept working alongside it
so the move stays possible.

## What you get

**A Filmora-style workspace.** Dark grey panels with a teal accent, and an icon bar across the
top of the asset panel: Media, Stock Media, Audio, Titles, Transitions, Effects, Stickers,
Templates and AI. Buttons on the top bar, under the preview and on the timeline toolbar can be
moved: press and hold, then drag.

**Free media inside the app.** Stock video, music, sound effects, stickers and title templates
from free providers (Pexels, Pixabay, Openverse, Jamendo, Freesound) and from Dafin's own
library. Preview a card, download it with one click, then drag it onto the timeline. The browser
is driven by a JSON catalog, so any JSON search API can be added without code changes.
Downloads go to a temporary folder that is cleared when Dafin closes, and media used by a saved
project is stored inside the project file.

**Dafin Sound Effects.** 335 original synthesized effects, royalty-free, built into the app so
they work offline. They were made for Dafin by ChatGPT.

**Volume dots.** Something Filmora lacks. Click the volume line on any clip to add a dot, drag
dots to shape the loudness, with no need to split the clip. Inspired by Cool Edit Pro.

**Titles and templates.** Title templates with quick controls for text, font, colours and
entrance speed. Premiere `.mogrt` titles stay editable animated titles, without Adobe software.
Split-screen templates are built in. Drop in your own Lottie files, sounds and templates.

**Editing tools.** Keyframes, speed control and ramps, snapping and markers, adjustment layers,
chroma key in one click, motion tracking for titles and stickers, audio sync by sound, beat
markers, auto ducking, loudness normalizing and a Shortcuts list you can rebind.

**AI that runs on your computer.** Auto captions use a local Whisper model. There is no cloud, no
account and no API key. More tools are listed in the AI tab as *Coming soon*.

**Opens other editors' projects.** Premiere Pro `.prproj` and Filmora `.wfp` files open as an
untitled Dafin project. Saving writes a `.drift` file and never touches the original.

**Export.** Presets for YouTube, TikTok, Instagram and WhatsApp, and GPU encoders for NVIDIA, AMD
and Intel, with an automatic fallback to the CPU if the GPU encoder fails.

**Learn as you go.** A Tutorial button opens a list of features, each with a guided tour that
outlines the real button or panel.

**Updates.** Installed Windows copies check this repository's newest GitHub release, verify the
installer's SHA-256 and update after you confirm. The previous version is kept in case you need to
go back.

The full list of what differs from Drift, with the files each change touches, is in
[DAFIN.md](DAFIN.md).

## How it works

Dafin is a C++ application with a Qt 6 and QML interface. FFmpeg handles decoding and encoding.
Drift's single compositor draws both the preview and the export, so what you see is what you get.
Dafin's changes sit on top of that engine, mostly in the QML interface and the asset panels.

## Download

Windows installers and portable builds are produced by the **Dafin release** workflow and
published on the [Releases](https://github.com/laur23212/Dafin/releases) page. Until a release is
published there, builds are available from the **Build** workflow on the
[Actions](https://github.com/laur23212/Dafin/actions) page. Linux builds (AppImage) come from the
same workflow. Building locally is described in [docs/BUILDING.md](docs/BUILDING.md).

## How we built it

Dafin is built by one person directing a small team:

- **The project owner** sets the goals, tests every build on Windows and decides what ships.
- **Claude** writes the code. Work is split into daily batches: each chat takes one task on its own
  branch and hands over a bundle, and one chat combines the bundles and pushes once.
  [CLAUDE.md](CLAUDE.md) holds the rules that keep several chats from overwriting each other.
- **ChatGPT** is the project's artist and makes the custom assets and templates, such as the sound
  effects.
- **GitHub Actions** builds everything, so no local toolchain is needed. A Windows installer
  takes about an hour the first time, while Skia and Qt are built and cached.

## For developers

- [DAFIN.md](DAFIN.md): what is different from Drift, stock media sources, building and publishing
- [CLAUDE.md](CLAUDE.md): rules for working on the repository with several chats at once
- [Building, testing, packaging, and architecture](docs/BUILDING.md)
- [GPU effects](docs/gpu-effects.md) and [GPU transitions](docs/gpu-transitions.md)

## Help and feedback

Found a bug or have an idea? Open an [issue](https://github.com/laur23212/Dafin/issues).

## Credits

Dafin is based on [Drift](https://github.com/CutWire-Studios/Drift) by CutWire Studios, and the
About box keeps that credit. Stock media comes from Pexels, Pixabay, Openverse, Jamendo and
Freesound under their own licenses.

## License

GPLv3, see [LICENSE](LICENSE).
