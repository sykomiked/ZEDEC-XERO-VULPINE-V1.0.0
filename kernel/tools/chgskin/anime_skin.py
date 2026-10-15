#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Build the Chiglet's built-in Anime skin as a .zxcs file (zui_chgskin.h).

    python3 tools/chgskin/anime_skin.py [--zxcs out.zxcs] [--inc out.inc]

Regenerate, never hand-edit, src/zui/zui_chgskin_anime.inc. Coordinates are
skin units: 1024 is the body radius r, y points down, the box is +-2048.
The art is original to this project; it copies no existing character.
"""
import argparse
import math
import struct

# enums (zui_chgskin.h)
BODY, HEAD = 0, 1
NONE, EYE, PUPIL, SQUINT, MOPEN, SMILE, FROWN, BROW, BLUSH, TEAR, CURIOUS, EMBLEM = range(12)
FIXED, ACCENT, BODYP, HAIR, IRIS, TONE, INK, GROUND = range(8)
EXPRESSIVE = 1

shapes = []


def rgb(v):
    return ((v >> 16) & 255, (v >> 8) & 255, v & 255)


def recipe(dark=0, light=0):
    return (dark, light, 0)


def shape(pts, group=HEAD, react=NONE, paint=FIXED, col=(0, 0, 0), alpha=255, pivot=(0, 0), amt=0):
    shapes.append(dict(pts=pts, group=group, react=react, paint=paint, col=col, alpha=alpha,
                       pivot=pivot, amt=amt))


def ellipse(cx, cy, rx, ry):
    """8 quadratic arcs; control points sit at 1/cos(22.5 deg) of the radius."""
    k = 1.0 / math.cos(math.pi / 8)
    pts = []
    for i in range(8):
        a = i * math.pi / 4
        b = a + math.pi / 8
        pts.append((cx + rx * math.cos(a), cy + ry * math.sin(a), 1))
        pts.append((cx + rx * k * math.cos(b), cy + ry * k * math.sin(b), 0))
    return pts


def path(*items):
    """Items are ('L', x, y) or ('Q', cx, cy, x, y); the first is the start."""
    pts = []
    for it in items:
        if it[0] == 'Q':
            pts.append((it[1], it[2], 0))
            pts.append((it[3], it[4], 1))
        else:
            pts.append((it[1], it[2], 1))
    if len(pts) > 1 and pts[-1][2] and pts[-1][:2] == pts[0][:2]:
        pts.pop()  # closing point repeats the start
    return pts


def mirror(pts):
    return [(-x, y, on) for (x, y, on) in reversed(pts)]


def star(cx, cy, ro, ri, n=5):
    pts = []
    for i in range(2 * n):
        a = -math.pi / 2 + i * math.pi / n
        rr = ro if i % 2 == 0 else ri
        pts.append((cx + rr * math.cos(a), cy + rr * math.sin(a), 1))
    return pts


DARK_HAIR = recipe(150)
LASH = recipe(185)

# ---- behind: the back of the hair (a soft bob) ----------------------------
shape(ellipse(0, -330, 870, 810), paint=HAIR, col=recipe(55))

# ---- the body: a small jacket in its own body tint, hands in its tone -----
shape(ellipse(-540, 760, 125, 270), group=BODY, paint=BODYP, col=recipe(40))
shape(ellipse(540, 760, 125, 270), group=BODY, paint=BODYP, col=recipe(40))
shape(path(('L', -300, 380), ('Q', -470, 400, -500, 560), ('L', -540, 1120),
           ('Q', 0, 1190, 540, 1120), ('L', 500, 560), ('Q', 470, 400, 300, 380)),
      group=BODY, paint=BODYP, col=recipe(0, 40))
shape(path(('L', -300, 380), ('L', 0, 640), ('L', 300, 380), ('L', 190, 380), ('L', 0, 540),
           ('L', -190, 380)), group=BODY, paint=ACCENT)
shape(ellipse(-545, 1010, 95, 90), group=BODY, paint=TONE)
shape(ellipse(545, 1010, 95, 90), group=BODY, paint=TONE)
# chest emblem: its specialty, as in the classic look
shape(ellipse(0, 850, 85, 85), group=BODY, react=EMBLEM, amt=0, paint=ACCENT)
shape(path(('L', 0, 750), ('L', 100, 850), ('L', 0, 950), ('L', -100, 850)), group=BODY,
      react=EMBLEM, amt=1, paint=ACCENT)
shape(path(('L', -130, 815), ('L', 130, 815), ('L', 130, 885), ('L', -130, 885)), group=BODY,
      react=EMBLEM, amt=2, paint=ACCENT)
shape(star(0, 850, 110, 48), group=BODY, react=EMBLEM, amt=3, paint=ACCENT)

# ---- neck and head ---------------------------------------------------------
shape(path(('L', -110, 230), ('L', 110, 230), ('L', 110, 420), ('L', -110, 420)), group=BODY,
      paint=TONE, col=recipe(35))
head_l = [('L', 0, 380), ('Q', -330, 380, -560, 120), ('Q', -700, -60, -690, -300),
          ('Q', -680, -880, 0, -880)]
head = path(*head_l)
head = head + [(-x, y, on) for (x, y, on) in reversed(head[1:-1])]
shape(head, paint=TONE)

# ---- cheeks ----------------------------------------------------------------
for sx in (-1, 1):
    shape(ellipse(sx * 395, 40, 125, 62), react=BLUSH, col=rgb(0xFF8FA3), alpha=150)

# ---- eyes -----------------------------------------------------------------
for sx in (-1, 1):
    ex, ey = sx * 270, -150
    pv = (ex, ey)
    shape(ellipse(ex, ey, 172, 205), react=EYE, col=rgb(0xFFFFFF), pivot=pv)
    shape(ellipse(ex, ey + 15, 136, 182), react=PUPIL, paint=IRIS, pivot=pv, amt=60)
    shape(ellipse(ex, ey - 80, 132, 92), react=PUPIL, paint=IRIS, col=recipe(95), alpha=170,
          pivot=pv, amt=60)
    shape(ellipse(ex, ey + 20, 62, 86), react=PUPIL, paint=IRIS, col=recipe(205), pivot=pv,
          amt=60)
    shape(ellipse(ex - sx * 52, ey - 70, 50, 50), react=PUPIL, col=rgb(0xFFFFFF), alpha=245,
          pivot=pv, amt=60)
    shape(ellipse(ex + sx * 50, ey + 85, 25, 25), react=PUPIL, col=rgb(0xFFFFFF), alpha=220,
          pivot=pv, amt=60)
    # upper lash: a thick arc with a small wing at the outer corner
    lash = path(('L', ex - sx * 200, ey - 105), ('Q', ex - sx * 40, ey - 300, ex + sx * 175,
                                                 ey - 175),
                ('L', ex + sx * 225, ey - 205), ('L', ex + sx * 200, ey - 120),
                ('Q', ex - sx * 30, ey - 240, ex - sx * 185, ey - 70))
    shape(lash, react=EYE, paint=HAIR, col=LASH, pivot=pv)
    shape(path(('L', ex - sx * 95, ey + 195), ('Q', ex, ey + 225, ex + sx * 110, ey + 185),
               ('Q', ex, ey + 205, ex - sx * 95, ey + 195)),
          react=EYE, paint=HAIR, col=LASH, alpha=200, pivot=pv)
    # happy squint: a ^ arc
    shape(path(('L', ex - 175, ey + 30), ('Q', ex, ey - 200, ex + 175, ey + 30),
               ('L', ex + 130, ey + 50), ('Q', ex, ey - 110, ex - 130, ey + 50)),
          react=SQUINT, paint=HAIR, col=LASH)

# ---- nose and mouth ------------------------------------------------------
shape(ellipse(0, 70, 22, 14), paint=TONE, col=recipe(80))
MP = (0, 175)
shape(path(('L', -120, 160), ('Q', 0, 300, 120, 160), ('Q', 0, 220, -120, 160)), react=SMILE,
      col=rgb(0x7A2E3A), pivot=MP)
shape(path(('L', -110, 215), ('Q', 0, 80, 110, 215), ('Q', 0, 145, -110, 215)), react=FROWN,
      col=rgb(0x7A2E3A), pivot=MP)
shape(path(('L', -130, 150), ('Q', 0, 140, 130, 150), ('Q', 125, 340, 0, 345),
           ('Q', -125, 340, -130, 150)), react=MOPEN, col=rgb(0x5A1F2B), pivot=MP)
shape(ellipse(0, 290, 78, 46), react=MOPEN, col=rgb(0xE87C8C), pivot=MP)
shape(path(('L', -112, 152), ('Q', 0, 145, 112, 152), ('L', 100, 185), ('Q', 0, 178, -100, 185)),
      react=MOPEN, col=rgb(0xFFFFFF), alpha=230, pivot=MP)

# ---- a tear ----------------------------------------------------------------
shape(path(('L', -330, -10), ('Q', -255, 90, -285, 130), ('Q', -330, 175, -375, 130),
           ('Q', -405, 90, -330, -10)), react=TEAR, col=rgb(0x8FD0FF), alpha=220)

# ---- front hair: crown, jagged bangs and side locks -----------------------
bang_edge = [(610, -520), (520, -210), (380, -560), (230, -300), (90, -580), (-60, -290),
             (-210, -570), (-360, -330), (-500, -540), (-610, -200), (-690, -480)]
items = [('L', -745, -150), ('Q', -830, -700, -420, -930), ('Q', 0, -1090, 420, -930),
         ('Q', 830, -700, 745, -150)]
px, py = 745, -150
for (x, y) in bang_edge:
    mx, my = (px + x) / 2, (py + y) / 2
    items.append(('Q', mx + 35, my, x, y))  # each strand bows a little
    px, py = x, y
items.append(('Q', -720, -320, -745, -150))
shape(path(*items), paint=HAIR)
lock = path(('L', -745, -280), ('Q', -820, 160, -650, 470), ('Q', -660, 120, -575, -260))
shape(lock, paint=HAIR)
shape(mirror(lock), paint=HAIR)
# shine across the crown
shape(path(('L', -440, -770), ('Q', 0, -930, 440, -770), ('Q', 0, -850, -440, -770)), paint=HAIR,
      col=recipe(0, 110), alpha=170)
# a star clip in its accent colour
shape(star(470, -690, 95, 42), paint=ACCENT)

# ---- brows, over the bangs as drawn by hand -------------------------------
brow = path(('L', -410, -455), ('Q', -275, -540, -135, -490), ('L', -140, -455),
            ('Q', -275, -500, -405, -425))
shape(brow, react=BROW, paint=HAIR, col=DARK_HAIR, pivot=(-270, -470), amt=460)
shape(mirror(brow), react=BROW, paint=HAIR, col=DARK_HAIR, pivot=(270, -470), amt=-460)

# ---- curiosity: one to three stray strands on top -------------------------
for k, dx in enumerate((0, -170, 170)):
    shape(path(('L', dx - 30, -930), ('Q', dx - 230, -1260, dx + 70, -1390),
               ('Q', dx - 110, -1240, dx + 50, -935)), react=CURIOUS, amt=k, paint=HAIR)


def build(name="Anime", author="36N9 GENETICS LLC", licence="Apache-2.0"):
    out = bytearray(b"ZXCS")
    npts = sum(len(s["pts"]) for s in shapes)
    out += struct.pack("<BBHH", 1, EXPRESSIVE, len(shapes), npts)
    for t in (name, author, licence):
        b = t.encode("ascii")
        out += struct.pack("<B", len(b)) + b
    for s in shapes:
        r, g, b = s["col"]
        out += struct.pack("<BBBBBBBBhhhH", s["group"], s["react"], s["paint"], s["alpha"], r, g, b,
                           0, int(s["pivot"][0]), int(s["pivot"][1]), int(s["amt"]), len(s["pts"]))
    for s in shapes:
        for (x, y, on) in s["pts"]:
            xi, yi = int(round(x)), int(round(y))
            assert -2048 <= xi <= 2048 and -2048 <= yi <= 2048, (x, y)
            out += struct.pack("<hhB", xi, yi, on)
    return bytes(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--zxcs")
    ap.add_argument("--inc")
    a = ap.parse_args()
    data = build()
    if a.zxcs:
        open(a.zxcs, "wb").write(data)
    if a.inc:
        lines = ["/* Generated by tools/chgskin/anime_skin.py. Do not edit. */",
                 "/* SPDX-License-Identifier: Apache-2.0 */",
                 "static const uint8_t ZUI_CHGSKIN_ANIME[%d] = {" % len(data)]
        for i in range(0, len(data), 16):
            lines.append("    " + ", ".join("0x%02x" % v for v in data[i:i + 16]) + ",")
        lines.append("};")
        open(a.inc, "w").write("\n".join(lines) + "\n")
    print("%d shapes, %d points, %d bytes" % (len(shapes), sum(len(s["pts"]) for s in shapes),
                                               len(data)))


if __name__ == "__main__":
    main()
