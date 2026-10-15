#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Generate the sign-name input methods of the historical scripts (zui_ime.h M7).

    python3 tools/zui/gen_ime_signs.py

Scripts with no keyboard layout of their own (cuneiform, Egyptian
hieroglyphs, Linear B, Gothic, Old Persian, ...) are typed by the names
Unicode gives their signs: the name without the script's prefix, lower case,
letters and digits only. "CUNEIFORM SIGN KA2" is typed ka2, "EGYPTIAN
HIEROGLYPH A001" a001, "GOTHIC LETTER AHSA" ahsa, "LINEAR B SYLLABLE B008 A"
either a or b008, and a runic letter by any of its names ("RUNIC LETTER
FEHU FEOH FE F": fehu, feoh, fe or f). Where a name has a catalogue number
(Gardiner's for Egyptian, Bennett's for Linear B, Laroche's for Luwian, ...)
the number alone is a key too.

Writes kernel/src/zui/zui_ime_signs.inc: one table and one engine per
script, the script-to-engine table, and the locale-to-engine table for the
locales of tools/zui/locales/LANGUAGES.tsv written in those scripts. Names
come from Python's unicodedata, which must be Unicode 15.1 (the version of
the rest of zui's Unicode tables).
"""
import os
import re
import sys
import unicodedata

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
OUT = os.path.join(ROOT, "kernel", "src", "zui", "zui_ime_signs.inc")
LANGS = os.path.join(ROOT, "tools", "zui", "locales", "LANGUAGES.tsv")
UCD = "15.1.0"
KEY_MAX = 24  # zui_ime_table_t holds 24 letters
MAX_CANDS = 64  # ZUI_IME_CAND_MAX

# ISO 15924 code, engine name, the name prefixes of its signs (in candidate
# order), and how a name becomes keys: "name" (the rest of the name), "words"
# (each word of the rest, for names that list several), "code" (the rest,
# plus its catalogue number alone when it has one)
SCRIPTS = [
    ("Xsux", "Cuneiform", ["CUNEIFORM SIGN ", "CUNEIFORM NUMERIC SIGN ",
                           "CUNEIFORM PUNCTUATION SIGN "], "name"),
    ("Egyp", "Egyptian hieroglyphs", ["EGYPTIAN HIEROGLYPH "], "code"),
    ("Hluw", "Anatolian hieroglyphs", ["ANATOLIAN HIEROGLYPH "], "code"),
    ("Linb", "Linear B", ["LINEAR B SYLLABLE ", "LINEAR B SYMBOL ", "LINEAR B IDEOGRAM ",
                          "LINEAR B MONOGRAM "], "code"),
    ("Lina", "Linear A", ["LINEAR A SIGN "], "code"),
    ("Cprt", "Cypriot syllabary", ["CYPRIOT SYLLABLE "], "name"),
    ("Cpmn", "Cypro-Minoan", ["CYPRO-MINOAN SIGN "], "code"),
    ("Xpeo", "Old Persian", ["OLD PERSIAN SIGN "], "name"),
    ("Ugar", "Ugaritic", ["UGARITIC LETTER "], "name"),
    ("Phnx", "Phoenician", ["PHOENICIAN LETTER "], "name"),
    ("Armi", "Imperial Aramaic", ["IMPERIAL ARAMAIC LETTER "], "name"),
    ("Nbat", "Nabataean", ["NABATAEAN LETTER ", "NABATAEAN FINAL LETTER "], "name"),
    ("Palm", "Palmyrene", ["PALMYRENE LETTER ", "PALMYRENE LEFT-POINTING ",
                           "PALMYRENE RIGHT-POINTING "], "name"),
    ("Hatr", "Hatran", ["HATRAN LETTER "], "name"),
    ("Elym", "Elymaic", ["ELYMAIC LETTER ", "ELYMAIC LIGATURE "], "name"),
    ("Mand", "Mandaic", ["MANDAIC LETTER "], "name"),
    ("Samr", "Samaritan", ["SAMARITAN LETTER "], "name"),
    ("Sarb", "Old South Arabian", ["OLD SOUTH ARABIAN LETTER "], "name"),
    ("Narb", "Old North Arabian", ["OLD NORTH ARABIAN LETTER "], "name"),
    ("Avst", "Avestan", ["AVESTAN LETTER "], "name"),
    ("Phli", "Inscriptional Pahlavi", ["INSCRIPTIONAL PAHLAVI LETTER "], "name"),
    ("Phlp", "Psalter Pahlavi", ["PSALTER PAHLAVI LETTER "], "name"),
    ("Prti", "Inscriptional Parthian", ["INSCRIPTIONAL PARTHIAN LETTER "], "name"),
    ("Mani", "Manichaean", ["MANICHAEAN LETTER "], "name"),
    ("Sogd", "Sogdian", ["SOGDIAN LETTER ", "SOGDIAN INDEPENDENT "], "name"),
    ("Sogo", "Old Sogdian", ["OLD SOGDIAN LETTER "], "name"),
    ("Chrs", "Chorasmian", ["CHORASMIAN LETTER "], "name"),
    ("Orkh", "Old Turkic", ["OLD TURKIC LETTER "], "name"),
    ("Hung", "Old Hungarian", ["OLD HUNGARIAN SMALL LETTER ", "OLD HUNGARIAN CAPITAL LETTER "],
     "name"),
    ("Ital", "Old Italic", ["OLD ITALIC LETTER "], "name"),
    ("Lyci", "Lycian", ["LYCIAN LETTER "], "name"),
    ("Lydi", "Lydian", ["LYDIAN LETTER "], "name"),
    ("Cari", "Carian", ["CARIAN LETTER "], "name"),
    ("Mero", "Meroitic", ["MEROITIC HIEROGLYPHIC LETTER ", "MEROITIC CURSIVE LETTER "], "name"),
    ("Goth", "Gothic", ["GOTHIC LETTER "], "name"),
    ("Runr", "Runic", ["RUNIC LETTER "], "words"),
    ("Copt", "Coptic", ["COPTIC SMALL LETTER ", "COPTIC CAPITAL LETTER "], "name"),
    ("Glag", "Glagolitic", ["GLAGOLITIC SMALL LETTER ", "GLAGOLITIC CAPITAL LETTER "], "name"),
    ("Perm", "Old Permic", ["OLD PERMIC LETTER "], "name"),
    ("Aghb", "Caucasian Albanian", ["CAUCASIAN ALBANIAN LETTER "], "name"),
    ("Elba", "Elbasan", ["ELBASAN LETTER "], "name"),
    ("Vith", "Vithkuqi", ["VITHKUQI SMALL LETTER ", "VITHKUQI CAPITAL LETTER "], "name"),
    ("Brah", "Brahmi", ["BRAHMI LETTER ", "BRAHMI VOWEL SIGN ", "BRAHMI SIGN ", "BRAHMI DIGIT "],
     "name"),
    ("Khar", "Kharoshthi", ["KHAROSHTHI LETTER ", "KHAROSHTHI VOWEL SIGN ", "KHAROSHTHI SIGN "],
     "name"),
    ("Sidd", "Siddham", ["SIDDHAM LETTER ", "SIDDHAM VOWEL SIGN ", "SIDDHAM SIGN "], "name"),
    ("Shrd", "Sharada", ["SHARADA LETTER ", "SHARADA VOWEL SIGN ", "SHARADA SIGN "], "name"),
    ("Gran", "Grantha", ["GRANTHA LETTER ", "GRANTHA VOWEL SIGN ", "GRANTHA SIGN "], "name"),
    ("Kawi", "Kawi", ["KAWI LETTER ", "KAWI VOWEL SIGN ", "KAWI SIGN "], "name"),
    ("Marc", "Marchen", ["MARCHEN LETTER ", "MARCHEN VOWEL SIGN ", "MARCHEN SUBJOINED LETTER "],
     "name"),
    ("Mong", "Mongolian", ["MONGOLIAN LETTER "], "name"),
]
CODE = re.compile(r"^(?:[A-Z]{1,2}\d{3}[A-Z]?|B\d{3}[A-Z]*|AB\d{3}[A-Z]?|CM\d{3}[A-Z]?)$")


def key(s):
    return "".join(c for c in s.lower() if c.isascii() and c.isalnum())


def keys_of(rest, how):
    if how == "words":
        return [key(w) for w in rest.split()]
    if how == "code":
        words = rest.split()
        codes = [w for w in words if CODE.match(w)]
        out = [key(rest)]
        if codes:
            out.append(key(codes[0]))
            other = " ".join(w for w in words if w != codes[0])
            if other:
                out.append(key(other))
        return out
    return [key(rest)]


def main():
    if unicodedata.unidata_version != UCD:
        sys.exit(f"Python's unicodedata is {unicodedata.unidata_version}, need {UCD}")
    names = []
    for cp in range(0x80, 0x110000):
        n = unicodedata.name(chr(cp), "")
        if n:
            names.append((cp, n))
    with open(LANGS, encoding="utf-8") as f:
        rows = [l.rstrip("\n").split("\t") for l in f if l.strip() and not l.startswith("#")]
    o = ["/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */",
         "/* SPDX-License-Identifier: Apache-2.0 */",
         f"/* Generated by tools/zui/gen_ime_signs.py from the Unicode {UCD} character names.",
         " * Do not edit. */",
         "// clang-format off"]
    engines, report, skipped = [], [], 0
    for code, title, prefixes, how in SCRIPTS:
        groups = {}
        for rank, pre in enumerate(prefixes):
            for cp, n in names:
                if not n.startswith(pre):
                    continue
                for k in keys_of(n[len(pre):], how):
                    if not k or len(k) > KEY_MAX:
                        skipped += 1
                        continue
                    groups.setdefault(k, [])
                    if chr(cp) not in groups[k]:
                        groups[k].append(chr(cp))
        if not groups:
            sys.exit(f"{code}: no signs named {prefixes}")
        cid = code.upper()
        o.append(f"/* {title}: {', '.join(p.strip() for p in prefixes)} */")
        o.append(f"static const zui_ime_entry_t SIGNS_{cid}[] = {{")
        for k in sorted(groups):
            o.append(f'    {{"{k}", "{" ".join(groups[k][:MAX_CANDS])}"}},')
        o.append("};")
        o.append(f"static const zui_ime_dict_t SIGNS_{cid}_DICT = "
                 f"{{SIGNS_{cid}, (uint32_t) (sizeof SIGNS_{cid} / sizeof SIGNS_{cid}[0])}};")
        o.append(f'static const zui_ime_engine_t zui_ime_signs_{code.lower()} = {{"signs-{code.lower()}", '
                 f'"{title} by sign name", &SIGNS_{cid}_DICT, zui_ime_table_reset, signs_key, '
                 f"signs_text, zui_ime_table_commit, zui_ime_table_cancel, zui_ime_table_select}};")
        engines.append(code)
        report.append(f"{code} {len(groups)}")
    o.append("/* the sign-name engine of each script */")
    o.append("static const struct {")
    o.append("    const char *script;")
    o.append("    const zui_ime_engine_t *eng;")
    o.append("} SCRIPT_SIGNS[] = {")
    for code in engines:
        o.append(f'    {{"{code}", &zui_ime_signs_{code.lower()}}},')
    o.append("};")
    o.append("/* the locales of LANGUAGES.tsv written in those scripts */")
    o.append("static const struct {")
    o.append("    const char *tag;")
    o.append("    const zui_ime_engine_t *eng;")
    o.append("} LOCALE_SIGNS[] = {")
    nloc = 0
    for r in rows:
        if r[2] in engines:
            o.append(f'    {{"{r[0]}", &zui_ime_signs_{r[2].lower()}}},')
            nloc += 1
    o.append("};")
    o.append("// clang-format on")
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(o) + "\n")
    print(f"{len(engines)} scripts, {nloc} locales, {skipped} names longer than {KEY_MAX}; "
          + ", ".join(report))


if __name__ == "__main__":
    main()
