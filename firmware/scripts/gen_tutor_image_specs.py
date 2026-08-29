#!/usr/bin/env python3
"""Generate TutorImageSpecs.inc from kids_tutor PNG assets."""
from __future__ import annotations

import argparse
import json
import pathlib
import re
from collections import Counter

import numpy as np
from PIL import Image

PALETTE = {
    "R": (224, 64, 48),
    "G": (32, 192, 112),
    "B": (48, 144, 208),
    "P": (144, 80, 176),
    "O": (224, 112, 32),
    "Y": (240, 192, 0),
}
COL_IDX = {k: i for i, k in enumerate("RGBPOY")}
SH_IDX = {"C": 0, "S": 1, "T": 2, "V": 3}  # circle square triangle vbar


def nearest(rgb):
    best, bd = "R", 1e9
    for n, (r, g, b) in PALETTE.items():
        d = (rgb[0] - r) ** 2 + (rgb[1] - g) ** 2 + (rgb[2] - b) ** 2
        if d < bd:
            bd, best = d, n
    return best


def load_mask(path):
    im = Image.open(path).convert("RGBA")
    arr = np.array(im)
    rgb = arr[:, :, :3].astype(np.int16)
    a = arr[:, :, 3]
    white = ((rgb[:, :, 0] > 240) & (rgb[:, :, 1] > 240) & (rgb[:, :, 2] > 240)) | (a < 128)
    return rgb, ~white


def connected_components(mask):
    h, w = mask.shape
    visited = np.zeros_like(mask, dtype=bool)
    comps = []
    for y in range(h):
        for x in range(w):
            if not mask[y, x] or visited[y, x]:
                continue
            q = [(y, x)]
            visited[y, x] = True
            cells = []
            while q:
                cy, cx = q.pop()
                cells.append((cy, cx))
                for ny, nx in ((cy - 1, cx), (cy + 1, cx), (cy, cx - 1), (cy, cx + 1)):
                    if 0 <= ny < h and 0 <= nx < w and mask[ny, nx] and not visited[ny, nx]:
                        visited[ny, nx] = True
                        q.append((ny, nx))
            ys = [c[0] for c in cells]
            xs = [c[1] for c in cells]
            comps.append(
                {
                    "n": len(cells),
                    "ymin": min(ys),
                    "ymax": max(ys),
                    "xmin": min(xs),
                    "xmax": max(xs),
                    "cells": cells,
                }
            )
    return comps


def comp_color(rgb, comp):
    s = np.zeros(3)
    n = 0
    step = max(1, len(comp["cells"]) // 80)
    for y, x in comp["cells"][::step]:
        s += rgb[y, x]
        n += 1
    mean = tuple((s / max(1, n)).astype(int))
    return nearest(mean)


def classify_shape(comp):
    w = comp["xmax"] - comp["xmin"] + 1
    h = comp["ymax"] - comp["ymin"] + 1
    fill = comp["n"] / max(1, w * h)
    aspect = w / max(1, h)
    if aspect < 0.65:
        return "V"
    if fill > 0.82 and 0.85 <= aspect <= 1.2:
        return "S"
    if fill > 0.55 and 0.85 <= aspect <= 1.2:
        return "C"
    if fill <= 0.7:
        return "T"
    return "C"


def mode(xs):
    return Counter(xs).most_common(1)[0][0]


def analyze(path: pathlib.Path):
    rgb, mask = load_mask(path)
    comps = [c for c in connected_components(mask) if c["n"] >= 40]
    comps.sort(key=lambda c: (c["ymin"] // 16, c["xmin"]))
    items = []
    for c in comps:
        items.append(
            {
                "col": comp_color(rgb, c),
                "sh": classify_shape(c),
                "h": c["ymax"] - c["ymin"] + 1,
            }
        )
    return items


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--images",
        default=r"F:\Projects\stackchan_kids_tutor_jiwoo_lite_v4_5_1\sdcard\kids_tutor\images",
    )
    ap.add_argument(
        "--out",
        default=r"F:\Projects\stackchan_project\firmware\src\kids_tutor\TutorImageSpecs.inc",
    )
    args = ap.parse_args()
    img_dir = pathlib.Path(args.images)
    rows = []
    for p in sorted(img_dir.glob("*.png")):
        pref = re.match(r"([a-z_]+?)_\d+", p.stem).group(1)
        items = analyze(p)
        if pref == "fk_pattern":
            cols = [it["col"] for it in items[:5]]
            while len(cols) < 5:
                cols.append("R")
            rows.append((p.stem, "P", cols[:5], "S", 0, 0))
        elif pref in ("ws_count", "ws_add", "ws_sub"):
            if not items:
                continue
            sh = mode([it["sh"] for it in items])
            col = mode([it["col"] for it in items])
            if sh == "V":
                sh = "S"
            rows.append((p.stem, "N", [col], sh, len(items), 0))
        elif pref == "pl_shape":
            it = items[0]
            sh = it["sh"] if it["sh"] != "V" else "S"
            rows.append((p.stem, "S", [it["col"]], sh, 1, 0))
        elif pref in ("kf_cmp", "pl_measure"):
            if len(items) < 2:
                items = items + items
            hs = [it["h"] for it in items[:2]]
            mx = max(hs) or 1
            h1 = max(1, int(round(hs[0] / mx * 5)))
            h2 = max(1, int(round(hs[1] / mx * 5)))
            if h1 == h2:
                if hs[0] > hs[1]:
                    h1 = min(5, h2 + 1)
                elif hs[1] > hs[0]:
                    h2 = min(5, h1 + 1)
            rows.append((p.stem, "B", [items[0]["col"], items[1]["col"]], "V", h1, h2))
        else:
            raise SystemExit(f"unknown prefix {pref} in {p.name}")

    kind_map = {"P": "Pattern", "N": "Count", "S": "Single", "B": "Bars"}
    shape_map = {"C": "Circle", "S": "Square", "T": "Triangle", "V": "Square"}
    lines = [
        "// Auto-generated by scripts/gen_tutor_image_specs.py — do not edit by hand.",
        "// kind: Pattern(5 colors+?), Count(grid), Single, Bars(two heights 1..5)",
        "// color indices: 0=R 1=G 2=B 3=P 4=O 5=Y",
        "static const TutorImageSpec kTutorImageSpecs[] = {",
    ]
    for stem, kind, cols, sh, a, b in rows:
        cbytes = list(cols)
        while len(cbytes) < 5:
            cbytes.append(cols[0] if cols else "R")
        cbytes = cbytes[:5]
        col_init = ", ".join(str(COL_IDX[c]) for c in cbytes)
        lines.append(
            f'  {{"{stem}", TutorImgKind::{kind_map[kind]}, {{{col_init}}}, '
            f'TutorImgShape::{shape_map[sh]}, {a}, {b}}},'
        )
    lines.append("};")
    lines.append(f"static const int kTutorImageSpecCount = {len(rows)};")
    out = pathlib.Path(args.out)
    out.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"wrote {len(rows)} specs -> {out}")


if __name__ == "__main__":
    main()
