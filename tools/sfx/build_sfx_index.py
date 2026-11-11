#!/usr/bin/env python3
"""Build Dafin's bundled sound-effects library from an SFX handoff folder.

    python3 tools/sfx/build_sfx_index.py <handoff-folder> [--out asset-library/sound-effects]

<handoff-folder> is the unzipped delivery (catalog.json + masters/*.wav, as ChatGPT hands it over,
e.g. Dafin_SFX_v4_Expanded). For every catalog item this script

  * encodes the 48 kHz WAV master to Ogg Opus (192 kbps) as <group>/<file>.ogg — .ogg rather than
    .opus because that is the extension Dafin's Media import already accepts;
  * levels it: integrated loudness above -8 LUFS is turned down to -8 (a few arcade/UI cues sat
    at -2.5 LUFS), and the decoded file is kept at or under -1 dBTP (Opus overshoots the WAV's
    sample peak, by up to +1.7 dB in the v4 delivery) — nothing is ever turned up;
  * draws 64 waveform peaks for the card;

and writes index.json, the file DafinSoundEffectsProvider reads from the app's resources.
Needs ffmpeg (with libopus) and numpy. Runs the same on Windows and Linux.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

import numpy as np

# The browser's sub-categories (sidebar rows) and the handoff's finer categories under each
# (filter chips). Every handoff category must appear exactly once.
GROUPS = [
    ("whooshes", "Whooshes & Swooshes", [
        ("whooshes_transitions", "Transitions"),
        ("air_whooshes", "Air & Soft"),
        ("cinematic_whooshes", "Cinematic & Bass"),
        ("reverse_whooshes", "Reverse & Build"),
        ("directional_swishes", "Fast & Directional"),
        ("stylized_whooshes", "Stylized & Sci-Fi"),
    ]),
    ("text-motion", "Text & Motion", [
        ("text_motion_swishes", "Titles & Logos"),
        ("text_sticker_swishes", "Text & Stickers"),
    ]),
    ("cinematic", "Cinematic & Impacts", [
        ("cinematic_impacts", "Impacts"),
        ("risers_downers", "Risers & Downers"),
        ("cinematic_horror", "Tension & Horror"),
    ]),
    ("ui", "UI Clicks & Pops", [
        ("ui_clicks_pops", "Clicks & Pops"),
        ("ui_tactile_clicks", "Tactile Clicks"),
        ("ui_bubble_pops", "Bubble Pops"),
        ("ui_toggles_switches", "Toggles & Switches"),
    ]),
    ("notifications", "Notifications & Bells", [
        ("notifications_bells", "Bells & Dings"),
        ("ui_notifications_expanded", "Notifications"),
        ("ui_feedback_status", "Success & Error"),
    ]),
    ("game", "Game & Cartoon", [
        ("ui_arcade_game", "Arcade & Game"),
        ("cartoon_comedy", "Cartoon & Comedy"),
    ]),
    ("scifi", "Sci-Fi & Glitch", [
        ("sci_fi_magic", "Sci-Fi & Magic"),
        ("glitch_digital", "Glitch & Digital"),
    ]),
]

LOUDNESS_CEILING = -8.0   # LUFS, integrated (only measurable on sounds of 0.4 s or more)
TRUE_PEAK_CEILING = -1.0  # dBTP of the decoded Opus
BITRATE = "192k"
PEAK_BINS = 64


def run(cmd):
    return subprocess.run(cmd, capture_output=True, text=True, check=False)


def measure(path):
    """(integrated LUFS or None, true peak dBTP) of a file as decoded."""
    r = run(["ffmpeg", "-hide_banner", "-nostats", "-i", path, "-af", "ebur128=peak=true", "-f", "null", "-"])
    lufs = re.findall(r"I:\s+(-?[\d.]+|-inf) LUFS", r.stderr)
    peak = re.findall(r"Peak:\s+(-?[\d.]+|-inf) dBFS", r.stderr)
    to_f = lambda s: -120.0 if s == "-inf" else float(s)
    lufs_v = to_f(lufs[-1]) if lufs else None
    if lufs_v is not None and lufs_v <= -69.0:  # too short for an integrated reading
        lufs_v = None
    return lufs_v, to_f(peak[-1]) if peak else 0.0


def encode(wav, out, gain_db):
    r = run(["ffmpeg", "-hide_banner", "-y", "-i", wav, "-af", f"volume={gain_db:.2f}dB",
             "-c:a", "libopus", "-b:a", BITRATE, "-vbr", "on", "-application", "audio",
             "-map_metadata", "-1", "-f", "ogg", out])
    if r.returncode != 0:
        raise RuntimeError(f"ffmpeg failed on {wav}: {r.stderr[-400:]}")


def peaks(path):
    r = subprocess.run(["ffmpeg", "-hide_banner", "-v", "error", "-i", path, "-ac", "1", "-ar", "8000",
                        "-f", "f32le", "-"], capture_output=True, check=True)
    s = np.abs(np.frombuffer(r.stdout, dtype=np.float32))
    if s.size == 0:
        return [0.0] * PEAK_BINS
    bins = np.array_split(s, PEAK_BINS)
    v = np.array([b.max() if b.size else 0.0 for b in bins])
    top = v.max() or 1.0
    return [round(float(x / top) * 0.95, 3) for x in v]  # the shape, scaled to the card


def build_item(item, src, out_dir, tmp):
    sub = item["category"]
    group = SUB_TO_GROUP[sub]
    stem = os.path.splitext(os.path.basename(item["file"]))[0]
    rel = f"{group}/{stem}.ogg"
    dest = os.path.join(out_dir, rel)
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    wav = os.path.join(src, item["wav_file"])

    lufs, _ = measure(wav)
    gain = min(0.0, LOUDNESS_CEILING - lufs) if lufs is not None else 0.0
    probe = os.path.join(tmp, item["id"] + ".ogg")
    for _ in range(4):
        encode(wav, probe, gain)
        out_lufs, tp = measure(probe)
        if tp <= TRUE_PEAK_CEILING:
            break
        gain -= (tp - TRUE_PEAK_CEILING) + 0.1
    shutil.move(probe, dest)

    words = set()
    for t in item.get("tags", []):
        words.update(t.replace("-", " ").replace("_", " ").split())
    words.update(item["name"].lower().split())
    words.add(item.get("mood", ""))
    words.update(SUB_NAMES[sub].lower().replace("&", " ").split())
    words.update(GROUP_NAMES[group].lower().replace("&", " ").split())
    words -= {"", "original", "synthesized", "designed", "sfx"}
    return {
        "id": item["id"],
        "title": item["name"],
        "group": group,
        "sub": sub,
        "file": rel,
        "duration": item["duration_sec"],
        "mood": item.get("mood", ""),
        "tags": " ".join(sorted(words)),
        "gainDb": round(gain, 2),
        "truePeak": tp,
        "lufs": out_lufs,
        "sha256": hashlib.sha256(open(dest, "rb").read()).hexdigest(),
        "peaks": peaks(dest),
    }


def release(handoff):
    try:
        report = json.load(open(os.path.join(handoff, "validation_report.json"), encoding="utf-8"))
        return str(report.get("package_version", ""))
    except (OSError, ValueError):
        return ""


SUB_TO_GROUP = {s: g for g, _, subs in GROUPS for s, _ in subs}
SUB_NAMES = {s: n for _, _, subs in GROUPS for s, n in subs}
GROUP_NAMES = {g: n for g, n, _ in GROUPS}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("handoff")
    ap.add_argument("--out", default=os.path.join(os.path.dirname(__file__), "..", "..", "asset-library", "sound-effects"))
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    args = ap.parse_args()

    catalog = json.load(open(os.path.join(args.handoff, "catalog.json"), encoding="utf-8"))
    items = catalog["items"]
    unknown = sorted({i["category"] for i in items} - set(SUB_TO_GROUP))
    if unknown:
        sys.exit(f"categories without a group in GROUPS: {unknown}")
    titles = [i["name"] for i in items]
    if len(set(titles)) != len(titles):
        sys.exit("two effects share a name")

    out_dir = os.path.abspath(args.out)
    if os.path.isdir(out_dir):
        shutil.rmtree(out_dir)
    os.makedirs(out_dir)
    with tempfile.TemporaryDirectory() as tmp, ThreadPoolExecutor(args.jobs) as ex:
        built = list(ex.map(lambda it: build_item(it, args.handoff, out_dir, tmp), items))

    counts = {}
    for b in built:
        counts[b["sub"]] = counts.get(b["sub"], 0) + 1
    index = {
        "version": 1,
        "library": catalog.get("library_id", "dafin-sfx"),
        "name": "Dafin Sound Effects",
        "release": release(args.handoff),
        "author": "Dafin Sound Design / ChatGPT",
        "license": "Royalty-Free Original (procedural synthesis)",
        "note": "Original synthesized sound design made for Dafin (no recordings, no third-party samples). "
                "No attribution needed. Not a CC0 dedication.",
        "groups": [{"id": g, "name": n, "subs": [{"id": s, "name": sn, "count": counts.get(s, 0)} for s, sn in subs]}
                   for g, n, subs in GROUPS],
        "items": built,
    }
    # One effect per line, so a later delivery diffs item by item.
    head = json.dumps({k: v for k, v in index.items() if k != "items"}, ensure_ascii=False, indent=1)
    lines = [json.dumps(b, ensure_ascii=False, separators=(",", ":")) for b in built]
    with open(os.path.join(out_dir, "index.json"), "w", encoding="utf-8", newline="\n") as f:
        f.write(head[:-2] + ',\n "items": [\n  ' + ",\n  ".join(lines) + "\n ]\n}\n")
    turned_down = sum(1 for b in built if b["gainDb"] < 0)
    print(f"{len(built)} effects, {turned_down} turned down, "
          f"max true peak {max(b['truePeak'] for b in built):.1f} dBTP -> {out_dir}")


if __name__ == "__main__":
    main()
