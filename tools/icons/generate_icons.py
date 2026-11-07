#!/usr/bin/env python3
"""Regenerate every packaged app icon from the single master image.

Master: Drift_icon.png at the repository root (Cadra's icon artwork, kept as supplied).
Outputs are plain downscales of it; nothing is recoloured, cropped or padded.

    python3 tools/icons/generate_icons.py          # needs Pillow

Writes:
  resources/windows/drift.ico          exe icon resource (Explorer, taskbar, Alt+Tab,
                                       Start menu and installer shortcuts, Inno Setup)
  resources/drift.png                  Qt window icon, splash and About dialog (256 px)
  installer/msix/Assets/*.png          MSIX tiles and taskbar/Start sizes
  flatpak/share/icons/hicolor/*/apps/  Linux desktop icon (Flatpak, AppImage, installs)
"""

from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
MASTER = ROOT / "Drift_icon.png"

ICO_SIZES = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
MSIX = {
    "Square44x44Logo.png": 44,
    "Square150x150Logo.png": 150,
    "StoreLogo.png": 50,
    "Square44x44Logo.targetsize-16_altform-unplated.png": 16,
    "Square44x44Logo.targetsize-24_altform-unplated.png": 24,
    "Square44x44Logo.targetsize-32_altform-unplated.png": 32,
    "Square44x44Logo.targetsize-48_altform-unplated.png": 48,
    "Square44x44Logo.targetsize-256_altform-unplated.png": 256,
}
HICOLOR = [64, 128, 256, 512, 1024]


def scaled(master: Image.Image, size: int) -> Image.Image:
    if master.size == (size, size):
        return master.copy()
    return master.resize((size, size), Image.Resampling.LANCZOS, reducing_gap=3.0)


def save_png(image: Image.Image, path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    image.save(path, "PNG", optimize=True)
    print(f"wrote {path.relative_to(ROOT)}  {image.size[0]}x{image.size[1]}")


def main() -> None:
    master = Image.open(MASTER).convert("RGBA")
    if master.size[0] != master.size[1]:
        raise SystemExit(f"{MASTER.name} must be square, got {master.size}")
    if master.size[0] < 1024:
        raise SystemExit(f"{MASTER.name} should be at least 1024 px, got {master.size[0]}")

    ico = ROOT / "resources/windows/drift.ico"
    # Every size is rendered from the master, not from the next size up, so each one is sharp.
    frames = [scaled(master, s) for s in ICO_SIZES]
    frames[-1].save(ico, format="ICO", sizes=[(s, s) for s in ICO_SIZES], append_images=frames[:-1])
    print(f"wrote {ico.relative_to(ROOT)}  sizes {ICO_SIZES}")

    save_png(scaled(master, 256), ROOT / "resources/drift.png")
    for name, size in MSIX.items():
        save_png(scaled(master, size), ROOT / "installer/msix/Assets" / name)
    for size in HICOLOR:
        save_png(scaled(master, size),
                 ROOT / f"flatpak/share/icons/hicolor/{size}x{size}/apps/org.cutwire.Drift.png")


if __name__ == "__main__":
    main()
