#!/usr/bin/env python3
"""Turn a supplied app icon into a clean master (Drift_icon.png) with transparent corners.

Icon makers often export the rounded tile on a solid square (black corners), and sometimes only
as a small .ico. This finds the tile's edge, fits a rounded rectangle to it, centres the tile,
scales it to the master size and cuts it out with a smooth anti-aliased edge. Edge pixels that
were blended with the background take the colour of the tile's rim, so no dark fringe is left.

    python3 tools/icons/make_master.py new_icon.ico        # or .png; needs Pillow, NumPy, SciPy, OpenCV
    python3 tools/icons/generate_icons.py                  # then regenerate every packaged icon

Options: --out (default Drift_icon.png), --size (default 1024), --fill (tile width / canvas,
default 0.9375).
"""

import argparse
from pathlib import Path

import cv2
import numpy as np
from PIL import Image
from scipy.ndimage import distance_transform_edt, maximum_filter
from scipy.optimize import least_squares

ROOT = Path(__file__).resolve().parents[2]


def load_largest(path: Path) -> np.ndarray:
    image = Image.open(path)
    if image.format == "ICO":
        image.size = max(image.ico.sizes())
    rgba = np.array(image.convert("RGBA")).astype(np.float64)
    if rgba.shape[0] != rgba.shape[1]:
        raise SystemExit(f"{path.name} must be square, got {rgba.shape[1]}x{rgba.shape[0]}")
    return rgba


def edge_strength(rgba: np.ndarray) -> np.ndarray:
    """How far each pixel is from the background around the tile (0 = background)."""
    corners = np.stack([rgba[0, 0], rgba[0, -1], rgba[-1, 0], rgba[-1, -1]])
    if corners[:, 3].max() < 8:  # corners already transparent: the alpha is the tile
        return rgba[..., 3]
    bg = np.median(corners, axis=0)
    return np.abs(rgba[..., :3] - bg[:3]).max(axis=2)


def edge_points(d: np.ndarray) -> np.ndarray:
    """Sub-pixel points where each row/column first and last reaches half its rim brightness."""

    def crossings(line):
        found = np.where(line > 25)[0]
        if len(found) == 0:
            return []
        out = []
        for side in (0, 1):
            seq = line if side == 0 else line[::-1]
            start = found[0] if side == 0 else len(line) - 1 - found[-1]
            level = 0.5 * seq[start:start + 5].max()
            j = start
            while j < len(seq) and seq[j] < level:
                j += 1
            if j == 0 or j >= len(seq):
                continue
            pos = (j - 0.5) + (level - seq[j - 1]) / (seq[j] - seq[j - 1])
            out.append(pos if side == 0 else len(line) - pos)
        return out

    pts = [(x, y + 0.5) for y in range(d.shape[0]) for x in crossings(d[y])]
    pts += [(x + 0.5, y) for x in range(d.shape[1]) for y in crossings(d[:, x])]
    return np.array(pts)


def rounded_rect_sdf(px, py, cx, cy, a, b, r):
    """Signed distance (negative inside) to a rounded rectangle with circular corners."""
    qx = np.abs(px - cx) - (a - r)
    qy = np.abs(py - cy) - (b - r)
    outside = np.hypot(np.maximum(qx, 0), np.maximum(qy, 0))
    return outside + np.minimum(np.maximum(qx, qy), 0) - r


def fit_tile(pts: np.ndarray, size: int):
    def resid(p):
        return rounded_rect_sdf(pts[:, 0], pts[:, 1], *p)

    p0 = [size / 2, size / 2, size * 0.47, size * 0.47, size * 0.2]
    fit = least_squares(resid, p0, bounds=([0, 0, 4, 4, 1], [size, size, size, size, size / 2]))
    rms = float(np.sqrt((resid(fit.x) ** 2).mean()))
    return fit.x, rms


def bleed_rim(rgba: np.ndarray, d: np.ndarray, tile) -> np.ndarray:
    """Replace background and half-blended edge pixels with the nearest real tile colour."""
    n = rgba.shape[0]
    ys, xs = np.mgrid[0:n, 0:n] + 0.5
    sd = rounded_rect_sdf(xs, ys, *tile)
    bright_rim = (d >= 0.8 * maximum_filter(d, size=5)) & (d >= 20) & (sd <= 1.5)
    trusted = (sd <= -2.5) | bright_rim
    iy, ix = distance_transform_edt(~trusted, return_distances=False, return_indices=True)
    return rgba[..., :3][iy, ix]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("source", type=Path)
    parser.add_argument("--out", type=Path, default=ROOT / "Drift_icon.png")
    parser.add_argument("--size", type=int, default=1024)
    parser.add_argument("--fill", type=float, default=0.9375)
    args = parser.parse_args()

    rgba = load_largest(args.source)
    n = rgba.shape[0]
    d = edge_strength(rgba)
    tile, rms = fit_tile(edge_points(d), n)
    cx, cy, a, b, r = tile
    print(f"source {n}px: tile centre ({cx:.1f}, {cy:.1f}), size {2*a:.1f} x {2*b:.1f}, "
          f"corner radius {r:.1f}, edge fit within {rms:.2f}px rms")

    rgb = bleed_rim(rgba, d, tile)

    # Centre the tile and scale it so it is square and fills `fill` of the canvas.
    m = args.size
    half = m * args.fill / 2
    sx, sy = a / half, b / half
    # source pixel-centre coordinate for output pixel centre X: cx + (X + 0.5 - m/2) * sx - 0.5
    matrix = np.array([[sx, 0, cx + (0.5 - m / 2) * sx - 0.5],
                       [0, sy, cy + (0.5 - m / 2) * sy - 0.5]])
    big = cv2.warpAffine(rgb.astype(np.float32), matrix, (m, m),
                         flags=cv2.INTER_LANCZOS4 | cv2.WARP_INVERSE_MAP,
                         borderMode=cv2.BORDER_REPLICATE)
    big = np.clip(big, 0, 255)

    radius = r * half / ((a + b) / 2)
    ys, xs = np.mgrid[0:m, 0:m] + 0.5
    alpha = np.clip(0.5 - rounded_rect_sdf(xs, ys, m / 2, m / 2, half, half, radius), 0, 1)

    out = np.dstack([big, alpha * 255]).round().astype(np.uint8)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(out, "RGBA").save(args.out, "PNG", optimize=True)
    print(f"wrote {args.out}  {m}x{m}, tile {2*half:.0f}px, corner radius {radius:.0f}px")
    if n < m:
        print(f"note: the source is only {n}px, so sizes above {n}px are upscaled")


if __name__ == "__main__":
    main()
