# Dafin

Dafin (Romanian for the laurel tree, the leaf in its icon) is a video editor for Windows and Linux based on
[Drift](https://github.com/CutWire-Studios/Drift) by CutWire Studios (GPL-3.0). It has a layout
familiar to Filmora users, a built-in Stock Media library and Cool Edit-style volume dots.
Dafin is not affiliated with Wondershare or CutWire Studios.

## What is different from Drift

| Area | Change | Files |
|---|---|---|
| Layout | New default workspace: library · info/properties · preview on top, timeline below | `resources/workspaces/filmora.json`, `WorkspaceStore.cpp` |
| Theme | Dark grey panels, teal accent, purple video / teal audio clips; default on first run | `resources/themes/filmora.json`, `ThemeManager.cpp` |
| Asset panel | Filmora-style icon+label category bar across the top (Media, Stock Media, Audio, Titles, Transitions, Effects, Stickers, Templates, then Drift's extras) | `AssetsPanelTopTabBar.qml`, `AssetsPanel.qml` |
| Online assets | Stock video, music, sound effects, stickers and title templates from free providers (Pexels, Pixabay, Openverse, Jamendo, Freesound, the bundled Dafin library) inside the asset panels, with previews, one-click download and Settings > Asset Providers for accounts | `src/app/online/library/`, `OnlineAssetBrowser.qml`, `asset-library/` |
| Lottie previews | Animated hover previews drawn by Drift's own Lottie renderer | `LottieFramePreviewImageProvider.*` |
| Volume dots | Cool Edit Pro-style volume line on every clip with sound: click the line to add a dot, drag dots to shape the volume (middle = 100 %, top = +6 dB, bottom = silent), Shift-drag for level only, double-click or right-click a dot to remove it. Uses Drift's volume keyframes, so playback and export follow it | `TimelineVolumeEnvelope.qml`, `TimelineTrackItem.cpp` |
| Name | Shown as Dafin everywhere (window, menus, About, installer). The About box keeps the credit to Drift. Settings live in their own "Dafin" folder; the in-app updater is off so it never offers upstream Drift. Settings and data from the app's earlier name, Cadra, move over on first start | `PreferencesController.cpp` (BrandingTranslator), `main.cpp`, `RenameMigration.cpp`, `installer/windows/Drift.iss`, `CMakeLists.txt` |
| Cleanup | Online downloads go to a temp folder that is deleted when Dafin closes (unless "Keep downloaded assets" is on). Media a saved project uses is embedded in the `.drift` file, so projects never lose it | `SessionFiles.cpp`, `ProjectFileController.cpp` (captureSave) |
| Movable buttons | Press and hold any button on the top bar, under the preview or on the timeline toolbar, then drag it elsewhere in that bar; drop a timeline action on More to move it into the menu. Reset in Settings > Toolbars | `BarReorder*.qml`, `BarSlot.qml`, `PreferencesController` (`barLayouts`) |

To switch the asset panel back to Drift's vertical rail, set `assets/tabStyle` to `rail` (Settings
UI for it is still to do). The old layout is still available as the "Edit" workspace.

## Stock Media sources

The tab is driven by a JSON catalog. On first run Drift copies the built-in one to your app data
folder (`%APPDATA%\Dafin\Dafin\free-library\catalog.json` on Windows,
`~/.local/share/Dafin/Dafin/free-library/catalog.json` on Linux) — use **Edit catalog** in the tab to open
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
   `Dafin-Setup-*.exe` / portable zip from the run's **Artifacts**.

Linux: same workflow with `linux` (AppImage) or `flatpak`, or build locally per `docs/BUILDING.md`.
