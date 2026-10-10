#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Generate the kernel/src/i18n tables from Unicode CLDR and the catalogs.

Inputs
  CLDR: the cldr-json npm packages, version 48.2.0 (published 2026-03-18):
        cldr-core, cldr-numbers-full, cldr-dates-full, cldr-localenames-full,
        cldr-misc-full. Unpack each tarball into <cldr-dir>/<package>/ so that
        <cldr-dir>/cldr-core/package/supplemental/plurals.json exists.
          https://registry.npmjs.org/<package>/-/<package>-48.2.0.tgz
        CLDR data is (c) Unicode, Inc., under the Unicode License v3.
  Unicode character properties: Python's unicodedata module (its
        unidata_version is recorded in the output).
  Catalogs: kernel/src/i18n/catalog/*.msg (format documented in en.msg).
  kernel/src/font/script.c: its script range table, to compute which
        codepoints each locale needs that the font module cannot classify.

Outputs (in the i18n directory)
  i18n_tables.c   locale registry, number/date data, plural rules, currency
                  symbols, grapheme-extend ranges, per-locale coverage ranges
  i18n_catalog.c  message catalogs
  i18n_msgid.h    message ids (the core's internal identifiers)
  and, with --docs FILE, rewrites the generated section of docs/LANGUAGES.md.

Usage
  python3 -I gen_i18n_tables.py <cldr-dir> [--docs ../../../docs/LANGUAGES.md]
"""
import json
import os
import re
import sys
import unicodedata

HERE = os.path.dirname(os.path.abspath(__file__))
CLDR_VERSION = "48.2.0"

# ---------------------------------------------------------------------------
# Locales that CLDR does not have. Every field here is OUR data, not CLDR's:
# number and date formats come from the CLDR root locale, the English name
# from CLDR's English names when CLDR has one, and the autonym is ours and is
# flagged unverified.
#   tag: (english-if-CLDR-lacks-it, autonym, script, note)
EXTRA = {
    "kr": (None, "Kanuri", "Latn", "Kanuri; no CLDR locale"),
    "kg": (None, "Kikongo", "Latn", "Kongo; no CLDR locale"),
    "crs": (None, "Kreol Seselwa", "Latn", "Seychellois Creole; no CLDR locale"),
    "swb": (None, "Shikomori", "Latn", "Comorian (Maore); no CLDR locale"),
    "ay": (None, "Aymar aru", "Latn", "no CLDR locale"),
    "nah": ("Nahuatl", "Nāhuatl", "Latn", "collective code; no CLDR locale"),
    "yua": ("Yucatec Maya", "Maaya t’aan", "Latn", "no CLDR locale"),
    "sm": (None, "Gagana Sāmoa", "Latn", "no CLDR locale"),
    "rom": (None, "romani čhib", "Latn", "macrolanguage; no CLDR locale"),
    "arc": (None, "𐡀𐡓𐡌𐡉𐡀", "Armi", "Imperial Aramaic; no CLDR locale"),
    "arc-Hebr": (None, "ארמית", "Hebr", "Biblical/Jewish Aramaic in square script; no CLDR locale"),
    "aii": ("Assyrian Neo-Aramaic", "ܣܘܪܝܬ", "Syrc", "no CLDR locale"),
    "sa-Latn": (None, None, "Latn", "IAST transliteration derived from CLDR sa"),
}

AU_MEMBERS = set("""DZ AO BJ BW BF BI CV CM CF TD KM CG CD CI DJ EG GQ ER SZ ET GA GM GH GN GW
KE LS LR LY MG MW ML MR MU MA MZ NA NE NG RW EH ST SN SC SL SO ZA SS SD TZ TG TN UG ZM
ZW""".split())

# Currencies whose symbols are emitted. Every currency in CLDR's currencyData
# that is legal tender today somewhere, plus a few the app shows.
EXTRA_CURRENCIES = ["XAU", "XDR"]

# Messages taken from CLDR for every locale (status cldr).
CLDR_MSGS = [
    ("TIME_MINUTES_AGO", "minute"),
    ("TIME_HOURS_AGO", "hour"),
    ("TIME_DAYS_AGO", "day"),
]

PLURAL_CATS = ["zero", "one", "two", "few", "many", "other"]

# ---------------------------------------------------------------------------


def die(msg):
    sys.stderr.write("gen_i18n_tables: " + msg + "\n")
    sys.exit(1)


class Cldr:
    def __init__(self, root):
        self.root = root
        self.cache = {}

    def path(self, pkg, *rest):
        return os.path.join(self.root, pkg, "package", *rest)

    def load(self, pkg, *rest):
        p = self.path(pkg, *rest)
        if p not in self.cache:
            if not os.path.exists(p):
                self.cache[p] = None
            else:
                with open(p, encoding="utf-8") as f:
                    self.cache[p] = json.load(f)
        return self.cache[p]

    def main(self, pkg, tag, fname):
        d = self.load(pkg, "main", tag, fname)
        if d is None:
            return None
        return d["main"][tag]

    def supp(self, fname):
        return self.load("cldr-core", "supplemental", fname)["supplemental"]


# ---------------------------------------------------------------------------
# string pool with de-duplication


class Pool:
    def __init__(self):
        self.off = {}
        self.data = bytearray()

    def add(self, s):
        if s is None:
            s = ""
        if s in self.off:
            return self.off[s]
        o = len(self.data)
        b = s.encode("utf-8")
        if b"\0" in b:
            die("NUL in string")
        self.data += b + b"\0"
        self.off[s] = o
        return o


def c_bytes(b):
    """Escape bytes for a C string literal: ASCII printable stays, others octal."""
    out = []
    for x in b:
        if x == 0x5C:
            out.append("\\\\")
        elif x == 0x22:
            out.append('\\"')
        elif x == 0x3F:
            out.append("\\077")  # avoid trigraphs
        elif 0x20 <= x < 0x7F:
            out.append(chr(x))
        else:
            out.append("\\%03o" % x)
    return "".join(out)


def emit_pool(pool, name):
    lines = ["static const char %s[] =" % name]
    data = bytes(pool.data)
    # split at NULs so each line is one or more whole strings
    parts = data.split(b"\0")[:-1]
    cur = ""
    for p in parts:
        piece = c_bytes(p) + "\\000"
        if len(cur) + len(piece) > 90 and cur:
            lines.append('    "%s"' % cur)
            cur = ""
        cur += piece
    if cur:
        lines.append('    "%s"' % cur)
    lines[-1] += ";"
    return "\n".join(lines) + "\n"


# ---------------------------------------------------------------------------
# UnicodeSet (exemplar) parsing: enough for CLDR exemplar sets.


def parse_uset(s):
    s = s.strip()
    if not (s.startswith("[") and s.endswith("]")):
        die("bad exemplar set " + s[:40])
    s = s[1:-1]
    items = []
    i = 0
    prev = None

    def unesc(j):
        c = s[j + 1]
        if c == "u":
            return chr(int(s[j + 2 : j + 6], 16)), j + 6
        if c == "U":
            return chr(int(s[j + 2 : j + 10], 16)), j + 10
        if c == "x" and s[j + 2] == "{":
            k = s.index("}", j)
            return chr(int(s[j + 3 : k], 16)), k + 1
        return c, j + 2

    while i < len(s):
        c = s[i]
        if c == " ":
            i += 1
            continue
        if c == "{":
            k = s.index("}", i)
            items.append(s[i + 1 : k])
            prev = None
            i = k + 1
            continue
        if c == "\\":
            ch, i = unesc(i)
        elif c == "-" and prev is not None and i + 1 < len(s) and s[i + 1] not in " ]":
            j = i + 1
            if s[j] == "\\":
                hi, i = unesc(j)
            else:
                hi, i = s[j], j + 1
            for x in range(ord(prev) + 1, ord(hi) + 1):
                items.append(chr(x))
            prev = None
            continue
        else:
            ch = c
            i += 1
        items.append(ch)
        prev = ch
    return items


def ranges_of(cps):
    cps = sorted(set(cps))
    out = []
    for c in cps:
        if out and out[-1][1] + 1 == c:
            out[-1][1] = c
        else:
            out.append([c, c])
    return [tuple(r) for r in out]


# ---------------------------------------------------------------------------
# Plural rules: parsed into a small relation table the C side interprets.

OPERANDS = {"n": 0, "i": 1, "v": 2, "w": 3, "f": 4, "t": 5, "e": 6, "c": 7}


def parse_rule(rule):
    rule = rule.split("@")[0].strip()
    if not rule:
        return []  # always true (the "other" category)
    ors = []
    for orpart in rule.split(" or "):
        ands = []
        for rel in orpart.split(" and "):
            m = re.fullmatch(r"\s*([nivwftec])\s*(?:%\s*(\d+))?\s*(!=|=)\s*([\d.,\s]+)\s*", rel)
            if not m:
                die("cannot parse plural relation: " + rel)
            op, mod, cmp_, rl = m.groups()
            rngs = []
            for r in rl.split(","):
                r = r.strip()
                if ".." in r:
                    lo, hi = r.split("..")
                    rngs.append((int(lo), int(hi)))
                else:
                    rngs.append((int(r), int(r)))
            for lo, hi in rngs:
                if hi > 0xFFFFFFFF:
                    die("plural range too large")
            ands.append((OPERANDS[op], int(mod) if mod else 0, cmp_ == "!=", rngs))
        ors.append(ands)
    return ors


# ---------------------------------------------------------------------------
# Number patterns


AFFIX_MAP = {"¤": "\x01", "-": "\x02", "%": "\x03", "‰": "\x04", "+": "\x05"}


def split_pattern(p):
    """Return (prefix, numberpart, suffix) with affix specials mapped."""
    # find the number part: from the first of #0@ to the last of #0@,.
    out_pre, out_num, out_suf = [], [], []
    i = 0
    state = 0
    inq = False
    while i < len(p):
        c = p[i]
        if c == "'":
            if i + 1 < len(p) and p[i + 1] == "'":
                (out_pre if state == 0 else out_suf).append("'")
                i += 2
                continue
            inq = not inq
            i += 1
            continue
        if not inq and c in "#0@,." and state < 2:
            if state == 0 and c in ",.":
                out_pre.append(c)
            else:
                state = 1
                out_num.append(c)
            i += 1
            continue
        if state == 1:
            state = 2
        target = out_pre if state == 0 else out_suf
        if inq:
            target.append(c)
        else:
            target.append(AFFIX_MAP.get(c, c))
        i += 1
    return "".join(out_pre), "".join(out_num), "".join(out_suf)


def parse_number_pattern(pat):
    if ";" in pat:
        pos, neg = pat.split(";", 1)
    else:
        pos, neg = pat, None
    ppre, pnum, psuf = split_pattern(pos)
    if neg is not None:
        npre, _, nsuf = split_pattern(neg)
    else:
        npre, nsuf = "\x02" + ppre, psuf
    ip = pnum.split(".")[0]
    groups = ip.split(",")
    if len(groups) == 1:
        g1, g2 = 0, 0
    else:
        g1 = len(groups[-1])
        g2 = len(groups[-2]) if len(groups) > 2 else g1
    return (ppre, psuf, npre, nsuf), g1, g2


# ---------------------------------------------------------------------------
# Devanagari -> IAST (for sa-Latn, derived from the sa data and catalog)

DEVA_VOWELS = {
    "अ": "a", "आ": "ā", "इ": "i", "ई": "ī", "उ": "u", "ऊ": "ū", "ऋ": "ṛ", "ॠ": "ṝ",
    "ऌ": "ḷ", "ॡ": "ḹ", "ए": "e", "ऐ": "ai", "ओ": "o", "औ": "au", "ऑ": "ŏ", "ऍ": "ĕ",
}
DEVA_SIGNS = {
    "ा": "ā", "ि": "i", "ी": "ī", "ु": "u", "ू": "ū", "ृ": "ṛ", "ॄ": "ṝ", "ॢ": "ḷ",
    "ॣ": "ḹ", "े": "e", "ै": "ai", "ो": "o", "ौ": "au", "ॉ": "ŏ", "ॅ": "ĕ",
}
DEVA_CONS = {
    "क": "k", "ख": "kh", "ग": "g", "घ": "gh", "ङ": "ṅ", "च": "c", "छ": "ch", "ज": "j",
    "झ": "jh", "ञ": "ñ", "ट": "ṭ", "ठ": "ṭh", "ड": "ḍ", "ढ": "ḍh", "ण": "ṇ", "त": "t",
    "थ": "th", "द": "d", "ध": "dh", "न": "n", "प": "p", "फ": "ph", "ब": "b", "भ": "bh",
    "म": "m", "य": "y", "र": "r", "ल": "l", "ळ": "ḻ", "व": "v", "श": "ś", "ष": "ṣ",
    "स": "s", "ह": "h",
}
DEVA_OTHER = {
    "ं": "ṃ", "ः": "ḥ", "ँ": "m̐", "ऽ": "’", "।": "|", "॥": "||", "ॐ": "oṃ",
    "०": "0", "१": "1", "२": "2", "३": "3", "४": "4", "५": "5", "६": "6", "७": "7",
    "८": "8", "९": "9",
}
VIRAMA = "्"
NUKTA = "़"


def deva_to_iast(s):
    out = []
    i = 0
    while i < len(s):
        c = s[i]
        if c in DEVA_CONS:
            out.append(DEVA_CONS[c])
            j = i + 1
            if j < len(s) and s[j] == NUKTA:
                j += 1
            if j < len(s) and s[j] == VIRAMA:
                i = j + 1
                continue
            if j < len(s) and s[j] in DEVA_SIGNS:
                out.append(DEVA_SIGNS[s[j]])
                i = j + 1
                continue
            out.append("a")
            i = j
            continue
        if c in DEVA_VOWELS:
            out.append(DEVA_VOWELS[c])
        elif c in DEVA_OTHER:
            out.append(DEVA_OTHER[c])
        elif c == NUKTA:
            pass
        else:
            out.append(c)
        i += 1
    return unicodedata.normalize("NFC", "".join(out))


# ---------------------------------------------------------------------------
# font/script.c replica, for the coverage report


def load_font_ranges():
    p = os.path.join(HERE, "..", "font", "script.c")
    with open(p, encoding="utf-8") as f:
        src = f.read()
    rng = []
    for m in re.finditer(r"\{\s*(0x[0-9A-Fa-f]+)\s*,\s*(0x[0-9A-Fa-f]+)\s*,\s*SCRIPT_(\w+)\s*\}", src):
        rng.append((int(m.group(1), 16), int(m.group(2), 16), m.group(3)))
    if len(rng) < 10:
        die("could not read RANGES from font/script.c")
    return rng


FONT_RANGES = None


def font_script_of(cp):
    if cp in (0x20, 0x09, 0x0A, 0x0D):
        return "COMMON"
    if 0x21 <= cp <= 0x2F or 0x3A <= cp <= 0x40 or 0x5B <= cp <= 0x60 or 0x7B <= cp <= 0x7E:
        return "COMMON"
    if cp == 0xA0 or 0x2000 <= cp <= 0x206F:
        return "COMMON"
    for lo, hi, s in FONT_RANGES:
        if lo <= cp <= hi:
            return s
    return "UNKNOWN"


# ---------------------------------------------------------------------------


def tag_parts(tag):
    p = tag.split("-")
    lang = p[0]
    script = region = None
    for x in p[1:]:
        if len(x) == 4 and x[0].isalpha():
            script = x
        elif (len(x) == 2 and x.isalpha()) or (len(x) == 3 and x.isdigit()):
            region = x
    return lang, script, region


def main():
    global FONT_RANGES
    args = sys.argv[1:]
    if not args:
        die("usage: gen_i18n_tables.py <cldr-dir> [--docs FILE]")
    cl = Cldr(args[0])
    docs = None
    if "--docs" in args:
        docs = args[args.index("--docs") + 1]
    FONT_RANGES = load_font_ranges()

    likely = cl.supp("likelySubtags.json")["likelySubtags"]
    parent_map = cl.supp("parentLocales.json")["parentLocales"]["parentLocale"]
    plurals = cl.supp("plurals.json")["plurals-type-cardinal"]
    numsys = cl.supp("numberingSystems.json")["numberingSystems"]
    tinfo = cl.supp("territoryInfo.json")["territoryInfo"]
    curdata = cl.supp("currencyData.json")["currencyData"]
    aliases = cl.supp("aliases.json")["metadata"]["alias"]["languageAlias"]
    scriptmeta = cl.load("cldr-core", "scriptMetadata.json")["scriptMetadata"]
    coverage = cl.load("cldr-core", "coverageLevels.json")["effectiveCoverageLevels"]

    cldr_tags = sorted(t for t in os.listdir(cl.path("cldr-numbers-full", "main")) if t != "und")
    all_tags = sorted(set(cldr_tags) | set(EXTRA))
    tagset = set(all_tags)

    en_lang = cl.main("cldr-localenames-full", "en", "languages.json")["localeDisplayNames"]["languages"]
    en_script = cl.main("cldr-localenames-full", "en", "scripts.json")["localeDisplayNames"]["scripts"]
    en_terr = cl.main("cldr-localenames-full", "en", "territories.json")["localeDisplayNames"]["territories"]

    def likely_script(tag):
        lang, script, region = tag_parts(tag)
        if script:
            return script
        for key in (tag, lang + "-" + region if region else None, lang):
            if key and key in likely:
                return tag_parts(likely[key])[1]
        return "Zyyy"

    def display_name(names_lang, names_script, names_terr, tag, pattern="{0} ({1})", sep="{0}, {1}"):
        if tag in names_lang:
            return names_lang[tag]
        lang, script, region = tag_parts(tag)
        base = names_lang.get(lang)
        if base is None:
            return None
        extra = []
        if script:
            extra.append(names_script.get(script, script))
        if region:
            extra.append(names_terr.get(region, region))
        if not extra:
            return base
        e = extra[0]
        for x in extra[1:]:
            e = sep.replace("{0}", e).replace("{1}", x)
        return pattern.replace("{0}", base).replace("{1}", e)

    # ---- parents ----
    def parent_of(tag):
        if tag in parent_map:
            p = parent_map[tag]
            return None if p in ("root", "und") else p
        if tag == "sa-Latn" or tag == "arc-Hebr":
            return None
        parts = tag.split("-")
        while len(parts) > 1:
            parts = parts[:-1]
            cand = "-".join(parts)
            if cand in tagset:
                lang, script, region = tag_parts(tag)
                # CLDR nonlikelyScript rule: lang-Script with a non-default
                # script inherits from root, not from lang.
                if len(parts) == 1 and script and script != likely_script(lang):
                    return None
                return cand
        return None

    # ---- plural rule sets ----
    plural_sets = []  # list of (cats tuple, parsed rules list)
    plural_index = {}

    def plural_for(tag):
        lang = tag.split("-")[0]
        parts = tag.split("-")
        rules = None
        while parts:
            k = "_".join(parts)
            if k in plurals:
                rules = plurals[k]
                break
            parts = parts[:-1]
        has = rules is not None
        if rules is None:
            rules = {"pluralRule-count-other": ""}
        cats = []
        parsed = []
        for c in PLURAL_CATS:
            k = "pluralRule-count-" + c
            if k in rules:
                if c == "other":
                    continue
                cats.append(c)
                parsed.append(parse_rule(rules[k]))
        key = json.dumps([cats, parsed])
        if key not in plural_index:
            plural_index[key] = len(plural_sets)
            plural_sets.append((cats, parsed))
        _ = lang
        return plural_index[key], has, cats + ["other"]

    # ---- digits ----
    digitsets = []
    digit_index = {}

    def digits_for(ns):
        d = numsys[ns]
        if d.get("_type") != "numeric":
            die("non-numeric default numbering system " + ns)
        cps = [ord(x) for x in d["_digits"]]
        if len(cps) != 10:
            die("numbering system %s has %d digits" % (ns, len(cps)))
        key = tuple(cps)
        if key not in digit_index:
            digit_index[key] = len(digitsets)
            digitsets.append((ns, key))
        return digit_index[key]

    pool = Pool()

    # ---- number formats ----
    numfmts = []
    numfmt_index = {}

    def numfmt_for(src):
        n = cl.main("cldr-numbers-full", src, "numbers.json")["numbers"]
        ns = n["defaultNumberingSystem"]
        sym = n["symbols-numberSystem-" + ns]
        dec = n["decimalFormats-numberSystem-" + ns]["standard"]
        pct = n["percentFormats-numberSystem-" + ns]["standard"]
        curf = n["currencyFormats-numberSystem-" + ns]
        cur = curf["standard"]
        cura = curf.get("standard-alphaNextToNumber", cur)
        spacing = curf.get("currencySpacing", {})
        ins_before = spacing.get("beforeCurrency", {}).get("insertBetween", " ")
        ins_after = spacing.get("afterCurrency", {}).get("insertBetween", " ")
        da, g1, g2 = parse_number_pattern(dec)
        pa, pg1, pg2 = parse_number_pattern(pct)
        ca, cg1, cg2 = parse_number_pattern(cur)
        caa, ag1, ag2 = parse_number_pattern(cura)
        rec = (
            ns,
            digits_for(ns),
            sym["decimal"],
            sym["group"],
            sym["minusSign"],
            sym["plusSign"],
            sym["percentSign"],
            int(n.get("minimumGroupingDigits", "1")),
            g1 | (g2 << 4),
            pg1 | (pg2 << 4),
            cg1 | (cg2 << 4),
            ag1 | (ag2 << 4),
            da,
            pa,
            ca,
            caa,
            ins_before,
            ins_after,
        )
        if rec not in numfmt_index:
            numfmt_index[rec] = len(numfmts)
            numfmts.append(rec)
        return numfmt_index[rec], ns

    # ---- dates ----
    monthsets, monthset_index = [], {}
    daysets, dayset_index = [], {}
    datefmts, datefmt_index = [], {}
    DAYKEYS = ["sun", "mon", "tue", "wed", "thu", "fri", "sat"]
    TOKENS = {
        "d": 0x11, "dd": 0x12, "M": 0x13, "MM": 0x14, "MMM": 0x15, "MMMM": 0x16,
        "L": 0x13, "LL": 0x14, "LLL": 0x17, "LLLL": 0x18,
        "y": 0x19, "yy": 0x1A, "yyyy": 0x1B,
        "E": 0x1C, "EE": 0x1C, "EEE": 0x1C, "EEEE": 0x1D, "c": 0x1C, "ccc": 0x1C, "cccc": 0x1D,
        "G": 0x1E, "GG": 0x1E, "GGG": 0x1E,
    }

    def compile_date(p):
        out = []
        i = 0
        while i < len(p):
            c = p[i]
            if c == "'":
                if i + 1 < len(p) and p[i + 1] == "'":
                    out.append("'")
                    i += 2
                    continue
                j = p.index("'", i + 1)
                out.append(p[i + 1 : j])
                i = j + 1
                continue
            if c.isascii() and c.isalpha():
                j = i
                while j < len(p) and p[j] == c:
                    j += 1
                tok = p[i:j]
                if tok not in TOKENS:
                    die("unsupported date token %r in %r" % (tok, p))
                out.append(chr(TOKENS[tok]))
                i = j
                continue
            out.append(c)
            i += 1
        return "".join(out)

    def nameset(store, index, names):
        key = tuple(pool.add(x) for x in names)
        if key not in index:
            index[key] = len(store)
            store.append(key)
        return index[key]

    def datefmt_for(src, translit=False):
        g = cl.main("cldr-dates-full", src, "ca-gregorian.json")["dates"]["calendars"]["gregorian"]
        tr = deva_to_iast if translit else (lambda x: x)
        pats = []
        for k in ("full", "long", "medium", "short"):
            p = g["dateFormats"][k]
            if isinstance(p, dict):
                p = p["_value"]
            pats.append(compile_date(tr(p)))
        mf = g["months"]["format"]
        ms = g["months"]["stand-alone"]
        df = g["days"]["format"]
        ma = nameset(monthsets, monthset_index, [tr(mf["abbreviated"][str(i)]) for i in range(1, 13)])
        mw = nameset(monthsets, monthset_index, [tr(mf["wide"][str(i)]) for i in range(1, 13)])
        msa = nameset(monthsets, monthset_index, [tr(ms["abbreviated"][str(i)]) for i in range(1, 13)])
        msw = nameset(monthsets, monthset_index, [tr(ms["wide"][str(i)]) for i in range(1, 13)])
        da = nameset(daysets, dayset_index, [tr(df["abbreviated"][k]) for k in DAYKEYS])
        dw = nameset(daysets, dayset_index, [tr(df["wide"][k]) for k in DAYKEYS])
        era = tr(g["eras"]["eraAbbr"]["1"])
        has_names = not re.fullmatch(r"M0?1", mf["wide"]["1"])
        rec = (tuple(pool.add(p) for p in pats), ma, mw, msa, msw, da, dw, pool.add(era), has_names)
        if rec not in datefmt_index:
            datefmt_index[rec] = len(datefmts)
            datefmts.append(rec)
        return datefmt_index[rec], has_names

    # ---- currencies ----
    tender = set()
    for reg, lst in curdata["region"].items():
        for ent in lst:
            for code, info in ent.items():
                if "_to" not in info and info.get("_tender", "true") != "false":
                    tender.add(code)
    tender |= set(EXTRA_CURRENCIES)
    tender = sorted(tender)
    region_cur = {}
    for reg, lst in curdata["region"].items():
        for ent in lst:
            for code, info in ent.items():
                if "_to" not in info and info.get("_tender", "true") != "false":
                    region_cur[reg] = code
    cur_digits = {}
    for code, f in curdata["fractions"].items():
        if code != "DEFAULT":
            cur_digits[code] = int(f["_digits"])

    def is_sym_or_space(ch):
        cat = unicodedata.category(ch)
        return cat[0] in "SZ"

    symsets, symset_index = [], {}
    syms = []  # (code, sym_off, narrow_off, flags)

    def sym_table(src):
        c = cl.main("cldr-numbers-full", src, "currencies.json")["numbers"]["currencies"]
        t = {}
        for code in tender:
            if code not in c:
                continue
            t[code] = (c[code].get("symbol", code), c[code].get("symbol-alt-narrow", c[code].get("symbol", code)))
        return t

    def symset_for(ents):
        """ents: list of (code, symbol, narrow), stored as a delta over the parent."""
        key = tuple(ents)
        if key not in symset_index:
            start = len(syms)
            for code, s, nn in ents:
                fl = 0
                if s and is_sym_or_space(s[0]):
                    fl |= 1
                if s and is_sym_or_space(s[-1]):
                    fl |= 2
                if nn and is_sym_or_space(nn[0]):
                    fl |= 4
                if nn and is_sym_or_space(nn[-1]):
                    fl |= 8
                syms.append((code, pool.add(s), pool.add(nn), fl))
            symset_index[key] = len(symsets)
            symsets.append((start, len(ents)))
        return symset_index[key]

    # ---- coverage ----
    covsets, covset_index = [], {}
    cov_ranges = []

    def covset_for(src, digits_idx, translit=False):
        ch = cl.main("cldr-misc-full", src, "characters.json")
        cps = set()
        if ch:
            ch = ch["characters"]
            for k in ("exemplarCharacters", "punctuation"):
                if k in ch:
                    for item in parse_uset(ch[k]):
                        if translit:
                            item = deva_to_iast(item)
                        for form in ("NFC", "NFD"):
                            for x in unicodedata.normalize(form, item):
                                cps.add(ord(x))
        for d in digitsets[digits_idx][1]:
            cps.add(d)
        cps.discard(0x20)
        rngs = ranges_of(cps)
        gaps = sum(1 for c in cps if font_script_of(c) == "UNKNOWN")
        key = tuple(rngs)
        if key not in covset_index:
            covset_index[key] = len(covsets)
            covsets.append((len(cov_ranges), len(rngs), gaps, len(cps)))
            cov_ranges.extend(rngs)
        return covset_index[key], gaps, len(cps), cps

    # ---- territories ----
    lang_terr = {}
    for reg, info in tinfo.items():
        for lk, li in info.get("languagePopulation", {}).items():
            pct = float(li.get("_populationPercent", "0"))
            off = "_officialStatus" in li
            if off or pct >= 2.0:
                lang_terr.setdefault(lk, []).append(reg)

    def territories(tag):
        lang, script, region = tag_parts(tag)
        if region:
            return [region]
        key = lang + ("_" + script if script and script != likely_script(lang) else "")
        return sorted(lang_terr.get(key, []))

    def ldn(tag, what):
        d = cl.main("cldr-localenames-full", tag, what + ".json")
        return d["localeDisplayNames"][what] if d else {}

    # ---- build locales ----
    locales = []
    loc_index = {}
    und_rel = cl.main("cldr-dates-full", "und", "dateFields.json")["dates"]["fields"]

    for tag in all_tags:
        extra = EXTRA.get(tag)
        lang, script, region = tag_parts(tag)
        derived_sa = tag == "sa-Latn"
        if tag in cldr_tags:
            src = tag
        elif derived_sa:
            src = "sa"
        else:
            src = "und"
        english = display_name(en_lang, en_script, en_terr, tag)
        if english is None:
            english = extra[0] if extra and extra[0] else tag
        autonym = None
        autonym_cldr = False
        if tag in cldr_tags:
            ln = ldn(tag, "languages")
            sn = ldn(tag, "scripts")
            tn = ldn(tag, "territories")
            ldp = (cl.main("cldr-localenames-full", tag, "localeDisplayNames.json") or {}).get(
                "localeDisplayNames", {}).get("localeDisplayPattern", {"localePattern": "{0} ({1})", "localeSeparator": "{0}, {1}"})
            if lang in ln:
                autonym = display_name(ln, sn, tn, tag, ldp["localePattern"], ldp["localeSeparator"])
                autonym_cldr = autonym is not None
        if derived_sa:
            ln = ldn("sa", "languages")
            autonym = deva_to_iast(ln.get("sa", "संस्कृत")) + " (IAST)"
            autonym_cldr = False
        if autonym is None:
            autonym = extra[1] if extra and extra[1] else english
        scr = extra[2] if extra else likely_script(tag)
        if tag in cldr_tags:
            lay = cl.main("cldr-misc-full", tag, "layout.json")
            rtl = lay["layout"]["orientation"]["characterOrder"] == "right-to-left"
        else:
            rtl = scriptmeta.get(scr, {}).get("rtl") == "YES"
        pl, has_pl, cats = plural_for(tag if tag in cldr_tags or tag in EXTRA else tag)
        nf, ns = numfmt_for(src if not derived_sa else "sa")
        if derived_sa:
            # IAST uses Latin digits
            rec = list(numfmts[nf])
            rec[0] = "latn"
            rec[1] = digits_for("latn")
            rec = tuple(rec)
            if rec not in numfmt_index:
                numfmt_index[rec] = len(numfmts)
                numfmts.append(rec)
            nf = numfmt_index[rec]
            ns = "latn"
        df, has_names = datefmt_for(src, translit=derived_sa)
        cv, gaps, ncps, _ = covset_for(src, numfmts[nf][1], translit=derived_sa)
        if src == "und" and not derived_sa:
            cv, gaps, ncps = None, 0, 0
        terr = territories(tag)
        au = [t for t in terr if t in AU_MEMBERS]
        cov = coverage.get(tag.replace("-", "_"), coverage.get(tag, "none" if src == "und" else "basic"))
        flags = 0
        if tag in cldr_tags:
            flags |= 1  # I18N_LF_CLDR
        if autonym_cldr:
            flags |= 2  # I18N_LF_AUTONYM_CLDR
        if has_pl:
            flags |= 4  # I18N_LF_PLURALS_CLDR
        if has_names:
            flags |= 8  # I18N_LF_MONTH_NAMES
        if derived_sa:
            flags |= 16  # I18N_LF_DERIVED
        loc_index[tag] = len(locales)
        locales.append(dict(
            tag=tag, english=english, autonym=autonym, script=scr, rtl=rtl, plural=pl,
            cats=cats, numfmt=nf, ns=ns, datefmt=df, symsrc=src, covset=cv, gaps=gaps, ncps=ncps,
            terr=terr, au=au, cov=cov, flags=flags, src=src,
            region_currency=region_cur.get(region) if region else None,
            note=extra[3] if extra else "",
        ))

    for L in locales:
        p = parent_of(L["tag"])
        L["parent"] = loc_index[p] if p in loc_index else None
    symcache = {}
    root_syms = sym_table("und")
    root_symset = symset_for([(c, v[0], v[1]) for c, v in sorted(root_syms.items()) if v != (c, c)])
    for L in locales:
        if L["symsrc"] not in symcache:
            symcache[L["symsrc"]] = sym_table(L["symsrc"])
        mine = symcache[L["symsrc"]]
        if L["parent"] is not None:
            P = locales[L["parent"]]
            if P["symsrc"] not in symcache:
                symcache[P["symsrc"]] = sym_table(P["symsrc"])
            base = symcache[P["symsrc"]]
        else:
            base = root_syms
        ents = []
        for code in tender:
            v = mine.get(code, (code, code))
            if v != base.get(code, (code, code)):
                ents.append((code, v[0], v[1]))
        L["symset"] = symset_for(ents)

    # ---- aliases and region->script hints for the resolver ----
    alias_list = []
    for k, v in sorted(aliases.items()):
        rep = v["_replacement"]
        if "-" in k or "_" in k or len(k) > 3:
            continue
        if rep in loc_index and k not in loc_index:
            alias_list.append((k, rep))
    hint_list = []
    for k, v in sorted(likely.items()):
        parts = k.split("-")
        if len(parts) != 2 or len(parts[1]) not in (2, 3) or parts[0] == "und":
            continue
        lang, reg = parts
        lscr = tag_parts(v)[1]
        if lang not in loc_index:
            continue
        if lscr == likely_script(lang):
            continue
        if (lang + "-" + lscr) in loc_index:
            hint_list.append((k, lang + "-" + lscr))

    # ---- grapheme extend ranges ----
    ext = []
    for cp in range(0x110000):
        if 0xD800 <= cp <= 0xDFFF:
            continue
        cat = unicodedata.category(chr(cp))
        if cat in ("Mn", "Me", "Mc"):
            ext.append(cp)
    ext += [0x200C, 0x200D] + list(range(0x1F3FB, 0x1F400)) + list(range(0xE0020, 0xE0080))
    ext += [0xFF9E, 0xFF9F]
    ext_ranges = ranges_of(ext)

    # ---- catalogs ----
    cat_dir = os.path.join(HERE, "catalog")
    files = sorted(f for f in os.listdir(cat_dir) if f.endswith(".msg"))
    if "en.msg" not in files:
        die("catalog/en.msg is required")

    def read_cat(path):
        ents = []
        with open(path, encoding="utf-8") as f:
            for ln_no, line in enumerate(f, 1):
                line = line.rstrip("\n")
                if not line.strip() or line.startswith("#"):
                    continue
                parts = line.split("|", 3)
                if len(parts) != 4:
                    die("%s:%d: expected ID|cat|status|text" % (path, ln_no))
                mid, cat, status, text = parts
                if cat != "-" and cat not in PLURAL_CATS:
                    die("%s:%d: bad category %s" % (path, ln_no, cat))
                if not (status in ("source", "machine") or status.startswith("cldr:")
                        or re.fullmatch(r"native:[^:]+:\d{4}-\d{2}-\d{2}", status)):
                    die("%s:%d: bad status %s" % (path, ln_no, status))
                if text != text.strip() or not text:
                    die("%s:%d: empty text or stray spaces" % (path, ln_no))
                if unicodedata.normalize("NFC", text) != text:
                    die("%s:%d: text is not NFC" % (path, ln_no))
                ents.append((mid, cat, status, text, ln_no))
        return ents

    en = read_cat(os.path.join(cat_dir, "en.msg"))
    ids = []
    plural_ids = set()
    for mid, cat, status, text, _ in en:
        if status != "source":
            die("en.msg must be status source: " + mid)
        if mid not in ids:
            ids.append(mid)
        if cat != "-":
            plural_ids.add(mid)
    for mid, _ in CLDR_MSGS:
        if mid in ids:
            die("%s is CLDR-sourced and must not be in a catalog file" % mid)
        ids.append(mid)
        plural_ids.add(mid)
    id_num = {m: i for i, m in enumerate(ids)}

    def placeholders(t):
        return sorted(set(re.findall(r"\{\d+\}", t)))

    en_ph = {}
    for mid, cat, status, text, _ in en:
        en_ph.setdefault(mid, placeholders(text))

    catalogs = {}  # tag -> list of (id, cat, status, note, text)
    STATUS = {"source": 0, "cldr": 1, "machine": 2, "native": 3}

    def add_entries(tag, ents, path):
        if tag not in loc_index:
            die("%s: catalog for %s, which is not in the locale registry" % (path, tag))
        L = locales[loc_index[tag]]
        out = catalogs.setdefault(tag, [])
        seen = {}
        for mid, cat, status, text, ln_no in ents:
            if mid not in id_num:
                die("%s:%d: unknown id %s" % (path, ln_no, mid))
            if mid in [m for m, _ in CLDR_MSGS]:
                die("%s:%d: %s is CLDR-sourced" % (path, ln_no, mid))
            if (mid in plural_ids) != (cat != "-"):
                die("%s:%d: %s plural/plain mismatch with en.msg" % (path, ln_no, mid))
            if placeholders(text) != en_ph[mid]:
                die("%s:%d: placeholders %s differ from en %s" % (path, ln_no, placeholders(text), en_ph[mid]))
            seen.setdefault(mid, set()).add(cat)
            st = status.split(":")[0]
            note = status[len(st) + 1:] if ":" in status else ""
            out.append((id_num[mid], cat, STATUS[st], note, text))
        for mid, cs in seen.items():
            if mid in plural_ids:
                need = set(L["cats"])
                if cs != need:
                    die("%s: %s has forms %s but %s needs exactly %s" % (path, mid, sorted(cs), tag, sorted(need)))

    raw = {}
    for f in files:
        tag = f[:-4]
        raw[tag] = read_cat(os.path.join(cat_dir, f))
        add_entries(tag, raw[tag], f)
    if "sa" in raw and "sa-Latn" not in raw:
        ents = [(m, c, "machine", deva_to_iast(t), n) for m, c, s, t, n in raw["sa"]]
        add_entries("sa-Latn", ents, "sa.msg (IAST transliteration)")
        # mark as transliterated
        catalogs["sa-Latn"] = [(i, c, s, "translit:sa", t) for i, c, s, _, t in catalogs["sa-Latn"]]

    # CLDR relative-time messages
    und_vals = {}
    for mid, field in CLDR_MSGS:
        und_vals[mid] = und_rel[field].get("relativeTime-type-past", {})
    for L in locales:
        if not (L["flags"] & 1):
            continue
        tag = L["tag"]
        fields = cl.main("cldr-dates-full", tag, "dateFields.json")["dates"]["fields"]
        par = locales[L["parent"]]["tag"] if L["parent"] is not None else None
        pfields = cl.main("cldr-dates-full", par, "dateFields.json")["dates"]["fields"] if par else None
        for mid, field in CLDR_MSGS:
            v = fields[field].get("relativeTime-type-past", {})
            if v == und_vals[mid]:
                continue  # only root's placeholder text: CLDR has nothing
            if pfields is not None and pfields[field].get("relativeTime-type-past", {}) == v:
                continue  # inherited unchanged from the parent
            for k, text in v.items():
                cat = k.split("-")[-1]
                catalogs.setdefault(tag, []).append(
                    (id_num[mid], cat, STATUS["cldr"], "dateFields/" + field, text))

    cat_tags = sorted(catalogs)
    for L in locales:
        L["catalog"] = cat_tags.index(L["tag"]) if L["tag"] in catalogs else None

    # ---------------------------------------------------------------------
    # emit i18n_msgid.h
    hdr = []
    hdr.append(HEADER_C)
    hdr.append("/* i18n_msgid.h — GENERATED by gen_i18n_tables.py from catalog/en.msg. Do not edit.\n"
               " *\n * These ids are the core's internal identifiers for user-facing text. The\n"
               " * swarm stores and passes ids, never translated strings; the operator's\n"
               " * language is applied at the edge by i18n_msg(). */\n")
    hdr.append("#ifndef ZXV_I18N_MSGID_H\n#define ZXV_I18N_MSGID_H\n\ntypedef enum {\n")
    for m in ids:
        hdr.append("    I18N_%s = %d,\n" % (m, id_num[m]))
    hdr.append("    I18N_MSG__COUNT = %d\n} i18n_msgid_t;\n\n#endif /* ZXV_I18N_MSGID_H */\n" % len(ids))
    with open(os.path.join(HERE, "i18n_msgid.h"), "w", encoding="utf-8") as f:
        f.write("".join(hdr))

    # ---------------------------------------------------------------------
    # emit i18n_catalog.c
    cpool = Pool()
    out = [HEADER_C,
           "/* i18n_catalog.c — GENERATED by gen_i18n_tables.py from the catalog/ .msg files and CLDR %s.\n"
           " * Do not edit; edit the .msg files and regenerate. */\n" % CLDR_VERSION,
           '#include "i18n_internal.h"\n\n']
    ent_rows = []
    cat_rows = []
    for tag in cat_tags:
        ents = sorted(catalogs[tag], key=lambda e: (e[0], PLURAL_CATS.index(e[1]) if e[1] != "-" else -1))
        start = len(ent_rows)
        for (i, c, s, note, text) in ents:
            cc = 255 if c == "-" else PLURAL_CATS.index(c)
            ent_rows.append("    {%d, %d, %d, %d, %d},\n" % (i, cc, s, cpool.add(note), cpool.add(text)))
        cat_rows.append("    {%d, %d, %d},\n" % (cpool.add(tag), start, len(ents)))
    out.append(emit_pool(cpool, "CPOOL"))
    out.append("\nconst char *const i18n_cpool = CPOOL;\n\n")
    out.append("const i18n_entry_t i18n_entries[%d] = {\n" % len(ent_rows) + "".join(ent_rows) + "};\n\n")
    out.append("const i18n_catalog_t i18n_catalogs[%d] = {\n" % len(cat_rows) + "".join(cat_rows) + "};\n")
    out.append("const uint32_t i18n_catalog_count = %d;\n\n" % len(cat_rows))
    out.append("static const char *const KEYS[%d] = {\n" % len(ids))
    for m in ids:
        out.append('    "%s",\n' % m)
    out.append("};\nconst char *const *const i18n_msg_keys = KEYS;\n")
    with open(os.path.join(HERE, "i18n_catalog.c"), "w", encoding="utf-8") as f:
        f.write("".join(out))

    # ---------------------------------------------------------------------
    # emit i18n_tables.c
    o = [HEADER_C,
         "/* i18n_tables.c — GENERATED by gen_i18n_tables.py from Unicode CLDR %s (cldr-json)\n"
         " * and Python unicodedata %s. Do not edit. CLDR data: (c) Unicode, Inc.,\n"
         " * Unicode License v3. */\n" % (CLDR_VERSION, unicodedata.unidata_version),
         '#include "i18n_internal.h"\n\n',
         'const char i18n_cldr_version[] = "%s";\n' % CLDR_VERSION,
         'const char i18n_unicode_version[] = "%s";\n\n' % unicodedata.unidata_version]

    loc_rows = []
    for L in locales:
        def idx(v):
            return 0xFFFF if v is None else v
        loc_rows.append("    {%d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d},\n" % (
            pool.add(L["tag"]), pool.add(L["english"]), pool.add(L["autonym"]), pool.add(L["script"]),
            pool.add(" ".join(L["terr"])), pool.add(" ".join(L["au"])), pool.add(L["region_currency"] or ""),
            idx(L["parent"]), idx(L["catalog"]), L["plural"], L["numfmt"], L["datefmt"], L["symset"],
            idx(L["covset"]), 1 if L["rtl"] else 0,
            {"modern": 3, "moderate": 2, "basic": 1}.get(L["cov"], 0), L["flags"]))
    nf_rows = []
    for rec in numfmts:
        (ns, di, dec, grp, minus, plus, pct, mg, gd, gp, gc, ga, da, pa, ca, caa, ib, ia) = rec
        offs = [pool.add(x) for x in (ns, dec, grp, minus, plus, pct, ib, ia)]
        affs = [pool.add(x) for a in (da, pa, ca, caa) for x in a]
        nf_rows.append("    {%s, {%s}, %d, %d, {%d, %d, %d, %d}},\n" % (
            ", ".join(str(x) for x in offs), ", ".join(str(x) for x in affs), di, mg, gd, gp, gc, ga))
    df_rows = []
    for (pats, ma, mw, msa, msw, da, dw, era, hn) in datefmts:
        df_rows.append("    {{%s}, %d, %d, %d, %d, %d, %d, %d},\n" % (
            ", ".join(str(p) for p in pats), ma, mw, msa, msw, da, dw, era))
    o.append(emit_pool(pool, "POOL"))
    o.append("\nconst char *const i18n_pool = POOL;\n\n")
    o.append("const i18n_loc_t i18n_locs[%d] = {\n" % len(loc_rows) + "".join(loc_rows) + "};\n")
    o.append("const uint32_t i18n_loc_count = %d;\n\n" % len(loc_rows))
    o.append("const i18n_numfmt_t i18n_numfmts[%d] = {\n" % len(nf_rows) + "".join(nf_rows) + "};\n\n")
    o.append("const uint32_t i18n_digitsets[%d][10] = {\n" % len(digitsets))
    for ns, cps in digitsets:
        o.append("    {%s}, /* %s */\n" % (", ".join("0x%X" % c for c in cps), ns))
    o.append("};\n\n")
    o.append("const i18n_datefmt_t i18n_datefmts[%d] = {\n" % len(df_rows) + "".join(df_rows) + "};\n\n")
    o.append("const uint32_t i18n_monthsets[%d][12] = {\n" % len(monthsets))
    for ms in monthsets:
        o.append("    {%s},\n" % ", ".join(str(x) for x in ms))
    o.append("};\n\nconst uint32_t i18n_daysets[%d][7] = {\n" % len(daysets))
    for ds in daysets:
        o.append("    {%s},\n" % ", ".join(str(x) for x in ds))
    o.append("};\n\n")
    # plural tables
    prow, rrow, grow = [], [], []
    for cats, parsed in plural_sets:
        cat_codes = [PLURAL_CATS.index(c) for c in cats]
        firsts, counts = [], []
        for rule in parsed:
            firsts.append(len(rrow))
            n = 0
            for oi, ands in enumerate(rule):
                for ai, (op, mod, neg, rngs) in enumerate(ands):
                    and_next = 1 if ai + 1 < len(ands) else 0
                    rrow.append("    {%d, %d, %d, %d, %d, %d},\n" % (op, 1 if neg else 0, and_next, len(rngs), len(grow), mod))
                    for lo, hi in rngs:
                        grow.append("    {%dU, %dU},\n" % (lo, hi))
                    n += 1
            counts.append(n)
        while len(cat_codes) < 5:
            cat_codes.append(255)
            firsts.append(0)
            counts.append(0)
        prow.append("    {%d, {%s}, {%s}, {%s}},\n" % (
            len(cats), ", ".join(map(str, cat_codes)), ", ".join(map(str, firsts)), ", ".join(map(str, counts))))
    o.append("const i18n_plural_rules_t i18n_plurals[%d] = {\n" % len(prow) + "".join(prow) + "};\n\n")
    o.append("const i18n_plural_rel_t i18n_plural_rels[%d] = {\n" % max(1, len(rrow)) + ("".join(rrow) or "    {0, 0, 0, 0, 0, 0},\n") + "};\n\n")
    o.append("const i18n_range_t i18n_plural_ranges[%d] = {\n" % max(1, len(grow)) + ("".join(grow) or "    {0, 0},\n") + "};\n\n")
    # currency symbols
    o.append("const uint16_t i18n_root_symset = %d;\n\n" % root_symset)
    o.append("const i18n_symset_t i18n_symsets[%d] = {\n" % len(symsets))
    for s, n in symsets:
        o.append("    {%d, %d},\n" % (s, n))
    o.append("};\n\nconst i18n_sym_t i18n_syms[%d] = {\n" % max(1, len(syms)))
    for code, so, no, fl in syms:
        o.append('    {"%s", %d, %d, %d},\n' % (code, so, no, fl))
    o.append("};\n\n")
    o.append("const i18n_curdigits_t i18n_curdigits[%d] = {\n" % len(cur_digits))
    for code in sorted(cur_digits):
        o.append('    {"%s", %d},\n' % (code, cur_digits[code]))
    o.append("};\nconst uint32_t i18n_curdigits_count = %d;\n\n" % len(cur_digits))
    # resolver tables
    o.append("const i18n_alias_t i18n_aliases[%d] = {\n" % len(alias_list))
    for k, v in alias_list:
        o.append('    {"%s", %d},\n' % (k, loc_index[v]))
    o.append("};\nconst uint32_t i18n_alias_count = %d;\n\n" % len(alias_list))
    o.append("const i18n_hint_t i18n_hints[%d] = {\n" % len(hint_list))
    for k, v in hint_list:
        o.append('    {"%s", %d},\n' % (k, loc_index[v]))
    o.append("};\nconst uint32_t i18n_hint_count = %d;\n\n" % len(hint_list))
    # grapheme extend
    o.append("const i18n_range_t i18n_extend[%d] = {\n" % len(ext_ranges))
    for lo, hi in ext_ranges:
        o.append("    {0x%X, 0x%X},\n" % (lo, hi))
    o.append("};\nconst uint32_t i18n_extend_count = %d;\n\n" % len(ext_ranges))
    # coverage
    o.append("const i18n_covset_t i18n_covsets[%d] = {\n" % len(covsets))
    for s, n, g, c in covsets:
        o.append("    {%d, %d, %d, %d},\n" % (s, n, g, c))
    o.append("};\n\nconst i18n_range_t i18n_cov_ranges[%d] = {\n" % len(cov_ranges))
    for lo, hi in cov_ranges:
        o.append("    {0x%X, 0x%X},\n" % (lo, hi))
    o.append("};\n")
    with open(os.path.join(HERE, "i18n_tables.c"), "w", encoding="utf-8") as f:
        f.write("".join(o))

    sys.stderr.write("locales %d, numfmts %d, datefmts %d, plural sets %d, symsets %d (%d syms), "
                     "covsets %d, pool %d bytes, catalogs %d (%d entries, cpool %d bytes), ids %d\n" % (
                         len(locales), len(numfmts), len(datefmts), len(plural_sets), len(symsets),
                         len(syms), len(covsets), len(pool.data), len(cat_tags), len(ent_rows),
                         len(cpool.data), len(ids)))

    if docs:
        write_docs(docs, locales, catalogs, ids, plural_ids, cat_tags)


# ---------------------------------------------------------------------------

def write_docs(path, locales, catalogs, ids, plural_ids, cat_tags):
    from collections import Counter
    n_hand = len([i for i in ids if i not in [m for m, _ in CLDR_MSGS]])
    rows = []
    rows.append("| Tag | Language | Autonym | Script | Dir | CLDR | Plurals | Catalog (hand ids / %d) | Review | Font gaps | AU states |" % n_hand)
    rows.append("|---|---|---|---|---|---|---|---|---|---|---|")
    by_tag = {L["tag"]: L for L in locales}
    for L in locales:
        tag = L["tag"]
        # one row per locale that is a base language, has a script subtag, has
        # a catalog, or is used in an AU member state
        lang, script, region = tag_parts(tag)
        if region and L["catalog"] is None and not L["au"]:
            continue
        ents = catalogs.get(tag, [])
        hand = set(i for i, c, s, n, t in ents if s != 1)
        st = Counter(s for i, c, s, n, t in ents if s != 1)
        review = []
        if st.get(0):
            review.append("source")
        if st.get(2):
            review.append("machine, needs native review")
        if st.get(3):
            review.append("native-reviewed")
        if any(s == 1 for i, c, s, n, t in ents):
            review.append("CLDR time strings")
        cov = {3: "modern", 2: "moderate", 1: "basic"}.get(
            {"modern": 3, "moderate": 2, "basic": 1}.get(L["cov"], 0), "none")
        if L["flags"] & 16:
            cov = "derived (transliterated from CLDR sa)"
        elif not (L["flags"] & 1):
            cov = "none (root formats)"
        pl = ",".join(L["cats"]) if L["flags"] & 4 else "other only (no CLDR rules)"
        gaps = "%d of %d" % (L["gaps"], L["ncps"]) if L["ncps"] else "n/a"
        rows.append("| `%s` | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s |" % (
            tag, L["english"].replace("|", "/"), L["autonym"].replace("|", "/") + ("" if L["flags"] & 2 else " (unverified)"),
            L["script"], "RTL" if L["rtl"] else "LTR", cov, pl,
            str(len(hand)) if hand else "-", "; ".join(review) or "-", gaps, " ".join(L["au"]) or "-"))
    _ = by_tag
    text = "\n".join(rows) + "\n"
    with open(path, encoding="utf-8") as f:
        doc = f.read()
    a = "<!-- BEGIN GENERATED LOCALE TABLE -->\n"
    b = "<!-- END GENERATED LOCALE TABLE -->"
    if a not in doc or b not in doc:
        die("docs file lacks the generated-table markers")
    pre = doc.split(a)[0]
    post = doc.split(b)[1]
    with open(path, "w", encoding="utf-8") as f:
        f.write(pre + a + text + b + post)


HEADER_C = ("/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */\n"
            "/* SPDX-License-Identifier: Apache-2.0 */\n")

if __name__ == "__main__":
    main()
