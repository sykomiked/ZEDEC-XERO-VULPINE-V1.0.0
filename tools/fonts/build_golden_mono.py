#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Build ASCW Golden Mono, the fixed-width member of the ASCW Golden family.

    python3 tools/fonts/build_golden_mono.py NotoSansMono[wdth,wght].ttf OUT.ttf \
        NotoSansSymbols2-Regular.ttf NotoSansSymbols[wght].ttf

The sources are the OFL fonts in google/fonts (ofl/notosansmono,
ofl/notosanssymbols2, ofl/notosanssymbols). Noto Sans Mono is fixed at
weight 400 and width 100; the key symbols it lacks (command, option, shift,
return, delete, control, escape, caps lock, forward delete) are taken from
the symbol fonts, scaled to fit its advance width and centred. The face is
renamed into the family (the OFL's Reserved Font Name clause) and given the
family's golden-section metrics. The result stays under the OFL.
"""
import sys
from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.pens.transformPen import TransformPen
from fontTools.pens.boundsPen import BoundsPen

src, out, syms = sys.argv[1], sys.argv[2], sys.argv[3:]
f = instantiateVariableFont(TTFont(src), {"wght": 400, "wdth": 100})
upm = f["head"].unitsPerEm
cmap = f.getBestCmap()
adv = f["hmtx"][cmap[ord("a")]][0]
srcs = []
for fn in syms:
    t = TTFont(fn)
    if "fvar" in t:
        t = instantiateVariableFont(t, {"wght": 400})
    assert t["head"].unitsPerEm == upm
    srcs.append((t.getGlyphSet(), t.getBestCmap()))
order = list(f.getGlyphOrder())
added = []
for cp in [0x2318, 0x2325, 0x21E7, 0x23CE, 0x232B, 0x2303, 0x238B, 0x21E5, 0x21EA, 0x2326, 0x21DE, 0x21DF]:
    if cp in cmap:
        continue
    found = [(gs, c) for gs, c in srcs if cp in c]
    if not found:
        continue
    sgs, scmap = found[0]
    g = sgs[scmap[cp]]
    bp = BoundsPen(sgs)
    g.draw(bp)
    xmin, ymin, xmax, ymax = bp.bounds
    w = xmax - xmin
    k = min(1.0, (adv * 0.9) / w if w else 1.0, (upm * 0.7) / (ymax - ymin) if ymax > ymin else 1.0)
    dx = (adv - w * k) / 2 - xmin * k
    pen = TTGlyphPen(None)
    g.draw(TransformPen(pen, (k, 0, 0, k, dx, 0)))
    name = "uni%04X" % cp
    f["glyf"][name] = pen.glyph()
    order.append(name)
    f["hmtx"][name] = (adv, int(round(xmin * k + dx)))
    for t in f["cmap"].tables:
        if t.isUnicode():
            t.cmap[cp] = name
    added.append(hex(cp))
f.setGlyphOrder(order)
f["glyf"].glyphOrder = order
if "post" in f and f["post"].formatType == 2:
    f["post"].extraNames = []
    f["post"].mapping = {}
# drop tables that index glyphs we did not extend consistently
for t in ("hdmx", "LTSH", "VDMX"):
    if t in f:
        del f[t]
# names
fam = "ASCW Golden Mono"
n = f["name"]
for rec in list(n.names):
    if rec.nameID in (1, 2, 3, 4, 6, 16, 17, 21, 22, 25):
        n.removeNames(nameID=rec.nameID)
n.setName(fam, 1, 3, 1, 0x409)
n.setName("Regular", 2, 3, 1, 0x409)
n.setName("ASCW;ASCWGoldenMono;2026; derived from Noto Sans Mono", 3, 3, 1, 0x409)
n.setName(fam, 4, 3, 1, 0x409)
n.setName("ASCWGoldenMono-Regular", 6, 3, 1, 0x409)
# golden-section metrics
asc, desc = round(upm * 0.618), round(upm * 0.382)
f["hhea"].ascent, f["hhea"].descent, f["hhea"].lineGap = asc, -desc, 0
o = f["OS/2"]
o.sTypoAscender, o.sTypoDescender, o.sTypoLineGap = asc, -desc, 0
o.usWinAscent, o.usWinDescent = round(upm * 0.679), round(upm * 0.420)
f["maxp"].numGlyphs = len(order)
f.recalcTimestamp = False  # reproducible: keep the source's dates
f.save(out)
g = TTFont(out)
print("added", added, "glyphs", len(g.getGlyphOrder()), "cmap", len(g.getBestCmap()), "adv", adv)
