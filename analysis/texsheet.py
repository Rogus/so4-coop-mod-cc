#!/usr/bin/env python3
"""Rank the .dds textures saved by the mod (so4coop_textures/) by how likely they hold the Xbox button
icons, and write PNG previews.

usage: texsheet.py <dds dir> <out dir> [top N]

Score: pixels (opaque) near each Xbox face-button colour - A green, B red, X blue, Y yellow. An icon
sheet has all four, so the ranking key is the smallest of the four counts. Output:
  <out>/ranking.txt        every texture with its four counts
  <out>/top_NN_<name>.png  the top N, downscaled to at most 2048 px
  <out>/contact.png        thumbnails of the top N with their rank
"""
import glob
import os
import sys

import numpy as np
from PIL import Image, ImageDraw

COLOURS = {"A": (96, 176, 48), "B": (210, 50, 40), "X": (40, 110, 220), "Y": (240, 190, 30)}


def counts(img):
    a = np.asarray(img.convert("RGBA"), dtype=np.int16)
    vis = a[..., 3] > 160
    rgb = a[..., :3]
    out = {}
    for k, c in COLOURS.items():
        d = np.abs(rgb - np.array(c, dtype=np.int16)).sum(axis=2)
        out[k] = int(((d < 90) & vis).sum())
    return out


def main():
    src, dst = sys.argv[1], sys.argv[2]
    top = int(sys.argv[3]) if len(sys.argv) > 3 else 30
    os.makedirs(dst, exist_ok=True)
    rows, bad = [], 0
    files = sorted(glob.glob(os.path.join(src, "*.dds")))
    for i, f in enumerate(files):
        try:
            im = Image.open(f)
            im.load()
        except Exception:
            bad += 1
            continue
        w, h = im.size
        s = im
        if max(w, h) > 4096:
            s = im.reduce(max(w, h) // 4096)
        c = counts(s)
        rows.append((min(c.values()), sum(c.values()), os.path.basename(f), w, h, c))
        if (i + 1) % 200 == 0:
            print(f"  {i + 1}/{len(files)}", file=sys.stderr)
    rows.sort(key=lambda r: (r[0], r[1]), reverse=True)
    with open(os.path.join(dst, "ranking.txt"), "w") as o:
        o.write(f"{len(rows)} textures read, {bad} unreadable\n")
        for r in rows:
            c = r[5]
            o.write(f"{r[0]:8d} {r[1]:9d}  {r[2]}  {r[3]}x{r[4]}  A {c['A']} B {c['B']} X {c['X']} Y {c['Y']}\n")
    thumbs = []
    for n, r in enumerate(rows[:top]):
        im = Image.open(os.path.join(src, r[2]))
        im.load()
        im = im.convert("RGBA")
        if max(im.size) > 2048:
            im.thumbnail((2048, 2048))
        im.save(os.path.join(dst, f"top_{n:02d}_{os.path.splitext(r[2])[0]}.png"))
        t = im.copy()
        t.thumbnail((256, 256))
        thumbs.append((n, r, t))
    if thumbs:
        cols = 6
        sheet = Image.new("RGBA", (cols * 264, ((len(thumbs) + cols - 1) // cols) * 284), (40, 40, 48, 255))
        d = ImageDraw.Draw(sheet)
        for n, r, t in thumbs:
            x, y = (n % cols) * 264 + 4, (n // cols) * 284 + 4
            sheet.alpha_composite(t, (x, y + 20))
            d.text((x, y + 2), f"#{n} {r[3]}x{r[4]} min {r[0]}", fill=(255, 255, 255, 255))
        sheet.save(os.path.join(dst, "contact.png"))
    print(f"{len(rows)} textures read, {bad} unreadable; top {min(top, len(rows))} written to {dst}")


if __name__ == "__main__":
    main()
