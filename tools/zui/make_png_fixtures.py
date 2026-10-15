# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
# Writes kernel/src/zui/zui_png_fixture.inc: small PNG files covering every format zui_image reads
# (each colour type and bit depth, every filter, stored / fixed / dynamic zlib, IDAT split across
# chunks, tRNS, an interlaced file it must refuse), each with the pixels zui_image_load must give:
# cropped to the drawn part, premultiplied ARGB.   python3 tools/zui/make_png_fixtures.py
import os, struct, zlib

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '../../kernel/src/zui/zui_png_fixture.inc')
GROUND = (0x00, 0x29, 0x11)


def chunk(t, b): return struct.pack('>I', len(b)) + t + b + struct.pack('>I', zlib.crc32(t + b) & 0xffffffff)


def pack(vals, depth):
    if depth == 8: return bytes(vals)
    if depth == 16: return b''.join(struct.pack('>H', v) for v in vals)
    out, acc, n = bytearray(), 0, 0
    for v in vals:
        acc = (acc << depth) | v; n += depth
        if n == 8: out.append(acc); acc = n = 0
    if n: out.append(acc << (8 - n))
    return bytes(out)


def paeth(a, b, c):
    p = a + b - c; pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    return a if pa <= pb and pa <= pc else b if pb <= pc else c


def filt(rows, bpp):
    """each row with filter (row index mod 5), as the encoder may choose any"""
    out, prev = bytearray(), bytes(len(rows[0]))
    for y, r in enumerate(rows):
        f = y % 5; o = bytearray([f])
        for i in range(len(r)):
            a = r[i - bpp] if i >= bpp else 0; b = prev[i]; c = prev[i - bpp] if i >= bpp else 0
            pred = (0, a, b, (a + b) // 2, paeth(a, b, c))[f]
            o.append((r[i] - pred) & 255)
        out += o; prev = r
    return bytes(out)


def png(w, h, depth, ctype, rows, bpp, mode, plte=None, trns=None, split=1, interlace=0):
    raw = filt(rows, bpp)
    if mode == 'stored': z = zlib.compress(raw, 0)
    elif mode == 'fixed':
        co = zlib.compressobj(9, zlib.DEFLATED, 15, 9, zlib.Z_FIXED); z = co.compress(raw) + co.flush()
    else: z = zlib.compress(raw, 9)
    b = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, depth, ctype, 0, 0, interlace))
    if plte: b += chunk(b'PLTE', bytes(sum(plte, ())))
    if trns is not None: b += chunk(b'tRNS', trns)
    b += chunk(b'tEXt', b'Comment\x00skipped')
    step = (len(z) + split - 1) // split
    for i in range(0, len(z), step): b += chunk(b'IDAT', z[i:i + step])
    return b + chunk(b'IEND', b'')


def premul(px):
    r, g, b, a = px
    return (a << 24) | (r * a // 255) << 16 | (g * a // 255) << 8 | (b * a // 255)


def crop(pix, w, h):
    xs = [x for y in range(h) for x in range(w) if pix[y][x][3]]
    ys = [y for y in range(h) for x in range(w) if pix[y][x][3]]
    x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
    return x1 - x0 + 1, y1 - y0 + 1, [premul(pix[y][x]) for y in range(y0, y1 + 1) for x in range(x0, x1 + 1)]


def keyed(r, g, b):
    d = abs(r - GROUND[0]) + abs(g - GROUND[1]) + abs(b - GROUND[2])
    return (r, g, b, min(255, max(0, (d - 30) * 3)))


cases = []

# 1. RGBA 8: a clear border, a gradient with soft alpha; dynamic Huffman, split over three IDATs
w, h = 13, 7
pix = [[(0, 0, 0, 0) if x in (0, 12) or y == 0 else ((x * 19) & 255, (y * 37) & 255, (x * y * 7) & 255, (40 + x * 15 + y * 3) & 255)
        for x in range(w)] for y in range(h)]
rows = [bytes(sum(pix[y], ())) for y in range(h)]
cases.append(('rgba8', png(w, h, 8, 6, rows, 4, 'dynamic', split=3), 0, *crop(pix, w, h)))

# 2. RGB 8 drawn on the ZXV green (an app icon), keyed out; fixed Huffman
w, h = 16, 12
def icon(x, y):
    if 3 <= x <= 12 and 2 <= y <= 9: return (0xF2, 0xC2, 0x30) if (x + y) % 3 else (0xA0, 0x80, 0x20)
    if x == 2 or y == 1: return (0x10, 0x40, 0x20)   # a soft edge, near the ground
    return GROUND
pix_rgb = [[icon(x, y) for x in range(w)] for y in range(h)]
rows = [bytes(sum(pix_rgb[y], ())) for y in range(h)]
pix = [[keyed(*p) for p in row] for row in pix_rgb]
cases.append(('rgb8_keyed', png(w, h, 8, 2, rows, 3, 'fixed'), 1, *crop(pix, w, h)))

# 3. palette 4-bit with tRNS; stored (no compression)
w, h = 9, 5
pal = [(0, 0, 0), (242, 194, 48), (10, 200, 90), (255, 255, 255)] + [(i, i, i) for i in range(12)]
alpha = bytes([0, 255, 128, 64])
idx = [[(x + 2 * y) % 4 for x in range(w)] for y in range(h)]
rows = [pack(idx[y], 4) for y in range(h)]
pix = [[pal[i] + ((alpha[i] if i < 4 else 255),) for i in r] for r in idx]
cases.append(('pal4_trns', png(w, h, 4, 3, rows, 1, 'stored', plte=pal, trns=alpha), 0, *crop(pix, w, h)))

# 4. grey 16-bit (high byte kept); dynamic
w, h = 5, 4
g16 = [[(x * 13000 + y * 1111) & 0xFFFF for x in range(w)] for y in range(h)]
rows = [pack(g16[y], 16) for y in range(h)]
pix = [[(v >> 8, v >> 8, v >> 8, 255) for v in r] for r in g16]
cases.append(('grey16', png(w, h, 16, 0, rows, 2, 'dynamic'), 0, *crop(pix, w, h)))

# 5. grey + alpha 8; fixed
w, h = 4, 4
ga = [[(x * 60, 0 if (x, y) == (0, 0) else 50 + y * 60) for x in range(w)] for y in range(h)]
rows = [bytes(sum(ga[y], ())) for y in range(h)]
pix = [[(v, v, v, a) for v, a in r] for r in ga]
cases.append(('grey_alpha8', png(w, h, 8, 4, rows, 2, 'fixed'), 0, *crop(pix, w, h)))

# 6. grey 1-bit with a tRNS key on 0: only the set bits are drawn; dynamic
w, h = 10, 3
bits = [[1 if (x * 3 + y) % 4 == 1 else 0 for x in range(w)] for y in range(h)]
rows = [pack(bits[y], 1) for y in range(h)]
pix = [[(255, 255, 255, 255) if v else (0, 0, 0, 0) for v in r] for r in bits]
cases.append(('grey1_trns', png(w, h, 1, 0, rows, 1, 'dynamic', trns=struct.pack('>H', 0)), 0, *crop(pix, w, h)))

# 7. RGB 16-bit; stored
w, h = 3, 2
c16 = [[(x * 20000, y * 30000, 65535 - x * 9000) for x in range(w)] for y in range(h)]
rows = [pack(sum(c16[y], ()), 16) for y in range(h)]
pix = [[(r >> 8, g >> 8, b >> 8, 255) for r, g, b in row] for row in c16]
cases.append(('rgb16', png(w, h, 16, 2, rows, 6, 'stored'), 0, *crop(pix, w, h)))

L = ['/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */',
     '/* SPDX-License-Identifier: Apache-2.0 */',
     '/* zui_png_fixture.inc: PNG files for test_zui_image, one per format, and the',
     ' * premultiplied ARGB pixels zui_image_load must give for each (cropped to the',
     ' * drawn part). Generated by tools/zui/make_png_fixtures.py; do not edit. */',
     '/* clang-format off */']
for name, data, key, cw, ch, want in cases:
    L.append('static const uint8_t PNG_%s[%d] = {' % (name.upper(), len(data)))
    for i in range(0, len(data), 16): L.append('    ' + ','.join('0x%02x' % v for v in data[i:i + 16]) + ',')
    L.append('};')
    L.append('static const uint32_t WANT_%s[%d] = {' % (name.upper(), len(want)))
    for i in range(0, len(want), 8): L.append('    ' + ','.join('0x%08xu' % v for v in want[i:i + 8]) + ',')
    L.append('};')
L.append('typedef struct { const char *name; const uint8_t *png; uint32_t n; int keyed; int32_t w, h; const uint32_t *want; } png_case_t;')
L.append('static const png_case_t PNG_CASES[] = {')
for name, data, key, cw, ch, want in cases:
    N = name.upper()
    L.append('    {"%s", PNG_%s, sizeof PNG_%s, %d, %d, %d, WANT_%s},' % (name, N, N, key, cw, ch, N))
L.append('};')
# an interlaced file: refused
inter = png(4, 4, 8, 6, [bytes(16)] * 4, 4, 'stored', interlace=1)
L.append('static const uint8_t PNG_INTERLACED[%d] = {' % len(inter))
for i in range(0, len(inter), 16): L.append('    ' + ','.join('0x%02x' % v for v in inter[i:i + 16]) + ',')
L.append('};')
L.append('/* clang-format on */')
open(OUT, 'w').write('\n'.join(L) + '\n'); print('wrote', os.path.relpath(OUT), len(cases), 'cases')
