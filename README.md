<p align="center">
  <img src="Drift_icon.png" alt="Dafin icon: a D with a laurel leaf" width="128" height="128">
</p>

<h1 align="center">Dafin</h1>

<p align="center">
  <strong>Create. Edit. Share.</strong><br>
  A free, open-source video editor for Windows and Linux, Filmora-inspired.
</p>

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/github/license/laur23212/Dafin?style=flat&labelColor=28272f&color=2fb8a6" alt="License: GPL-3.0" height="24"></a>
  <img src="https://img.shields.io/badge/platform-Windows%20%7C%20Linux-2fb8a6?style=flat&labelColor=28272f" alt="Platform: Windows | Linux" height="24">
  <img src="https://img.shields.io/badge/price-free%2C%20no%20watermark-8a5cf6?style=flat&labelColor=28272f" alt="Free, no watermark" height="24">
</p>

<p align="center">
  <a href="#download"><strong>Download</strong></a> ·
  <a href="#everything-in-one-place">Features</a> ·
  <a href="#how-we-built-it">How we built it</a> ·
  <a href="DAFIN.md">What's new vs Drift</a>
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

## Everything in one place

Dafin is Filmora-inspired in its layout: media library on the left, properties beside it,
preview on top, timeline below. Panels are dark grey with a teal accent, video clips are purple
and audio clips are teal.

| | |
|---|---|
| 🎬 **Stock Media** | Free video, music, stickers and title templates from Pexels, Pixabay, Openverse, Jamendo, Freesound and Dafin's own library. Preview, download in one click, drag to the timeline. |
| 🔊 **Audio** | 335 original **Dafin Sound Effects** that work offline (made by ChatGPT, royalty-free), loudness normalizing, beat markers and auto ducking. |
| 🔠 **Titles** | Title templates with quick controls for text, font, colours and speed. Premiere `.mogrt` titles stay editable, with no Adobe software. |
| ✨ **Effects and stickers** | Lottie stickers, chroma key in one click, adjustment layers, keyframes, speed ramps and motion tracking. |
| 🧩 **Templates** | Built-in split-screen layouts. Add your own Lottie files, sounds and templates by dragging them onto a tab. |
| 🤖 **AI** | An AI tab for tools that run on your computer: no cloud, no account, no API key. Auto captions come first, and the rest are marked *Coming soon*. |
| 🎚️ **Volume dots** | A feature Filmora lacks. Click the volume line on a clip, add dots and drag them to shape the loudness without splitting the clip. Inspired by Cool Edit Pro. |
| 📤 **Export** | One-click YouTube, TikTok, Instagram and WhatsApp presets, and NVIDIA, AMD and Intel GPU encoders with a CPU fallback. |
| 📂 **Open other projects** | Premiere Pro `.prproj` and Filmora `.wfp` files open as a Dafin project. The originals are never changed. |
| 🧭 **Tutorial** | Guided tours that outline the real button or panel, so you can learn while you edit. |

Make it yours: press and hold any button on the top bar, under the preview or on the timeline
toolbar, then drag it where you want it. Installed Windows copies update themselves from this
repository's newest release.

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
