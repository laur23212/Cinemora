# Cadra

Cadra (from the Romanian *cadru*, a film frame) is a video editor for Windows and Linux based on
[Drift](https://github.com/CutWire-Studios/Drift) by CutWire Studios (GPL-3.0). It has a layout
familiar to Filmora users, a built-in Stock Media library and Cool Edit-style volume dots.
Cadra is not affiliated with Wondershare or CutWire Studios.

## What is different from Drift

| Area | Change | Files |
|---|---|---|
| Layout | New default workspace: library · info/properties · preview on top, timeline below | `resources/workspaces/filmora.json`, `WorkspaceStore.cpp` |
| Theme | Dark grey panels, teal accent, purple video / teal audio clips; default on first run | `resources/themes/filmora.json`, `ThemeManager.cpp` |
| Asset panel | Filmora-style icon+label category bar across the top (Media, Stock Media, Audio, Titles, Transitions, Effects, Stickers, Templates, then Drift's extras) | `AssetsPanelTopTabBar.qml`, `AssetsPanel.qml` |
| Stock Media | Free stock video (Pexels, Pixabay), sound effects (Freesound) and Lottie animations, with hover previews and a download button | `src/app/online/free/`, `StockMediaTab.qml`, `StockMediaCard.qml`, `resources/free-library/catalog.json` |
| Lottie previews | Animated hover previews drawn by Drift's own Lottie renderer | `LottieFramePreviewImageProvider.*` |
| Volume dots | Cool Edit Pro-style volume line on every clip with sound: click the line to add a dot, drag dots to shape the volume (middle = 100 %, top = +6 dB, bottom = silent), Shift-drag for level only, double-click or right-click a dot to remove it. Uses Drift's volume keyframes, so playback and export follow it | `TimelineVolumeEnvelope.qml`, `TimelineTrackItem.cpp` |
| Name | Shown as Cadra everywhere (window, menus, About, installer). The About box keeps the credit to Drift. Settings live in their own "Cadra" folder; the in-app updater is off so it never offers upstream Drift | `PreferencesController.cpp` (BrandingTranslator), `main.cpp`, `installer/windows/Drift.iss`, `CMakeLists.txt` |
| Cleanup | Everything downloaded from Stock Media goes to a temp folder that is deleted when Drift closes. Media a saved project uses is embedded in the `.drift` file, so projects never lose it | `FreeLibrary.cpp`, `ProjectFileController.cpp` (captureSave) |

To switch the asset panel back to Drift's vertical rail, set `assets/tabStyle` to `rail` (Settings
UI for it is still to do). The old layout is still available as the "Edit" workspace.

## Stock Media sources

The tab is driven by a JSON catalog. On first run Drift copies the built-in one to your app data
folder (`%APPDATA%\Cadra\Cadra\free-library\catalog.json` on Windows,
`~/.local/share/Cadra/Cadra/free-library/catalog.json` on Linux) — use **Edit catalog** in the tab to open
it, then **Reload catalog**. The `_help` block at the top of the file explains the format; any JSON
search API can be added without code changes.

Pexels, Pixabay and Freesound need a free API key each. The tab shows a **Get a free key** button
and a field to paste it; keys are stored in Drift's settings, not in the catalog.

Lottie: LottieFiles' search API needs a signed-in account, so it is not browsable here. On
lottiefiles.com open a free animation, copy its **Lottie JSON** URL and paste it into the tab's link
box — it is downloaded and also saved under "My Lotties" in the catalog for next time.

## Building the Windows installer with GitHub (no local toolchain)

1. Push this branch to your fork of Drift.
2. On GitHub: **Actions** → **Build** → **Run workflow**, pick this branch, platforms = `windows`.
3. When it finishes (first run ≈ 1 h because Skia and Qt are built/cached), download the
   `Cadra-Setup-*.exe` / portable zip from the run's **Artifacts**.

Linux: same workflow with `linux` (AppImage) or `flatpak`, or build locally per `docs/BUILDING.md`.
