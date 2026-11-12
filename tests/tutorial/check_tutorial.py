#!/usr/bin/env python3
"""Dafin tutorial content check.

  * resources/tutorial/tutorial.json is valid and has the documented shape
  * feature ids are unique
  * every tour target ("a|b|c" is split on "|") resolves to a UI element in the QML sources, or is
    on the explicit known-missing list (tests/tutorial/known_missing.txt)

Usage: python3 -I check_tutorial.py <source dir>
"""
import json
import re
import sys
from pathlib import Path

root = Path(sys.argv[1] if len(sys.argv) > 1 else Path(__file__).resolve().parents[2])
errors = []


def read(rel):
    return (root / rel).read_text(encoding="utf-8")


# --- The JSON ---------------------------------------------------------------------------------
try:
    doc = json.loads(read("resources/tutorial/tutorial.json"))
except Exception as e:  # noqa: BLE001
    print(f"FAIL: tutorial.json is not valid JSON: {e}")
    sys.exit(1)

features = doc.get("features")
if not isinstance(features, list) or not features:
    print("FAIL: tutorial.json has no 'features' list")
    sys.exit(1)

seen = set()
targets = {}  # target id -> feature ids using it
steps = 0
for n, f in enumerate(features):
    where = f"features[{n}]"
    for key in ("id", "category", "title", "summary"):
        if not isinstance(f.get(key), str) or not f[key].strip():
            errors.append(f"{where}: '{key}' must be a non-empty string")
    fid = f.get("id")
    if fid in seen:
        errors.append(f"{where}: duplicate feature id '{fid}'")
    seen.add(fid)
    tour = f.get("tour")
    if not isinstance(tour, list):
        errors.append(f"{where} ({fid}): 'tour' must be a list")
        continue
    for m, step in enumerate(tour):
        steps += 1
        if not isinstance(step.get("text"), str) or not step["text"].strip():
            errors.append(f"{fid} step {m}: 'text' must be a non-empty string")
        target = step.get("target")
        if not isinstance(target, str) or not target.strip():
            errors.append(f"{fid} step {m}: 'target' must be a non-empty string")
            continue
        for part in target.split("|"):
            part = part.strip()
            if not part:
                errors.append(f"{fid} step {m}: empty part in target '{target}'")
            else:
                targets.setdefault(part, []).append(fid)

# --- What the QML sources provide ---------------------------------------------------------------
qml_dir = root / "src" / "qml"
provided = set()


def block_keys(text, start_marker):
    """Quoted keys at the top level of the `({ ... })` object that follows start_marker."""
    at = text.index(start_marker)
    end = text.index("\n    })", at)
    body = text[at:end]
    return re.findall(r'^\s{8}"([A-Za-z0-9_]+)"\s*:', body, re.M)


tab_ids = re.findall(r'ListElement\s*\{\s*tabId:\s*"([a-z0-9_]+)"', read("src/qml/AssetsPanel.qml"))
header_ids = block_keys(read("src/qml/EditorHeader.qml"), "readonly property var headerItems")
action_ids = block_keys(read("src/qml/components/timeline/TimelineToolbarActions.qml"),
                        "readonly property var actionMeta")

if 'tourPrefix: "bar."' not in read("src/qml/EditorHeader.qml"):
    errors.append("EditorHeader.qml no longer gives its buttons a 'bar.' tour prefix")
provided.update("bar." + i for i in header_ids)

for rel, prefix, ids in (
    ("src/qml/components/timeline/TimelineToolbarActionSlot.qml", "bar.", action_ids),
    ("src/qml/components/assets/AssetsPanelTopTabBar.qml", "assets.tab.", tab_ids),
    ("src/qml/components/assets/AssetsPanelTabButton.qml", "assets.tab.", tab_ids),
):
    line = next((ln for ln in read(rel).splitlines() if "tourId:" in ln), "")
    if f'"{prefix}"' not in line:
        errors.append(f"{rel}: no tourId with prefix '{prefix}'")
    else:
        provided.update(prefix + i for i in ids)

# Plain literals: `tourId: "panel.assets"`, `tourId: cond ? "panel.properties" : ""`.
for path in qml_dir.rglob("*.qml"):
    for line in path.read_text(encoding="utf-8").splitlines():
        if "tourId:" not in line:
            continue
        for lit in re.findall(r'"([A-Za-z]+(?:\.[A-Za-z]+)+)"', line.split("tourId:", 1)[1]):
            provided.add(lit)

# --- Compare ----------------------------------------------------------------------------------
known_missing = set()
for ln in read("tests/tutorial/known_missing.txt").splitlines():
    ln = ln.strip()
    if ln and not ln.startswith("#"):
        known_missing.add(ln)

for target in sorted(targets):
    if target in provided:
        if target in known_missing:
            errors.append(f"'{target}' has an element now: remove it from known_missing.txt")
    elif target not in known_missing:
        errors.append(f"target '{target}' (used by {', '.join(sorted(set(targets[target])))}) has no "
                      f"element in the code and is not in known_missing.txt")
for stale in sorted(known_missing - set(targets)):
    errors.append(f"known_missing.txt lists '{stale}', which no tour step uses")

if errors:
    print("FAIL:")
    for e in errors:
        print("  -", e)
    sys.exit(1)

print(f"OK: {len(features)} features, {steps} steps, {len(targets)} distinct targets, "
      f"{len(targets) - len(known_missing)} found in code, {len(known_missing)} known missing")
