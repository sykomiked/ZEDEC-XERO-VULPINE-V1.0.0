#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""tools/brand/vectorize_icons.py PNG_DIR OUT_DIR FIRST LAST: trace the app icons FIRST..LAST
(NNN-name.png) into SVGs in the L1 icons' style: one gold (#F2C230) even-odd
path on the deep green (#002911) chassis, viewBox 0 0 1024 1024.

The shape is the PNG's alpha silhouette, which is what the hand-built L1 SVGs
follow (IoU 0.81-0.91 against them), with bright strokes cut out as gaps. Method: average the alpha down to 627 px,
trace the 50% iso-line with marching squares (sub-pixel, saddles resolved by
the cell's mean), simplify each ring with Ramer-Douglas-Peucker, drop specks,
and widen the tolerance until the file is under the byte budget. Integers
and plain Python plus numpy; same input, same bytes out.
"""
import os
import sys

import numpy as np
from PIL import Image

GOLD, GREEN = "#F2C230", "#002911"
BUDGET = 28 * 1024
GRID = 627          # trace resolution (half the 1254 px source)
MIN_AREA = 6.0      # rings smaller than this (trace px^2) are dropped


def load_alpha(path):
    """The shape field: the alpha, with the bright strokes (the Crystal series'
    cyan facet lines, luminance over ~100) cut out as gaps, the way the
    Dragon series' line work is already gaps in its alpha."""
    im = Image.open(path).convert("RGBA")
    if im.size != (GRID * 2, GRID * 2):
        im = im.resize((GRID * 2, GRID * 2), Image.LANCZOS)
    px = np.asarray(im).astype(np.float64)
    lum = (px[..., 0] * 299 + px[..., 1] * 587 + px[..., 2] * 114) / 1000
    a = px[..., 3] * np.clip((130.0 - lum) / 60.0, 0.0, 1.0)
    a = a.reshape(GRID, 2, GRID, 2).mean(axis=(1, 3))
    # a one-pixel transparent border so every ring closes
    return np.pad(a, 1)


def trace(a, level=127.5):
    """Closed rings (lists of (x, y)) of the iso-line a == level."""
    h, w = a.shape
    inside = a > level
    # edge crossing points, keyed by edge id
    def ex(y, x):  # horizontal edge from (x, y) to (x+1, y)
        t = (level - a[y, x]) / (a[y, x + 1] - a[y, x])
        return (x + t, y)

    def ey(y, x):  # vertical edge from (x, y) to (x, y+1)
        t = (level - a[y, x]) / (a[y + 1, x] - a[y, x])
        return (x, y + t)

    c = (inside[:-1, :-1] * 1) | (inside[:-1, 1:] * 2) | (inside[1:, 1:] * 4) | (inside[1:, :-1] * 8)
    ys, xs = np.nonzero((c != 0) & (c != 15))
    nxt = {}
    # edges: T top (y,x,'h'), B bottom (y+1,x,'h'), L left (y,x,'v'), R right (y,x+1,'v')
    for y, x in zip(ys.tolist(), xs.tolist()):
        k = int(c[y, x])
        T, B, L, R = (y, x, 0), (y + 1, x, 0), (y, x, 1), (y, x + 1, 1)
        if k in (5, 10):
            mid = (a[y, x] + a[y, x + 1] + a[y + 1, x] + a[y + 1, x + 1]) / 4 > level
            if k == 5:
                segs = [(L, T), (R, B)] if not mid else [(L, B), (R, T)]
            else:
                segs = [(T, R), (B, L)] if not mid else [(T, L), (B, R)]
        else:
            segs = {1: [(L, T)], 2: [(T, R)], 3: [(L, R)], 4: [(R, B)], 6: [(T, B)], 7: [(L, B)],
                    8: [(B, L)], 9: [(B, T)], 11: [(B, R)], 12: [(R, L)], 13: [(R, T)],
                    14: [(T, L)]}[k]
        for s, e in segs:
            nxt[s] = e
    rings, seen = [], set()
    for start in list(nxt):
        if start in seen:
            continue
        ring, cur = [], start
        while cur not in seen and cur in nxt:
            seen.add(cur)
            y, x, v = cur
            ring.append(ey(y, x) if v else ex(y, x))
            cur = nxt[cur]
        if len(ring) >= 3:
            rings.append(ring)
    return rings


def area(r):
    s = 0.0
    for i in range(len(r)):
        x1, y1 = r[i - 1]
        x2, y2 = r[i]
        s += x1 * y2 - x2 * y1
    return abs(s) / 2


def rdp(pts, eps):
    if len(pts) < 3:
        return pts
    keep = [False] * len(pts)
    keep[0] = keep[-1] = True
    stack = [(0, len(pts) - 1)]
    while stack:
        i, j = stack.pop()
        x1, y1 = pts[i]
        x2, y2 = pts[j]
        dx, dy = x2 - x1, y2 - y1
        n = (dx * dx + dy * dy) ** 0.5 or 1e-12
        best, bi = -1.0, -1
        for k in range(i + 1, j):
            x0, y0 = pts[k]
            d = abs(dy * x0 - dx * y0 + x2 * y1 - y2 * x1) / n
            if d > best:
                best, bi = d, k
        if best > eps:
            keep[bi] = True
            stack += [(i, bi), (bi, j)]
    return [p for p, k in zip(pts, keep) if k]


def simplify_ring(r, eps):
    # split the ring at its farthest point so RDP sees an open line
    far = max(range(len(r)), key=lambda i: (r[i][0] - r[0][0]) ** 2 + (r[i][1] - r[0][1]) ** 2)
    a = rdp(r[:far + 1], eps)
    b = rdp(r[far:] + [r[0]], eps)
    return a[:-1] + b[:-1]


def fmt(v):
    s = "%.1f" % v
    return s[:-2] if s.endswith(".0") else s


def path_d(rings, scale):
    out = []
    for r in rings:
        pts = [(fmt((x - 1) * scale), fmt((y - 1) * scale)) for x, y in r]
        d = ["M%s %s" % pts[0]]
        d += ["L%s %s" % p for p in pts[1:]]
        out.append("".join(d) + "Z")
    return "".join(out)


def title_of(fn):
    return " ".join(w.capitalize() for w in os.path.splitext(fn)[0].split("-")[1:])


def svg(fn, d):
    return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1024 1024" width="1024" height="1024">'
            '<title>%s</title><g id="chassis"><rect width="1024" height="1024" fill="%s"/>'
            '<path fill="%s" fill-rule="evenodd" d="%s"/></g></svg>\n' % (title_of(fn), GREEN, GOLD, d))


def vectorize(png, fn):
    a = load_alpha(png)
    rings = [r for r in trace(a) if area(r) >= MIN_AREA]
    scale = 1024.0 / GRID
    for eps in (0.45, 0.6, 0.8, 1.0, 1.3, 1.7, 2.2):
        rs = [s for s in (simplify_ring(r, eps) for r in rings) if len(s) >= 3 and area(s) >= MIN_AREA]
        out = svg(fn, path_d(rs, scale))
        if len(out.encode()) <= BUDGET:
            return out, eps, len(rs)
    raise SystemExit("%s: over %d bytes even at the widest tolerance" % (fn, BUDGET))


def main():
    src, dst, first, last = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
    os.makedirs(dst, exist_ok=True)
    files = sorted(f for f in os.listdir(src) if f.endswith(".png") and f[:3].isdigit()
                   and first <= int(f[:3]) <= last)
    if len(files) != last - first + 1:
        raise SystemExit("want %d icons %03d..%03d, found %d" % (last - first + 1, first, last, len(files)))
    for f in files:
        out, eps, n = vectorize(os.path.join(src, f), f)
        name = os.path.splitext(f)[0] + ".svg"
        with open(os.path.join(dst, name), "w") as fh:
            fh.write(out)
        print("%s %6d bytes  eps %.2f  %d rings" % (name, len(out.encode()), eps, n))


if __name__ == "__main__":
    main()
