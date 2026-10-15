#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""make_fixtures.py - the vectoriser's test pictures and clip.

Writes kernel/src/vtrace/test/fx_vtrace.inc (C byte arrays). Every picture is
drawn here from a fixed recipe, so the file can be regenerated and reviewed:

  fx_pixel_png   32x32 pixel-art sprite, 7 flat colours plus transparency
                 (PNG RGBA, alpha only 0 or 255): pixel-exact mode must give
                 back the identical picture.
  fx_logo_png    128x128 flat logo, anti-aliased (drawn at 8x and averaged
                 down): a ring with a hole, a rounded square, a triangle, a
                 bar; 5 colours on white.
  fx_photo_jpg   96x96 photo-like scene (sky gradient, sun, two hills with
                 noise texture, a tree), JPEG quality 85: an honest
                 approximation case.
  fx_clip_y4m    48x32, 14 frames at 25 fps, 4:2:0: a still scene with a small
                 square moving across it, and a cut to a different scene at
                 frame 9 (scene change -> keyframe).
  fx_anim_gif    24x16 animated GIF, 6 frames, delays 10/10/20/10/10/30 cs: the
                 GIF path of the frame iterator.

  python3 tools/vtrace/make_fixtures.py
  python3 tools/vtrace/make_fixtures.py --corpus DIR   (also the same five as
        files in DIR: a starting corpus for vtrace --bench)
"""
import io
import os
import random
import sys

from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "..", "kernel", "src", "vtrace", "test", "fx_vtrace.inc")


def png(im):
    b = io.BytesIO()
    im.save(b, "PNG", optimize=False)
    return b.getvalue()


def pixel_art():
    pal = {".": (0, 0, 0, 0), "k": (20, 20, 30, 255), "o": (230, 120, 30, 255),
           "y": (250, 210, 60, 255), "w": (250, 250, 245, 255), "g": (60, 170, 80, 255),
           "b": (50, 90, 200, 255), "r": (200, 40, 50, 255)}
    rows = [
        "................................",
        "..........kkkkkkkkkkkk..........",
        "........kkooooooooooookk........",
        "......kkooooyyyyyyyyooookk......",
        ".....koooyyyyyyyyyyyyyyooook....",
        "....kooyyyywwwyyyyyywwwyyyook...",
        "...kooyyyywwkkwyyyywwkkwyyyok...",
        "...kooyyyywwkkwyyyywwkkwyyyook..",
        "..kooyyyyyywwwyyyyyywwwyyyyyok..",
        "..kooyyyyyyyyyyyyyyyyyyyyyyyook.",
        "..koyyyyrryyyyyyyyyyyyyrryyyyok.",
        "..koyyyyrryyyyyyyyyyyyyrryyyyok.",
        "..koyyyyyyykyyyyyyyykyyyyyyyyok.",
        "..kooyyyyyyykkkkkkkkyyyyyyyyook.",
        "...kooyyyyyyyyyyyyyyyyyyyyyook..",
        "...kooyyyyyyyyyyyyyyyyyyyyyook..",
        "....koooyyyyyyyyyyyyyyyyyooook..",
        ".....kkoooooyyyyyyyyyoooookk....",
        ".......kkkoooooooooooookkk......",
        "..........kkkkkkkkkkkkk.........",
        "............kggggggk............",
        "...........kggggggggk...........",
        "..........kgggbbbbgggk..........",
        ".........kgggbbbbbbgggk.........",
        ".........kggbbwwwwbbggk.........",
        ".........kggbbwwwwbbggk.........",
        ".........kgggbbbbbbgggk.........",
        "..........kgggbbbbgggk..........",
        "...........kggggggggk...........",
        "............kkkkkkkk............",
        "................................",
        "................................",
    ]
    im = Image.new("RGBA", (32, 32))
    for y, row in enumerate(rows):
        for x, ch in enumerate(row[:32].ljust(32, ".")):
            im.putpixel((x, y), pal[ch])
    return png(im)


def logo():
    S = 8
    im = Image.new("RGB", (128 * S, 128 * S), (255, 255, 255))
    d = ImageDraw.Draw(im)
    d.ellipse([10 * S, 10 * S, 70 * S, 70 * S], fill=(200, 30, 60))      # ring
    d.ellipse([26 * S, 26 * S, 54 * S, 54 * S], fill=(255, 255, 255))    # its hole
    d.ellipse([35 * S, 35 * S, 45 * S, 45 * S], fill=(30, 60, 160))      # a dot in the hole
    d.rounded_rectangle([76 * S, 12 * S, 118 * S, 54 * S], radius=10 * S, fill=(30, 60, 160))
    d.polygon([(20 * S, 118 * S), (60 * S, 78 * S), (100 * S, 118 * S)], fill=(250, 180, 20))
    d.rectangle([70 * S, 62 * S, 120 * S, 70 * S], fill=(40, 40, 40))
    d.polygon([(76 * S, 88 * S), (118 * S, 80 * S), (112 * S, 110 * S)], fill=(40, 150, 90))
    im = im.resize((128, 128), Image.BOX)
    return png(im)


def photo():
    rnd = random.Random(2026)
    W, H = 96, 96
    im = Image.new("RGB", (W, H))
    px = im.load()
    for y in range(H):
        for x in range(W):
            t = y / H
            px[x, y] = (int(90 + 80 * t), int(140 + 60 * t), int(230 - 30 * t))
    d = ImageDraw.Draw(im)
    d.ellipse([62, 10, 84, 32], fill=(255, 230, 150))
    d.polygon([(0, 60), (30, 40), (60, 58), (96, 46), (96, 96), (0, 96)], fill=(70, 120, 60))
    d.polygon([(0, 80), (40, 64), (96, 78), (96, 96), (0, 96)], fill=(50, 95, 40))
    d.rectangle([18, 48, 22, 70], fill=(80, 55, 30))
    d.ellipse([8, 30, 32, 54], fill=(40, 100, 45))
    im = im.filter(ImageFilter.GaussianBlur(0.8))
    px = im.load()
    for y in range(H):
        for x in range(W):
            r, g, b = px[x, y]
            n = rnd.randint(-10, 10) if y > 44 else rnd.randint(-3, 3)
            px[x, y] = tuple(max(0, min(255, v + n)) for v in (r, g, b))
    b = io.BytesIO()
    im.save(b, "JPEG", quality=85)
    return b.getvalue()


def y4m():
    W, H, N, CUT = 48, 32, 14, 9
    out = bytearray(b"YUV4MPEG2 W48 H32 F25:1 Ip A1:1 C420jpeg\n")
    for f in range(N):
        im = Image.new("RGB", (W, H), (235, 230, 220))
        d = ImageDraw.Draw(im)
        if f < CUT:
            d.rectangle([0, 23, 47, 31], fill=(90, 140, 70))
            d.ellipse([30, 2, 42, 14], fill=(240, 180, 40))
            d.rectangle([2 + 3 * f, 12, 8 + 3 * f, 18], fill=(180, 30, 40))
        else:
            im.paste((30, 40, 90), [0, 0, W, H])
            d.ellipse([8, 4, 38, 28], fill=(220, 220, 240))
            d.rectangle([14 + 2 * (f - CUT), 12, 22 + 2 * (f - CUT), 20], fill=(200, 60, 30))
        ycc = im.convert("YCbCr")
        yy, cb, cr = ycc.split()
        out += b"FRAME\n" + yy.tobytes()
        out += cb.resize((W // 2, H // 2), Image.BOX).tobytes()
        out += cr.resize((W // 2, H // 2), Image.BOX).tobytes()
    return bytes(out)


def gif():
    frames = []
    for f in range(6):
        im = Image.new("P", (24, 16), 0)
        im.putpalette([250, 250, 240, 40, 40, 40, 220, 60, 40, 60, 120, 220] + [0] * 756)
        d = ImageDraw.Draw(im)
        d.rectangle([0, 12, 23, 15], fill=1)
        d.rectangle([2 + 3 * f, 4, 7 + 3 * f, 9], fill=2)
        d.ellipse([16, 1, 21, 6], fill=3)
        frames.append(im)
    b = io.BytesIO()
    frames[0].save(b, "GIF", save_all=True, append_images=frames[1:], duration=[100, 100, 200, 100, 100, 300],
                   loop=0, optimize=False, disposal=1)
    return b.getvalue()


def c_array(name, data):
    lines = ["static const uint8_t %s[%d] = {" % (name, len(data))]
    for i in range(0, len(data), 24):
        lines.append("    " + ",".join(str(b) for b in data[i:i + 24]) + ",")
    lines.append("};")
    return "\n".join(lines) + "\n"


FILES = {"fx_pixel_png": "pixel.png", "fx_logo_png": "logo.png", "fx_photo_jpg": "photo.jpg",
         "fx_clip_y4m": "clip.y4m", "fx_anim_gif": "anim.gif"}


def main():
    parts = ["/* Generated by tools/vtrace/make_fixtures.py; do not edit. */",
             "/* SPDX-License-Identifier: Apache-2.0 */"]
    corpus = sys.argv[2] if len(sys.argv) > 2 and sys.argv[1] == "--corpus" else None
    if corpus:
        os.makedirs(corpus, exist_ok=True)
    for name, data in (("fx_pixel_png", pixel_art()), ("fx_logo_png", logo()),
                       ("fx_photo_jpg", photo()), ("fx_clip_y4m", y4m()),
                       ("fx_anim_gif", gif())):
        parts.append(c_array(name, data))
        if corpus:
            with open(os.path.join(corpus, FILES[name]), "wb") as f:
                f.write(data)
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as f:
        f.write("\n".join(parts))
    print("wrote", os.path.relpath(OUT))


if __name__ == "__main__":
    main()
