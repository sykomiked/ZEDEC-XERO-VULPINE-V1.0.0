#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Generate the Han character tables of zui's input methods (zui_ime.h M6).

    python3 tools/zui/gen_ime_han.py [--unihan DIR]

Reads the Unihan database (Unicode 15.1, downloaded from unicode.org unless
--unihan names a folder holding the Unihan_*.txt files) and writes
kernel/src/zui/zui_ime_han.inc with four tables of zui_ime_entry_t, one
character per candidate, most used first:

  HAN_PINYIN       Simplified Chinese: the 3500 common characters of the
                   Table of General Standard Chinese Characters (kTGH), by
                   toneless Pinyin (u-umlaut typed v), ordered by the
                   Xiandai Hanyu Pinlu counts (kHanyuPinlu)
  HAN_PINYIN_HANT  Traditional Chinese: characters with a frequency or a
                   Hong Kong school grade (kFrequency, kGradeLevel) that are
                   not simplified forms (kTraditionalVariant), by
                   toneless Pinyin (kMandarin), ordered by frequency
  HAN_JYUTPING     Cantonese: the same characters by toneless Jyutping
                   (kCantonese)
  HAN_KANA         Japanese: the 2136 Joyo kanji (kJoyoKanji) by reading in
                   hiragana (kJapanese, on readings turned into hiragana);
                   a kun reading that ends like a verb or adjective also
                   offers the kanji with that last kana as okurigana

These are single characters only: no word dictionary. The Unihan database
is under the Unicode License v3.
"""
import argparse
import os
import sys
import unicodedata
import urllib.request
import zipfile

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
OUT = os.path.join(ROOT, "kernel", "src", "zui", "zui_ime_han.inc")
UCD = "15.1.0"
MAX_CANDS = 64  # ZUI_IME_CAND_MAX
OKURI = set("うくぐすつぬぶむるい")


def load(folder):
    if not folder:
        folder = os.path.expanduser(f"~/.cache/zxv-unihan-{UCD}")
        if not os.path.exists(os.path.join(folder, "Unihan_Readings.txt")):
            os.makedirs(folder, exist_ok=True)
            url = f"https://www.unicode.org/Public/{UCD}/ucd/Unihan.zip"
            print("fetching", url, file=sys.stderr)
            path = os.path.join(folder, "Unihan.zip")
            with open(path, "wb") as f:
                f.write(urllib.request.urlopen(url).read())
            zipfile.ZipFile(path).extractall(folder)
    u = {}
    for name in os.listdir(folder):
        if not (name.startswith("Unihan_") and name.endswith(".txt")):
            continue
        with open(os.path.join(folder, name), encoding="utf-8") as f:
            for line in f:
                if line.startswith("#") or not line.strip():
                    continue
                cp, key, val = line.rstrip("\n").split("\t", 2)
                u.setdefault(key, {})[int(cp[2:], 16)] = val
    return u


def toneless(py):
    s = unicodedata.normalize("NFD", py)
    s = s.replace("ü", "v").replace("ü", "v")
    return "".join(c for c in s if c.isascii() and c.isalpha()).lower()


def hira(s):
    return "".join(chr(ord(c) - 0x60) if 0x30A1 <= ord(c) <= 0x30F6 else c for c in s)


def table(name, groups, comment):
    o = [f"/* {comment} */", f"static const zui_ime_entry_t {name}[] = {{"]
    for key in sorted(groups):
        chars = [c for c, _ in sorted(groups[key], key=lambda x: x[1])]
        seen, out = set(), []
        for c in chars:
            if c not in seen:
                seen.add(c)
                out.append(c)
        o.append(f'    {{"{key}", "{" ".join(out[:MAX_CANDS])}"}},')
    o.append("};")
    return o, len(groups)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--unihan")
    a = ap.parse_args()
    if unicodedata.unidata_version != UCD:
        sys.exit(f"Python's unicodedata is {unicodedata.unidata_version}, need {UCD}")
    u = load(a.unihan)
    pinlu = {}
    for cp, v in u["kHanyuPinlu"].items():
        for item in v.split():
            r, _, n = item.partition("(")
            pinlu.setdefault(cp, []).append((toneless(r), int(n.rstrip(")"))))

    def pinlu_count(cp, key):
        return sum(n for r, n in pinlu.get(cp, []) if r == key)

    # Simplified: TGH level 1 (index <= 3500)
    hans = {}
    for cp, v in u["kTGH"].items():
        idx = int(v.split(":")[1])
        if idx > 3500:
            continue
        keys = {r for r, _ in pinlu.get(cp, [])} or {toneless(r) for r in u["kMandarin"].get(cp, "").split()}
        for k in keys:
            hans.setdefault(k, []).append((chr(cp), (-pinlu_count(cp, k), idx)))
    # Traditional and Cantonese: kFrequency or kGradeLevel
    trad = set(u["kFrequency"]) | set(u["kGradeLevel"])

    def simplified_only(cp):  # a simplified form whose traditional form differs
        v = u["kTraditionalVariant"].get(cp)
        return bool(v) and f"U+{cp:04X}" not in v.split()

    trad = {cp for cp in trad if not simplified_only(cp)}

    def also_simplified(cp):  # also the simplified form of another character
        return len(u["kTraditionalVariant"].get(cp, "").split()) > 1

    def trad_rank(cp):
        return (int(u["kFrequency"].get(cp, 6)) + 2 * also_simplified(cp),
                int(u["kGradeLevel"].get(cp, 7)), cp)

    def simp_of(cp):
        v = u["kSimplifiedVariant"].get(cp)
        return [int(x[2:], 16) for x in v.split()] if v else [cp]

    hant, yue = {}, {}
    for cp in trad:
        for r in u["kMandarin"].get(cp, "").split():
            k = toneless(r)
            n = max(pinlu_count(s, k) for s in simp_of(cp) + [cp])
            hant.setdefault(k, []).append((chr(cp), (trad_rank(cp)[0], -n) + trad_rank(cp)[1:]))
        for r in u["kCantonese"].get(cp, "").split():
            k = "".join(c for c in r if c.isalpha())
            yue.setdefault(k, []).append((chr(cp), trad_rank(cp)))
    # Japanese: Joyo kanji
    kana = {}
    for cp, v in u["kJoyoKanji"].items():
        if v != "2010":
            continue
        rank = (int(u["kFrequency"].get(cp, 6)), int(u["kTotalStrokes"].get(cp, "99").split()[0]), cp)
        for r in u["kJapanese"].get(cp, "").split():
            key = hira(r)
            kana.setdefault(key, []).append((chr(cp), rank))
            if r == key and len(key) > 1 and key[-1] in OKURI:  # a kun reading
                kana.setdefault(key, []).append((chr(cp) + key[-1], (9,) + rank))
    o = ["/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */",
         "/* SPDX-License-Identifier: Apache-2.0 */",
         f"/* Generated by tools/zui/gen_ime_han.py from the Unicode {UCD} Unihan database",
         " * (Unicode License v3). Single characters, most used first. Do not edit. */",
         "// clang-format off"]
    counts = []
    for name, groups, comment in [
        ("HAN_PINYIN", hans, "Simplified Chinese by toneless Pinyin"),
        ("HAN_PINYIN_HANT", hant, "Traditional Chinese by toneless Pinyin"),
        ("HAN_JYUTPING", yue, "Cantonese by toneless Jyutping"),
        ("HAN_KANA", kana, "Joyo kanji by reading in hiragana"),
    ]:
        t, n = table(name, groups, comment)
        o += t
        counts.append(f"{name} {n}")
    o.append("// clang-format on")
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(o) + "\n")
    print(", ".join(counts))


if __name__ == "__main__":
    main()
