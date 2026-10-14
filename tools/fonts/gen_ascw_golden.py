#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""gen_ascw_golden.py — the ASCW Golden font map, from the fonts themselves.

Reads every face in assets/fonts/ascw-golden/ (their cmaps, with fontTools)
and every i18n locale (tools/fonts/dump_locales.c, built against
kernel/src/i18n), then writes one decision in five forms:

  assets/fonts/ascw-golden/fontmap.json        the map, for any host
  kernel/src/font/ascw_golden_tables.c         the kernel's copy (font/ascw_golden.h)
  kernel/arch/hosted/zxv_ui.html               @font-face block between markers
  macos/studio/Sources/ZXVStudio/ASCWGoldenMap.swift
  mobile/ios/Sources/ZXVKit/ASCWGoldenMap.swift
  mobile/android/app/src/main/java/org/zedec/zxv/ui/AscwGoldenMap.kt
  docs/FONTS_COVERAGE.md                       per-script and per-locale coverage

The decision:
  * ASCW Golden Latin II is the primary face: English and every Latin locale,
    and the face for digits, punctuation and symbols it has.
  * Each other script goes to the Golden member made for it (Greek, Cyrillic,
    Arabic, Devanagari, ...). Han is split the CLDR way: zh-Hans to Chinese
    Simplified, zh-Hant and yue to Chinese Traditional, ja to Japanese, ko to
    Korean. Where two members serve one script (Arabic and Persian), the one
    covering more of the locale's CLDR exemplar characters wins.
  * Where the Golden set has no member, the ASCW sibling faces fill in
    (Adlam, N'Ko, Vai, Tifinagh, Osmanya). Anything still missing falls to the
    platform's own fonts, and the coverage report lists it.

  python3 tools/fonts/gen_ascw_golden.py          (needs fontTools and gcc)
"""
import json
import os
import re
import subprocess
import sys
import tempfile

from fontTools import unicodedata as ucd
from fontTools.ttLib import TTFont

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
FONTS = os.path.join(ROOT, "assets", "fonts", "ascw-golden")

# file, scripts it is THE face for (ISO 15924), role
FACES = [
    ("ASCW-Golden-Latin2.ttf", ["Latn"], "primary"),
    ("ASCW-Golden-Latin.ttf", [], "alternate"),
    ("ASCW-Golden-Greek.ttf", ["Grek"], "script"),
    ("ASCW-Golden-Cyrillic.ttf", ["Cyrl"], "script"),
    ("ASCW-Golden-Armenian.ttf", ["Armn"], "script"),
    ("ASCW-Golden-Georgian.ttf", ["Geor"], "script"),
    ("ASCW-Golden-Hebrew.ttf", ["Hebr"], "script"),
    ("ASCW-Golden-Arabic.ttf", ["Arab"], "script"),
    ("ASCW-Golden-Persian.ttf", [], "alternate"),
    ("ASCW-Golden-Syriac.ttf", ["Syrc"], "script"),
    ("ASCW-Golden-Aramaic.ttf", ["Armi"], "script"),
    ("ASCW-Golden-Mandaic.ttf", ["Mand"], "script"),
    ("ASCW-Golden-Avestan.ttf", ["Avst"], "script"),
    ("ASCW-Golden-Devanagari.ttf", ["Deva"], "script"),
    ("ASCW-Golden-Bengali.ttf", ["Beng"], "script"),
    ("ASCW-Golden-Gurmukhi.ttf", ["Guru"], "script"),
    ("ASCW-Golden-Gujarati.ttf", ["Gujr"], "script"),
    ("ASCW-Golden-Oriya.ttf", ["Orya"], "script"),
    ("ASCW-Golden-Tamil.ttf", ["Taml"], "script"),
    ("ASCW-Golden-Telugu.ttf", ["Telu"], "script"),
    ("ASCW-Golden-Kannada.ttf", ["Knda"], "script"),
    ("ASCW-Golden-Malayalam.ttf", ["Mlym"], "script"),
    ("ASCW-Golden-Sinhala.ttf", ["Sinh"], "script"),
    ("ASCW-Golden-Thai.ttf", ["Thai"], "script"),
    ("ASCW-Golden-Lao.ttf", ["Laoo"], "script"),
    ("ASCW-Golden-Khmer.ttf", ["Khmr"], "script"),
    ("ASCW-Golden-Myanmar.ttf", ["Mymr"], "script"),
    ("ASCW-Golden-Ethiopic.ttf", ["Ethi"], "script"),
    ("ASCW-Golden-Cherokee.ttf", ["Cher"], "script"),
    ("ASCW-Golden-CanadianAboriginal.otf", ["Cans"], "script"),
    ("ASCW-Golden-ChineseSimplified.ttf", ["Hans", "Hani", "Bopo"], "script"),
    ("ASCW-Golden-ChineseTraditional.ttf", ["Hant"], "script"),
    ("ASCW-Golden-Japanese.ttf", ["Jpan", "Hira", "Kana"], "script"),
    ("ASCW-Golden-Korean.ttf", ["Kore", "Hang"], "script"),
    ("ASCW-Golden-Yi.ttf", ["Yiii"], "script"),
    ("ASCW-Golden-Nushu.ttf", ["Nshu"], "script"),
    ("ASCW-Golden-Coptic.ttf", ["Copt"], "script"),
    ("ASCW-Golden-Glagolitic.ttf", ["Glag"], "script"),
    ("ASCW-Golden-Gothic.ttf", ["Goth"], "script"),
    ("ASCW-Golden-Runic.ttf", ["Runr"], "script"),
    ("ASCW-Golden-OldItalic.ttf", ["Ital"], "script"),
    ("ASCW-Golden-OldTurkic.ttf", ["Orkh"], "script"),
    ("ASCW-Golden-OldHungarian.ttf", ["Hung"], "script"),
    ("ASCW-Golden-Carian.ttf", ["Cari"], "script"),
    ("ASCW-Golden-Meroitic.ttf", ["Mero", "Merc"], "script"),
    ("ASCW-Golden-EgyptianHieroglyphs.ttf", ["Egyp"], "script"),
    ("ASCW-Golden-AnatolianHieroglyphs.ttf", ["Hluw"], "script"),
    ("ASCW-Golden-Cuneiform.ttf", ["Xsux"], "script"),
    ("ASCW-Golden-Bamum.ttf", ["Bamu"], "script"),
    ("ASCW-Golden-BassaVah.ttf", ["Bass"], "script"),
    ("ASCW-Golden-MendeKikakui.ttf", ["Mend"], "script"),
    ("ASCW-Golden-Osage.ttf", ["Osge"], "script"),
    ("ASCW-Golden-OlChiki.ttf", ["Olck"], "script"),
    ("ASCW-Golden-MeeteiMayek.ttf", ["Mtei"], "script"),
    ("ASCW-Golden-Lepcha.ttf", ["Lepc"], "script"),
    ("ASCW-Golden-Limbu.ttf", ["Limb"], "script"),
    ("ASCW-Golden-Lisu.ttf", ["Lisu"], "script"),
    ("ASCW-Golden-Miao.ttf", ["Plrd"], "script"),
    ("ASCW-Golden-PahawhHmong.ttf", ["Hmng"], "script"),
    ("ASCW-Golden-SoraSompeng.ttf", ["Sora"], "script"),
    ("ASCW-Golden-WarangCiti.ttf", ["Wara"], "script"),
    ("ASCW-Golden-Wancho.ttf", ["Wcho"], "script"),
    ("ASCW-Golden-GunjalaGondi.otf", ["Gong"], "script"),
    ("ASCW-Golden-MasaramGondi.otf", ["Gonm"], "script"),
    ("ASCW-Golden-TaiLe.ttf", ["Tale"], "script"),
    ("ASCW-Golden-NewTaiLue.ttf", ["Talu"], "script"),
    ("ASCW-Golden-TaiTham.ttf", ["Lana"], "script"),
    ("ASCW-Golden-TaiViet.ttf", ["Tavt"], "script"),
    ("ASCW-Golden-Cham.ttf", ["Cham"], "script"),
    ("ASCW-Golden-Balinese.ttf", ["Bali"], "script"),
    ("ASCW-Golden-Javanese.otf", ["Java"], "script"),
    ("ASCW-Golden-Sundanese.ttf", ["Sund"], "script"),
    ("ASCW-Golden-Batak.ttf", ["Batk"], "script"),
    ("ASCW-Golden-Buginese.ttf", ["Bugi"], "script"),
    ("ASCW-Golden-Tagalog.ttf", ["Tglg"], "script"),
    ("ASCW-Golden-Hanunoo.ttf", ["Hano"], "script"),
    ("ASCW-Golden-Buhid.ttf", ["Buhd"], "script"),
    ("ASCW-Golden-Tagbanwa.ttf", ["Tagb"], "script"),
    # ASCW siblings for scripts the Golden set does not have
    ("ASCW-Adlam-Regular.ttf", ["Adlm"], "fallback"),
    ("ASCW-NKo-Regular.ttf", ["Nkoo"], "fallback"),
    ("ASCW-Vai-Regular.ttf", ["Vaii"], "fallback"),
    ("ASCW-Tifinagh-Regular.otf", ["Tfng"], "fallback"),
    ("ASCW-Osmanya-Regular.ttf", ["Osma"], "fallback"),
]
PRIMARY = 0
# the members a locale of one script may use instead of the script's own face
ALTERNATES = {"Arab": ["ASCW-Golden-Arabic.ttf", "ASCW-Golden-Persian.ttf"]}
# languages whose own Golden member wins a tie on coverage
LANG_PREFER = {"fa": "ASCW-Golden-Persian.ttf"}
# CLDR locale scripts that are aliases of another face's script
SCRIPT_ALIAS = {"Hani": "Hans"}

SCRIPT_NAMES = {"Hans": "Han (Simplified)", "Hant": "Han (Traditional)", "Jpan": "Japanese",
                "Kore": "Korean"}

ROLE_CODE = {"primary": 0, "script": 1, "alternate": 2, "fallback": 3}


def load_faces():
    faces = []
    for i, (fn, scripts, role) in enumerate(FACES):
        f = TTFont(os.path.join(FONTS, fn), lazy=True)
        fam = f["name"].getDebugName(1)
        cps = set(f.getBestCmap() or {})
        faces.append({"i": i, "file": fn, "family": fam, "scripts": scripts, "role": role,
                      "cps": cps, "copyright": f["name"].getDebugName(0) or "",
                      "unique": f["name"].getDebugName(3) or "",
                      "version": f["name"].getDebugName(5) or ""})
    on_disk = sorted(x for x in os.listdir(FONTS) if x.endswith((".ttf", ".otf")))
    listed = sorted(f["file"] for f in faces)
    if on_disk != listed:
        sys.exit("faces on disk and FACES differ: %s" % sorted(set(on_disk) ^ set(listed)))
    return faces


def assign(faces):
    """Every codepoint any face has -> the one face that draws it by default."""
    owner = {}
    for f in faces:
        for s in f["scripts"]:
            owner.setdefault(s, f["i"])
    for alias, s in SCRIPT_ALIAS.items():
        owner.setdefault(alias, owner[s])
    allcp = set()
    for f in faces:
        allcp |= f["cps"]
    table = {}
    for cp in sorted(allcp):
        s = ucd.script(cp)
        o = owner.get(s)
        if o is not None and cp in faces[o]["cps"]:
            table[cp] = o
            continue
        # common, inherited, or the owner lacks it: Latin II, then the owner
        # of any script that uses the character, then the faces in order
        cands = [PRIMARY]
        for x in sorted(ucd.script_extension(cp)):
            if x in owner:
                cands.append(owner[x])
        cands += [f["i"] for f in faces if f["role"] != "alternate"]
        cands += [f["i"] for f in faces]
        table[cp] = next(c for c in cands if cp in faces[c]["cps"])
    return owner, table


def ranges_of(table, bridge):
    """Coalesce cp->face into [lo, hi, face]. With bridge, also join two runs
    of one face across a gap no face covers: fewer ranges, but the range then
    claims code points the face lacks, so only the CSS (where the browser
    falls through on a missing glyph) uses it."""
    out = []
    for cp in sorted(table):
        f = table[cp]
        if out and out[-1][2] == f and out[-1][1] + 1 == cp:
            out[-1][1] = cp
        else:
            out.append([cp, cp, f])
    if not bridge:
        return out
    merged = []
    for r in out:
        if merged and merged[-1][2] == r[2]:
            gap = range(merged[-1][1] + 1, r[0])
            if all(c not in table for c in gap):
                merged[-1][1] = r[1]
                continue
        merged.append(list(r))
    return merged


def parse_ranges(s):
    cps = set()
    for part in s.split(","):
        if part:
            lo, hi = part.split("-")
            cps.update(range(int(lo, 16), int(hi, 16) + 1))
    return cps


def load_locales():
    tmp = tempfile.mkdtemp()
    exe = os.path.join(tmp, "dump_locales")
    src = os.path.join(ROOT, "kernel", "src", "i18n")
    subprocess.check_call(["gcc", "-std=c11", "-O1", "-I" + src,
                           os.path.join(ROOT, "tools", "fonts", "dump_locales.c")] +
                          [os.path.join(src, x) for x in ("i18n_text.c", "i18n_locale.c",
                           "i18n_format.c", "i18n_tables.c", "i18n_catalog.c")] + ["-o", exe])
    rows = []
    for line in subprocess.check_output([exe]).decode().splitlines():
        tag, script, rtl, eng, rng = (line.split("\t") + [""])[:5]
        rows.append({"tag": tag, "script": script, "rtl": rtl == "1", "name": eng,
                     "needs": parse_ranges(rng)})
    return rows


def pick_locale_face(loc, faces, owner, by_file):
    s = SCRIPT_ALIAS.get(loc["script"], loc["script"])
    cands = [by_file[x] for x in ALTERNATES.get(s, [])] or ([owner[s]] if s in owner else [])
    if not cands:
        return None
    pref = LANG_PREFER.get(loc["tag"].split("-")[0])
    if pref:
        cands = [by_file[pref]] + [c for c in cands if c != by_file[pref]]
    own = {c for c in loc["needs"] if ucd.script(c) not in ("Zyyy", "Zinh")} or loc["needs"]
    best = max(cands, key=lambda c: (len(own & faces[c]["cps"]), -cands.index(c)))
    return best


def main():
    faces = load_faces()
    by_file = {f["file"]: f["i"] for f in faces}
    owner, table = assign(faces)
    rngs = ranges_of(table, False)
    css_rngs = ranges_of(table, True)
    locales = load_locales()

    loc_rows = []
    for loc in locales:
        face = pick_locale_face(loc, faces, owner, by_file)
        chain = [face] if face is not None else []
        if PRIMARY not in chain:
            chain.append(PRIMARY)
        in_chain = set().union(*(faces[c]["cps"] for c in chain))
        missing_chain = sorted(loc["needs"] - in_chain)
        missing_all = sorted(c for c in loc["needs"] if c not in table)
        loc_rows.append({"tag": loc["tag"], "name": loc["name"], "script": loc["script"],
                         "rtl": loc["rtl"], "face": face, "chain": chain,
                         "missing_chain": missing_chain, "missing": missing_all})

    # overrides: locales whose face is not simply the face of their script
    overrides = []
    for r in loc_rows:
        s = SCRIPT_ALIAS.get(r["script"], r["script"])
        default = owner.get(s)
        if r["face"] is not None and r["face"] != default:
            overrides.append((r["tag"], r["face"]))

    write_json(faces, owner, rngs, loc_rows, overrides)
    write_c(faces, owner, rngs, overrides)
    write_css(faces, css_rngs)
    write_swift(faces, owner, overrides)
    write_kotlin(faces, owner, overrides)
    write_report(faces, owner, loc_rows, table)
    write_fontlog(faces)
    print("faces %d, ranges %d (css %d), locales %d, overrides %d" %
          (len(faces), len(rngs), len(css_rngs), len(loc_rows), len(overrides)))


def write_json(faces, owner, rngs, loc_rows, overrides):
    doc = {
        "_comment": "Generated by tools/fonts/gen_ascw_golden.py. Do not edit.",
        "primary": faces[PRIMARY]["family"],
        "faces": [{"file": f["file"], "family": f["family"], "role": f["role"],
                   "scripts": f["scripts"], "codepoints": len(f["cps"])} for f in faces],
        "scripts": {s: faces[i]["file"] for s, i in sorted(owner.items())},
        "locale_overrides": {t: faces[i]["file"] for t, i in overrides},
        "ranges": [[lo, hi, faces[f]["file"]] for lo, hi, f in rngs],
        "locales": {r["tag"]: {"script": r["script"],
                               "face": faces[r["face"]]["file"] if r["face"] is not None else None,
                               "missing": len(r["missing"])} for r in loc_rows},
    }
    with open(os.path.join(FONTS, "fontmap.json"), "w") as fp:
        json.dump(doc, fp, ensure_ascii=False, separators=(",", ":"))
        fp.write("\n")


def write_c(faces, owner, rngs, overrides):
    L = ["/* ascw_golden_tables.c — GENERATED by tools/fonts/gen_ascw_golden.py.",
         " * Do not edit. See ascw_golden.h.",
         " *",
         " * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC",
         " * SPDX-License-Identifier: Apache-2.0",
         " * (The font files themselves are under the SIL Open Font License 1.1:",
         " * assets/fonts/ascw-golden/OFL.txt.)",
         " */",
         '#include "ascw_golden_internal.h"', "",
         "const ascw_face_t ASCW_FACES[] = {"]
    for f in faces:
        L.append('    {"%s", "%s", "%s", %d},' % (f["file"], f["family"], " ".join(f["scripts"]),
                                                   ROLE_CODE[f["role"]]))
    L += ["};", "const uint32_t ASCW_FACE_N = %d;" % len(faces), "",
          "const ascw_script_face_t ASCW_SCRIPTS[] = {"]
    for s, i in sorted(owner.items()):
        L.append('    {"%s", %d},' % (s, i))
    L += ["};", "const uint32_t ASCW_SCRIPT_N = %d;" % len(owner), "",
          "/* exact CLDR tags, sorted */", "const ascw_locale_face_t ASCW_LOCALES[] = {"]
    for t, i in sorted(overrides):
        L.append('    {"%s", %d},' % (t, i))
    L += ["};", "const uint32_t ASCW_LOCALE_N = %d;" % len(overrides), "",
          "/* every codepoint some face has -> its default face; sorted, disjoint */",
          "const ascw_range_t ASCW_RANGES[] = {"]
    for lo, hi, f in rngs:
        L.append("    {0x%04X, 0x%04X, %d}," % (lo, hi, f))
    L += ["};", "const uint32_t ASCW_RANGE_N = %d;" % len(rngs), ""]
    with open(os.path.join(ROOT, "kernel", "src", "font", "ascw_golden_tables.c"), "w") as fp:
        fp.write("\n".join(L))


def css_ranges(rs):
    def one(lo, hi):
        return "U+%X" % lo if lo == hi else "U+%X-%X" % (lo, hi)
    return ",".join(one(lo, hi) for lo, hi in rs)


def write_css(faces, rngs):
    per = {}
    for lo, hi, f in rngs:
        per.setdefault(f, []).append((lo, hi))
    out = ["/* ASCW-GOLDEN:BEGIN generated by tools/fonts/gen_ascw_golden.py; served from /fonts/ */"]
    # one family, each face limited to the codepoints it draws by default, so
    # the browser fetches a face only when the page shows its script
    for f in faces:
        if f["i"] not in per:
            continue
        fmt = "opentype" if f["file"].endswith(".otf") else "truetype"
        out.append("@font-face{font-family:'ZXV UI';src:url('/fonts/%s') format('%s');"
                   "font-display:swap;unicode-range:%s}" % (f["file"], fmt, css_ranges(per[f["i"]])))
    # Han unification and Persian: the language picks the member
    lang = [("ja", "ASCW-Golden-Japanese.ttf"), ("ko", "ASCW-Golden-Korean.ttf"),
            ("zh-Hant", "ASCW-Golden-ChineseTraditional.ttf"),
            ("zh-HK", "ASCW-Golden-ChineseTraditional.ttf"),
            ("zh-TW", "ASCW-Golden-ChineseTraditional.ttf"),
            ("yue", "ASCW-Golden-ChineseTraditional.ttf"),
            ("fa", "ASCW-Golden-Persian.ttf")]
    # each language face is limited to its group's codepoints, so Latin text
    # in a Japanese or Persian paragraph still falls through to Latin II
    groups = [["ASCW-Golden-ChineseSimplified.ttf", "ASCW-Golden-ChineseTraditional.ttf",
               "ASCW-Golden-Japanese.ttf", "ASCW-Golden-Korean.ttf"],
              ["ASCW-Golden-Arabic.ttf", "ASCW-Golden-Persian.ttf"]]
    idx = {f["file"]: f["i"] for f in faces}
    for tag, fn in lang:
        g = next(g for g in groups if fn in g)
        members = {idx[x] for x in g}
        rs = [(lo, hi) for lo, hi, f in rngs if f in members]
        fmt = "opentype" if fn.endswith(".otf") else "truetype"
        out.append("@font-face{font-family:'ZXV UI %s';src:url('/fonts/%s') format('%s');"
                   "font-display:swap;unicode-range:%s}" % (tag, fn, fmt, css_ranges(rs)))
        out.append(":lang(%s){font-family:'ZXV UI %s','ZXV UI',system-ui,sans-serif}" % (tag, tag))
    out.append("/* ASCW-GOLDEN:END */")
    path = os.path.join(ROOT, "kernel", "arch", "hosted", "zxv_ui.html")
    html = open(path, encoding="utf-8").read()
    block = "\n".join(out)
    pat = re.compile(r"/\* ASCW-GOLDEN:BEGIN.*?ASCW-GOLDEN:END \*/", re.S)
    if not pat.search(html):
        sys.exit("zxv_ui.html has no ASCW-GOLDEN markers")
    html = pat.sub(lambda _m: block, html)
    with open(path, "w", encoding="utf-8") as fp:
        fp.write(html)


def cascade(faces):
    """Fallback order after the first face: script faces, alternates, fallbacks."""
    order = [f for f in faces if f["role"] in ("primary", "script")]
    order += [f for f in faces if f["role"] == "fallback"]
    order += [f for f in faces if f["role"] == "alternate"]
    return order


def write_swift(faces, owner, overrides):
    L = ["// GENERATED by tools/fonts/gen_ascw_golden.py. Do not edit.",
         "// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC",
         "// SPDX-License-Identifier: Apache-2.0",
         "// The fonts: SIL Open Font License 1.1 (assets/fonts/ascw-golden/OFL.txt).", "",
         "enum ASCWGoldenMap {",
         "    /// The primary face: English and every Latin-script language.",
         '    static let primary = "%s"' % faces[PRIMARY]["family"],
         "    /// Every face file, as bundled.",
         "    static let files: [String] = ["]
    L += ['        "%s",' % f["file"] for f in faces]
    L += ["    ]",
          "    /// Fallback order behind the chosen face (family names).",
          "    static let cascade: [String] = ["]
    L += ['        "%s",' % f["family"] for f in cascade(faces)]
    L += ["    ]",
          "    /// ISO 15924 script -> family.",
          "    static let scripts: [String: String] = ["]
    L += ['        "%s": "%s",' % (s, faces[i]["family"]) for s, i in sorted(owner.items())]
    L += ["    ]",
          "    /// CLDR locales whose face is not simply their script's face.",
          "    static let locales: [String: String] = ["]
    L += ['        "%s": "%s",' % (t, faces[i]["family"]) for t, i in sorted(overrides)]
    L += ["    ]", "}", ""]
    for p in ("macos/studio/Sources/ZXVStudio/ASCWGoldenMap.swift",
              "mobile/ios/Sources/ZXVKit/ASCWGoldenMap.swift"):
        with open(os.path.join(ROOT, p), "w") as fp:
            fp.write("\n".join(L))


def asset_name(fn):
    return fn


def write_kotlin(faces, owner, overrides):
    L = ["// GENERATED by tools/fonts/gen_ascw_golden.py. Do not edit.",
         "// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC",
         "// SPDX-License-Identifier: Apache-2.0",
         "// The fonts: SIL Open Font License 1.1 (assets/fonts/ascw-golden/OFL.txt).",
         "package org.zedec.zxv.ui", "",
         "internal object AscwGoldenMap {",
         '    const val PRIMARY = "%s"' % faces[PRIMARY]["file"],
         "    /** ISO 15924 script -> face file under assets/fonts/. */",
         "    val scripts: Map<String, String> = mapOf("]
    L += ['        "%s" to "%s",' % (s, faces[i]["file"]) for s, i in sorted(owner.items())]
    L += ["    )",
          "    /** CLDR locales whose face is not simply their script's face. */",
          "    val locales: Map<String, String> = mapOf("]
    L += ['        "%s" to "%s",' % (t, faces[i]["file"]) for t, i in sorted(overrides)]
    L += ["    )", "}", ""]
    with open(os.path.join(ROOT, "mobile/android/app/src/main/java/org/zedec/zxv/ui/AscwGoldenMap.kt"),
              "w") as fp:
        fp.write("\n".join(L))


def fmt_cps(cps, n=6):
    s = " ".join("U+%04X" % c for c in cps[:n])
    return s + (" …" if len(cps) > n else "")


def write_report(faces, owner, loc_rows, table):
    by_script = {}
    for r in loc_rows:
        by_script.setdefault(r["script"], []).append(r)
    L = ["<!--", "GENERATED by tools/fonts/gen_ascw_golden.py. Do not edit.",
         "Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC",
         "SPDX-License-Identifier: Apache-2.0", "-->", "",
         "# ASCW Golden coverage", "",
         "Measured from the fonts' own character maps against the CLDR exemplar",
         "characters, punctuation and digits each locale needs",
         "(`i18n_locale_needs()`). \"Covered\" means every one of those code points",
         "is in the locale's face or in ASCW Golden Latin II behind it.", ""]
    full = sum(1 for r in loc_rows if not r["missing_chain"])
    fam = sum(1 for r in loc_rows if not r["missing"])
    none = sum(1 for r in loc_rows if r["face"] is None)
    L += ["- Locales: **%d**" % len(loc_rows),
          "- Covered by their own face plus Latin II: **%d**" % full,
          "- Covered by the ASCW set as a whole (some characters from another member): **%d**" % fam,
          "- With no ASCW face for their script (platform fonts draw them): **%d**" % none, "",
          "## By script", "",
          "| Script | Locales | Face | Covered | Missing (worst locale) |",
          "|---|---|---|---|---|"]
    for s in sorted(by_script, key=lambda k: (-len(by_script[k]), k)):
        rows = by_script[s]
        facefiles = sorted({faces[r["face"]]["family"] for r in rows if r["face"] is not None})
        face = ", ".join(facefiles) if facefiles else "none: platform font"
        ok = sum(1 for r in rows if not r["missing_chain"])
        worst = max(rows, key=lambda r: len(r["missing_chain"]))
        miss = ("%s: %d (%s)" % (worst["tag"], len(worst["missing_chain"]),
                                  fmt_cps(worst["missing_chain"]))) if worst["missing_chain"] else ""
        L.append("| %s %s | %d | %s | %d | %s |" % (s, SCRIPT_NAMES.get(s) or ucd.script_name(s, default=""), len(rows),
                                                    face, ok, miss))
    L += ["", "## Faces", "", "| File | Family | Role | Scripts | Code points |", "|---|---|---|---|---|"]
    for f in faces:
        L.append("| %s | %s | %s | %s | %d |" % (f["file"], f["family"], f["role"],
                                                " ".join(f["scripts"]) or "-", len(f["cps"])))
    L += ["", "## Locales with gaps", "",
          "| Locale | Script | Face | Missing from face + Latin II | Missing from whole set |",
          "|---|---|---|---|---|"]
    for r in loc_rows:
        if r["missing_chain"]:
            L.append("| %s %s | %s | %s | %d %s | %d |" % (
                r["tag"], r["name"], r["script"],
                faces[r["face"]]["family"] if r["face"] is not None else "none",
                len(r["missing_chain"]), fmt_cps(r["missing_chain"], 4), len(r["missing"])))
    L.append("")
    with open(os.path.join(ROOT, "docs", "FONTS_COVERAGE.md"), "w") as fp:
        fp.write("\n".join(L))


# upstream of the faces whose name table no longer says (Runic lost its
# copyright string in the ASCW build; it is Noto Sans Runic)
UPSTREAM_COPYRIGHT = {
    "ASCW-Golden-Runic.ttf": "Copyright 2017 Google Inc. All Rights Reserved. (Noto Sans Runic)",
}


def write_fontlog(faces):
    L = ["<!--", "GENERATED by tools/fonts/gen_ascw_golden.py from the fonts' name tables.",
         "SPDX-License-Identifier: OFL-1.1", "-->", "",
         "# ASCW Golden: FONTLOG", "",
         "Every face here is a renamed, modified build of an upstream font under the",
         "SIL Open Font License 1.1 (OFL.txt in this folder). The ASCW changes:",
         "family renaming (the OFL's Reserved Font Name clause; Adobe's Source Han",
         "fonts reserve \"Source\", which no ASCW name uses), harmonised vertical",
         "metrics (most faces: ascender 0.618 em, descender 0.382 em, the golden",
         "section), and for",
         "the CJK faces a smaller character set. The fonts stay under the OFL; they",
         "are not relicensed under the Apache License that covers ZXV's software.", "",
         "ASCW Golden Cherokee was rebuilt for ZXV from Noto Sans Cherokee (OFL), because",
         "the Cherokee face in the ASCW source folder was Plantagenet Cherokee, a",
         "Tiro Typeworks font licensed with Windows, which may not be redistributed.", "",
         "| File | Family | Upstream (name ID 3) | Version | Copyright (name ID 0) |",
         "|---|---|---|---|---|"]
    for f in faces:
        c = f["copyright"] or UPSTREAM_COPYRIGHT.get(f["file"], "")
        if not c:
            sys.exit("no copyright notice for %s" % f["file"])
        L.append("| %s | %s | %s | %s | %s |" % (f["file"], f["family"], f["unique"].replace("|", "/"),
                                                f["version"].split(";")[0], c))
    L.append("")
    with open(os.path.join(FONTS, "FONTLOG.md"), "w") as fp:
        fp.write("\n".join(L))


if __name__ == "__main__":
    main()
