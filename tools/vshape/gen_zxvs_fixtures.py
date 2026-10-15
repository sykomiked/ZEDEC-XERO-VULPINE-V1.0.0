#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""gen_zxvs_fixtures.py - fixtures for kernel/src/vshape/test_zxvs.c.

    python3 tools/vshape/gen_zxvs_fixtures.py [out.inc]

Real files from this repository (a TSV of measurements, the head of
cards/cards.tsv, .zenodo.json, a C header, the head of a TrueType font) and
small made-up ones (a sine CSV with decimals, a ramp of numbers, XML, HTML,
zxcfg, a PNG, a WAV, edge cases), each with what the test expects: kind,
format, CID (CIDv1 raw sha2-256), and for the series fixtures the
normalised samples (2^-24 of the range, exactly as ZXVS.md defines them),
so the test can measure every curve against data it did not compute.
Standard library only.
"""
import hashlib
import math
import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
OUT = os.path.join(ROOT, "kernel", "src", "vshape", "test", "zxvs_fixtures.inc")

K_BYTES, K_SERIES, K_STRUCT, K_MEDIA = 1, 2, 3, 4
F = dict(bin=1, text=2, csv=3, tsv=4, json=5, xml=6, html=7, zxcfg=8, c=9, numbers=10, png=16, wav=24)


def rd(rel, limit=None, lines=None):
    with open(os.path.join(ROOT, rel), "rb") as f:
        b = f.read()
    if lines is not None:
        b = b"".join(b.splitlines(keepends=True)[:lines])
    if limit is not None:
        b = b[:limit]
    return b


def sine_csv():
    rows = ["t,value,noise"]
    s = 12345
    for i in range(2000):
        s = (s * 1103515245 + 12345) & 0x7FFFFFFF
        rows.append("%d,%.4f,%d" % (i, math.sin(i * 2 * math.pi / 500) * 3.5 + 1.25, s % 1000))
    return ("\n".join(rows) + "\n").encode()


def ramp():
    return ("".join("%d\n" % (i * i % 997 if i % 50 == 0 else i * 3) for i in range(600))).encode()


def png():
    w, h = 7, 5
    raw = b"".join(b"\x00" + bytes((x * 30 + y * 7) & 255 for x in range(w * 3)) for y in range(h))

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def wav():
    n = 800
    pcm = b"".join(struct.pack("<h", int(8000 * math.sin(i * 0.05))) for i in range(n))
    fmt = struct.pack("<HHIIHH", 1, 1, 16000, 32000, 2, 16)
    return (b"RIFF" + struct.pack("<I", 4 + 8 + 16 + 8 + len(pcm)) + b"WAVE" + b"fmt " + struct.pack("<I", 16) +
            fmt + b"data" + struct.pack("<I", len(pcm)) + pcm)


XML = b"""<?xml version="1.0"?>
<!-- a catalogue -->
<catalog region="eu">
  <book id="b1"><title>Signals</title><price cur="EUR">12.50</price></book>
  <book id="b2"><title>Shapes</title><price cur="EUR">9.99</price><![CDATA[raw <text>]]></book>
  <empty/>
</catalog>
"""
HTML = b"""<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>Shape</title>
<style>p { color: red; }</style></head>
<body><h1 class="t">Vector shape</h1><p>It scales cleanly.<br>It adds no detail.</p>
<ul><li>one</li><li>two</li><li>three</li></ul><img src="a.png" alt="a"></body></html>
"""
ZXCFG = b"""zxcfg 1
# layer system, scope global
display.scale = 2
display.theme.accent = blue
display.theme.dark = true
lock audio.volume = 40
audio.output = "speakers"
input.keyboard.layout = "us"
"""


def deep_json(d):
    return ("[" * d + "1" + "]" * d).encode()


def norm(vals):
    lo, hi = min(vals), max(vals)
    r = hi - lo
    return [((v - lo) * (1 << 24) + r // 2) // r if r else 0 for v in vals]


def column(text, col, sep, header=True, dec=0):
    out = []
    lines = [l for l in text.decode().splitlines() if l and not l.startswith("#")]
    for l in lines[1 if header else 0:]:
        c = l.split(sep)[col].strip()
        if c == "":
            continue
        # decimal to an integer at `dec` decimals (the C rule: round half away)
        neg = c.startswith("-")
        c = c.lstrip("+-")
        whole, _, frac = c.partition(".")
        digits = whole + frac
        scale = len(frac)
        v = int(digits) if digits else 0
        if scale <= dec:
            v *= 10 ** (dec - scale)
        else:
            q, r = divmod(v, 10 ** (scale - dec))
            v = q + (1 if 2 * r >= 10 ** (scale - dec) else 0)
        out.append(-v if neg else v)
    return out


def carr(name, ctype, vals, per=16):
    s = ["static const %s %s[%d] = {" % (ctype, name, max(1, len(vals)))]
    for i in range(0, len(vals), per):
        s.append("    " + ",".join(str(v) for v in vals[i:i + per]) + ",")
    if not vals:
        s.append("    0,")
    s.append("};")
    return "\n".join(s)


def cstr(name, b):
    out = ["static const uint8_t %s[%d] = {" % (name, max(1, len(b)))]
    for i in range(0, len(b), 24):
        out.append("    " + ",".join(str(x) for x in b[i:i + 24]) + ",")
    if not b:
        out.append("    0,")
    out.append("};")
    return "\n".join(out)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else OUT
    energy = rd("bench/results/2026-10-11-russell-gating/logs/energy_balance_float64.tsv")
    cards = rd("cards/cards.tsv", lines=400)
    sine = sine_csv()
    fx = [
        # name, bytes, kind, fmt
        ("energy_tsv", energy, K_SERIES, F["tsv"]),
        ("cards_head_tsv", cards, K_SERIES, F["tsv"]),
        ("sine_csv", sine, K_SERIES, F["csv"]),
        ("ramp_numbers", ramp(), K_SERIES, F["numbers"]),
        ("zenodo_json", rd(".zenodo.json"), K_STRUCT, F["json"]),
        ("deep_json", deep_json(100), K_STRUCT, F["json"]),
        ("bad_json", b'{"a": [1, 2,, 3]}\n', K_BYTES, F["text"]),
        ("stft_math_c", rd("kernel/src/vaudio/zxva_stft_math.h"), K_STRUCT, F["c"]),
        ("catalog_xml", XML, K_STRUCT, F["xml"]),
        ("page_html", HTML, K_STRUCT, F["html"]),
        ("settings_zxcfg", ZXCFG, K_STRUCT, F["zxcfg"]),
        ("font_head_bin", rd("assets/fonts/ascw-golden/ASCW-Golden-Armenian.ttf", limit=6000), K_BYTES, F["bin"]),
        ("tiny_png", png(), K_MEDIA, F["png"]),
        ("tiny_wav", wav(), K_MEDIA, F["wav"]),
        ("empty", b"", K_BYTES, F["bin"]),
        ("one_byte", b"x", K_BYTES, F["text"]),
    ]
    body = ["/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */",
            "/* SPDX-License-Identifier: Apache-2.0 */",
            "/* Generated by tools/vshape/gen_zxvs_fixtures.py; do not edit. */"]
    rows = []
    for name, b, kind, fmt in fx:
        body.append(cstr("fx_" + name, b))
        cid = bytes([1, 0x55, 0x12, 0x20]) + hashlib.sha256(b).digest()
        body.append(cstr("fx_cid_" + name, cid))
        rows.append('    {"%s", fx_%s, %d, %d, %d, fx_cid_%s},' % (name, name, len(b), kind, fmt, name))
    # independent normalised samples for three series
    series = [("energy_tsv", 1, "\t", True, 4, energy), ("sine_csv", 1, ",", True, 4, sine),
              ("sine_csv_noise", 2, ",", True, 0, sine)]
    srows = []
    for name, col, sep, hdr, dec, src in series:
        vals = column(src, col, sep, hdr, dec)
        body.append(carr("sn_" + name, "int32_t", norm(vals)))
        fxname = name.replace("_noise", "")
        srows.append('    {"%s", "%s", %d, sn_%s, %d},' % (name, fxname, col, name, len(vals)))
    body.append("typedef struct {\n    const char *name;\n    const uint8_t *d;\n    uint32_t n, kind, fmt;\n"
                "    const uint8_t *cid;\n} zfx_t;")
    body.append("static const zfx_t ZFX[] = {\n" + "\n".join(rows) + "\n};")
    body.append("typedef struct {\n    const char *name, *fixture;\n    uint32_t column;\n    const int32_t *yn;\n"
                "    uint32_t n;\n} zsn_t;")
    body.append("static const zsn_t ZSN[] = {\n" + "\n".join(srows) + "\n};")
    open(out, "w").write("\n".join(body) + "\n")
    print("wrote", out, os.path.getsize(out), "bytes")


if __name__ == "__main__":
    main()
