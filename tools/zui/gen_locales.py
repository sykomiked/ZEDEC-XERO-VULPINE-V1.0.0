#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Generate zui's locale tables from Unicode CLDR and the reader's words.

    python3 tools/zui/gen_locales.py [--cldr DIR]

Inputs:
  tools/zui/locales/LANGUAGES.tsv   which locales, in table order
  tools/zui/locales/words/<tag>.json the screen reader's words per locale,
                                     with their review state
  CLDR 48.2.0 JSON (the npm packages cldr-core, cldr-numbers-full,
  cldr-dates-full, cldr-cal-persian-full, cldr-cal-buddhist-full,
  cldr-localenames-full, cldr-misc-full). --cldr names a folder holding one
  unpacked folder per package; without it they are downloaded from the npm
  registry into ~/.cache/zxv-cldr-48.2.0.

Outputs (both committed, both "do not edit"):
  kernel/src/zui/zui_locale_data.inc     words, counts, plural rules, table
  kernel/src/zui/zui_locale_samples.inc  CLDR plural samples for the test

The CLDR data is under the Unicode License v3.
"""
import argparse
import io
import json
import os
import re
import sys
import tarfile
import urllib.request

CLDR_VERSION = "48.2.0"
PACKAGES = ["cldr-core", "cldr-numbers-full", "cldr-dates-full", "cldr-cal-persian-full",
            "cldr-cal-buddhist-full", "cldr-localenames-full", "cldr-misc-full"]
ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
LOC_DIR = os.path.join(ROOT, "tools", "zui", "locales")
OUT_DATA = os.path.join(ROOT, "kernel", "src", "zui", "zui_locale_data.inc")
OUT_SAMPLES = os.path.join(ROOT, "kernel", "src", "zui", "zui_locale_samples.inc")

WORD_KEYS = [
    "group", "button", "list", "readonly", "textarea", "textfield", "scroll", "ai", "progress",
    "image", "separator", "heading", "checkbox", "region", "error", "menu", "menuitem", "dialog",
    "option", "secure", "empty", "busy", "disabled", "checked", "not_checked", "partly_checked",
    "selected", "dimmed", "blank", "pending", "accepted", "rejected", "ai_accepted",
    "ai_rejected", "reader_on", "reader_off", "nothing", "end", "top", "no_next_heading",
    "no_prev_heading", "no_next_landmark", "no_prev_landmark", "no_next_ai", "no_prev_ai",
    "no_focus", "nothing_to_activate", "not_a_control", "no_actions", "accept_hint",
    "lm_banner", "lm_navigation", "lm_main", "lm_search", "lm_complementary", "lm_status",
    "verb_low", "verb_medium", "verb_high", "error_prefix", "shortcut", "from", "by", "about",
    "heading_level", "position"]
NOUNS = ["item", "character", "token", "percent"]
CATS = ["zero", "one", "two", "few", "many", "other"]
REVIEW = {"source": "ZUI_REVIEW_SOURCE", "unreviewed": "ZUI_REVIEW_MACHINE",
          "reviewed": "ZUI_REVIEW_NATIVE"}
# Scripts written right to left, for a locale CLDR has no layout for.
RTL_SCRIPTS = {"Arab", "Hebr", "Syrc", "Thaa", "Nkoo", "Adlm", "Rohg", "Mand", "Samr", "Armi",
               "Phnx", "Nbat", "Palm", "Hatr", "Elym", "Prti", "Phli", "Phlp", "Avst", "Sarb",
               "Narb", "Mani", "Chrs", "Khar", "Lydi", "Mero", "Merc", "Sogd", "Sogo", "Hung",
               "Orkh", "Cprt", "Yezi", "Mend"}
# Plural rules for languages CLDR gives none, so their counts can agree with
# the number. Not CLDR data: Latin's singular, Sanskrit's singular and dual.
PLURALS = {"la": {"pluralRule-count-one": "n = 1 @integer 1",
                  "pluralRule-count-other": " @integer 0, 2~16, 100, 1000"},
           "sa": {"pluralRule-count-one": "n = 1 @integer 1",
                  "pluralRule-count-two": "n = 2 @integer 2",
                  "pluralRule-count-other": " @integer 0, 3~17, 100, 1000"}}

# Choices CLDR does not make for us. The list separator joins the parts of a
# spoken announcement (clauses, not an enumeration), so it is the script's
# comma rather than CLDR's list pattern where those differ.
LIST_SEP = {"zh-Hans": "，", "zh-Hant": "，", "yue": "，", "ja": "、", "th": " ",
            "my": "၊ ", "nqo": "߸ ", "lzh": "，"}
ARABIC_COMMA_SCRIPTS = {"Arab", "Thaa"}
ETHIOPIC_COMMA = "፣ "
# Own names where CLDR has none or this project already shipped another.
NAMES = {"id": "Bahasa Indonesia", "dv": "ދިވެހިބަސް", "iu": "ᐃᓄᒃᑎᑐᑦ", "la": "Lingua Latina",
         "grc": "Ἀρχαία Ἑλληνική", "ang": "Ænglisc", "lzh": "文言", "ota": "لسان عثمانی",
         "pi": "Pāli", "cu": "Церковнословѣнскїй"}
# Legacy and macro-language tags that name a locale here.
ALIASES = [("iw", "he"), ("in", "id"), ("tl", "fil"), ("no", "nb"), ("mo", "ro"),
           ("sh", "sr"), ("ji", ""), ("syc", "syr")]


def fetch(cldr):
    if cldr:
        return cldr
    cache = os.path.expanduser(f"~/.cache/zxv-cldr-{CLDR_VERSION}")
    for p in PACKAGES:
        dst = os.path.join(cache, p)
        if os.path.isdir(dst):
            continue
        url = f"https://registry.npmjs.org/{p}/-/{p}-{CLDR_VERSION}.tgz"
        print("fetching", url, file=sys.stderr)
        data = urllib.request.urlopen(url).read()
        with tarfile.open(fileobj=io.BytesIO(data)) as t:
            for m in t.getmembers():
                if not m.isfile() or ".." in m.name:
                    continue
                rel = m.name.split("/", 1)[1]
                os.makedirs(os.path.join(dst, os.path.dirname(rel)), exist_ok=True)
                with open(os.path.join(dst, rel), "wb") as f:
                    f.write(t.extractfile(m).read())
    return cache


def load(cldr, *parts):
    with open(os.path.join(cldr, *parts), encoding="utf-8") as f:
        return json.load(f)


def c_str(s):
    out = []
    for ch in s:
        o = ord(ch)
        if ch in '"\\':
            out.append("\\" + ch)
        elif o < 0x20 or o in (0x00A0, 0x061C, 0x200E, 0x200F, 0x202F, 0x2212):
            out.append(f"\\u{o:04X}")
        else:
            out.append(ch)
    s = "".join(out)
    # a hex-looking character after \uXXXX would be read as part of it only for
    # \x escapes; \u takes exactly four digits, so nothing to split
    return '"' + s + '"'


# ---- plural rules: CLDR syntax to C, for integers (v = 0, so f = t = w = e = c = 0) ----

def rule_to_c(rule):
    rule = rule.split("@")[0].strip()
    if not rule:
        return "1"
    ors = []
    for alt in rule.split(" or "):
        ands = []
        for cond in alt.split(" and "):
            m = re.fullmatch(r"\s*([niftvwec])\s*(?:%\s*(\d+))?\s*(!=|=)\s*([\d.,]+)\s*", cond)
            if not m:
                raise ValueError("plural rule: " + cond)
            op, mod, rel, ranges = m.groups()
            var = "i" if op in "ni" else "0"
            if mod:
                var = f"({var} % {mod}u)" if var != "0" else "0"
            parts = []
            for r in ranges.split(","):
                if ".." in r:
                    a, b = r.split("..")
                    lo = f"{var} >= {a}u && " if int(a) > 0 else ""
                    parts.append(f"({lo}{var} <= {b}u)")
                else:
                    parts.append(f"{var} == {r}u")
            e = " || ".join(parts)
            if rel == "!=":
                e = f"!({e})"
            elif len(parts) > 1:
                e = f"({e})"
            ands.append(e)
        ors.append(" && ".join(f"({a})" for a in ands) if len(ands) > 1 else ands[0])
    return " || ".join(f"({o})" for o in ors) if len(ors) > 1 else ors[0]


def integer_samples(rule):
    m = re.search(r"@integer([^@]*)", rule)
    if not m:
        return []
    out = []
    for tok in m.group(1).replace("…", "").split(","):
        tok = tok.strip()
        if not tok:
            continue
        if "~" in tok:
            a, b = tok.split("~")
            a, b = int(a), int(b)
            out += [a, b] if b - a > 4 else list(range(a, b + 1))
        elif re.fullmatch(r"\d+", tok):
            out.append(int(tok))
        elif re.fullmatch(r"\d+c\d+", tok):
            base, exp = tok.split("c")
            out.append(int(base) * 10 ** int(exp))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cldr")
    args = ap.parse_args()
    cldr = fetch(args.cldr)

    langs = []
    with open(os.path.join(LOC_DIR, "LANGUAGES.tsv"), encoding="utf-8") as f:
        for line in f:
            if line.startswith("#") or not line.strip():
                continue
            cols = line.rstrip("\n").split("\t")
            cols += [""] * (8 - len(cols))
            langs.append({"tag": cols[0], "english": cols[1], "script": cols[2],
                          "group": cols[3], "data": cols[5], "ui": cols[6] or "full",
                          "bridge": cols[7]})

    plurals = load(cldr, "cldr-core", "supplemental", "plurals.json")["supplemental"][
        "plurals-type-cardinal"]
    likely = load(cldr, "cldr-core", "supplemental", "likelySubtags.json")["supplemental"][
        "likelySubtags"]
    week = load(cldr, "cldr-core", "supplemental", "weekData.json")["supplemental"]["weekData"]
    calpref = load(cldr, "cldr-core", "supplemental", "calendarPreferenceData.json")[
        "supplemental"]["calendarPreferenceData"]
    numsystems = load(cldr, "cldr-core", "supplemental", "numberingSystems.json")[
        "supplemental"]["numberingSystems"]
    days = {"sun": 0, "mon": 1, "tue": 2, "wed": 3, "thu": 4, "fri": 5, "sat": 6}

    rules = [("other", {})]  # (key, CLDR rule dict), index = rule id
    systems = ["latn"]
    rows, words_c, samples = [], [], []
    en_words = None

    def main_file(pkg, name, chain):
        """The first locale of chain with this CLDR file, and the file."""
        for t in chain:
            path = os.path.join(cldr, pkg, "main", t, name)
            if os.path.exists(path):
                return t, load(cldr, pkg, "main", t, name)["main"][t]
        raise SystemExit(f"{chain[0]}: no {pkg} {name}")

    for L in langs:
        tag = L["tag"]
        base = tag.split("-")[0]
        script_only = L["ui"] == "script"
        if L["ui"] not in ("full", "script"):
            raise SystemExit(f"{tag}: ui is 'script' or empty")
        chain = [t for t in (tag, L["data"], "und") if t]
        sub = likely.get(tag) or likely.get(base) or likely.get(L["data"] or "und")
        region = sub.split("-")[-1] if re.fullmatch(r"[A-Z]{2}|\d{3}", sub.split("-")[-1]) else "001"
        # plural rule (the English rule for English words)
        pr = (plurals["en"] if script_only else
              PLURALS.get(tag) or plurals.get(tag) or plurals.get(base) or
              plurals.get(L["data"]) or plurals.get(L["data"].split("-")[0]) or
              {"pluralRule-count-other": ""})
        key = json.dumps({k: v.split("@")[0].strip() for k, v in pr.items()}, sort_keys=True)
        ids = [r[0] for r in rules]
        if len(pr) == 1:
            rid = 0
        elif key in ids:
            rid = ids.index(key)
        else:
            rules.append((key, pr))
            rid = len(rules) - 1
        for k, v in pr.items() if not script_only else ():
            cat = k.split("-")[-1]
            for n in integer_samples(v):
                samples.append((tag, n, cat))
        # numbers
        num = main_file("cldr-numbers-full", "numbers.json", chain)[1]["numbers"]
        dflt = num["defaultNumberingSystem"]
        native = num["otherNumberingSystems"].get("native", dflt)
        if numsystems[native]["_type"] != "numeric":  # Ge'ez numerals are not positional
            native = dflt
        pat = num[f"decimalFormats-numberSystem-{dflt}"]["standard"].split(";")[0]
        ip = pat.split(".")[0]
        groups = ip.split(",")
        g1 = len(groups[-1]) if len(groups) > 1 else 0
        g2 = len(groups[-2]) if len(groups) > 2 else g1
        ming = int(num.get("minimumGroupingDigits", "1"))

        def sym(sysname):
            s = num.get(f"symbols-numberSystem-{sysname}") or num[f"symbols-numberSystem-{dflt}"]
            if sysname not in systems:
                systems.append(sysname)
            return (sysname, s["decimal"], s["group"], s["minusSign"])
        nums = [sym(dflt), sym(native)]
        # calendar and dates
        cal = "gregorian"
        pref = calpref.get(region, calpref["001"])
        for c in ((pref.split() if isinstance(pref, str) else pref) if not script_only else []):
            if c in ("gregorian", "persian", "buddhist"):
                cal = c
                break
        dtag, greg = main_file("cldr-dates-full", "ca-gregorian.json", chain)
        greg = greg["dates"]["calendars"]["gregorian"]
        if cal == "gregorian":
            dcal = greg
        else:
            pkg = f"cldr-cal-{cal}-full"
            dcal = load(cldr, pkg, "main", dtag, f"ca-{cal}.json")["main"][dtag]["dates"][
                "calendars"][cal]
        def pattern(p):
            return p["_value"] if isinstance(p, dict) else p
        date = pattern(dcal["dateFormats"]["short"])
        time = pattern(greg["timeFormats"]["short"])
        dp = greg["dayPeriods"]["format"]["abbreviated"]
        am, pm = dp["am"], dp["pm"]
        # names, direction, list separator
        try:
            names = load(cldr, "cldr-localenames-full", "main", tag, "languages.json")["main"][
                tag]["localeDisplayNames"]["languages"]
        except FileNotFoundError:
            names = {}
        name = NAMES.get(tag) or names.get(tag) or names.get(base)
        if script_only and not NAMES.get(tag):
            name = L["english"]  # the words are English, so is the name
        if tag == "zh-Hans":
            name = "简体中文"
        elif tag == "zh-Hant":
            name = "繁體中文"
        if not name:
            raise SystemExit(f"{tag}: no name")
        if os.path.exists(os.path.join(cldr, "cldr-misc-full", "main", tag, "layout.json")):
            layout = load(cldr, "cldr-misc-full", "main", tag, "layout.json")["main"][tag]["layout"]
            rtl = layout["orientation"]["characterOrder"] == "right-to-left"
        else:
            rtl = L["script"] in RTL_SCRIPTS
        if script_only:
            sep = ", "
        elif tag in LIST_SEP:
            sep = LIST_SEP[tag]
        elif L["script"] in ARABIC_COMMA_SCRIPTS:
            sep = "، "
        elif L["script"] == "Ethi":
            sep = ETHIOPIC_COMMA
        else:
            sep = ", "
        first = days[week["firstDay"].get(region, week["firstDay"]["001"])]
        # words
        if script_only:
            if os.path.exists(os.path.join(LOC_DIR, "words", f"{tag}.json")):
                raise SystemExit(f"{tag}: has words, so its ui is not 'script'")
            rows.append(dict(tag=tag, name=name, rtl=rtl, rule=rid, first=days[week["firstDay"].get(
                region, week["firstDay"]["001"])], cal=cal, g1=g1, g2=g2, ming=ming, nums=nums,
                date=date, time=time, am=am, pm=pm, sep=sep, cid="en", script=L["script"],
                region=region, review="ZUI_REVIEW_UNTRANSLATED", bridge=L["bridge"] or "en"))
            continue
        with open(os.path.join(LOC_DIR, "words", f"{tag}.json"), encoding="utf-8") as f:
            w = json.load(f)
        if list(w["words"]) != WORD_KEYS:
            raise SystemExit(f"{tag}.json: the words must be the {len(WORD_KEYS)} keys of en.json, in order")
        if tag == "en":
            en_words = w["words"]
        cats = {k.split("-")[-1] for k in pr}
        for noun in NOUNS:
            have = set(w["counts"][noun])
            if "other" not in have or not have <= set(CATS):
                raise SystemExit(f"{tag}.json: counts.{noun} needs 'other'")
            extra = have - cats
            if extra and tag != "en":
                raise SystemExit(f"{tag}.json: counts.{noun} has {sorted(extra)}, CLDR has {sorted(cats)}")
        cid = tag.replace("-", "_")
        words_c.append((cid, w))
        rows.append(dict(tag=tag, name=name, rtl=rtl, rule=rid, first=first, cal=cal,
                         g1=g1, g2=g2, ming=ming, nums=nums, date=date, time=time, am=am,
                         pm=pm, sep=sep, cid=cid, script=L["script"], region=region,
                         review=REVIEW[w["review"]], bridge=tag))

    index = {r["tag"]: i for i, r in enumerate(rows)}
    for r in rows:
        b = rows[index[r["bridge"]]] if r["bridge"] in index else None
        if not b or b["review"] == "ZUI_REVIEW_UNTRANSLATED":
            raise SystemExit(f"{r['tag']}: bridge {r['bridge']} is not a translated locale here")
        r["bridge_i"] = index[r["bridge"]]

    # ---- write zui_locale_data.inc ----
    o = []
    o.append("/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */")
    o.append("/* SPDX-License-Identifier: Apache-2.0 */")
    o.append("/* Generated by tools/zui/gen_locales.py from Unicode CLDR %s (Unicode License v3)" % CLDR_VERSION)
    o.append(" * and the files in tools/zui/locales/words. Do not edit; edit those and re-run. */")
    o.append("")
    o.append("// clang-format off")
    o.append("enum {")
    for i, s in enumerate(systems):
        o.append(f"    SYS_{s.upper()} = {i},")
    o.append(f"    SYS_COUNT = {len(systems)}")
    o.append("};")
    o.append("/* the zero digit of each decimal numbering system (hanidec is not contiguous) */")
    o.append("static const uint32_t SYS_ZERO[SYS_COUNT] = {")
    for s in systems:
        z = 0 if s == "hanidec" else ord(numsystems[s]["_digits"][0])
        o.append(f"    0x{z:04X}u, /* {s} */")
    o.append("};")
    o.append("")
    o.append("/* CLDR cardinal plural rules for integers (v = 0): one function per rule set */")
    o.append(f"#define ZUI_PLURAL_RULES {len(rules)}")
    o.append("static zui_plural_t plural_rule(uint8_t rule, uint64_t i)")
    o.append("{")
    o.append("    switch (rule) {")
    for rid, (key, pr) in enumerate(rules):
        if rid == 0:
            continue
        users = " ".join(r["tag"] for r in rows if r["rule"] == rid)
        o.append(f"    case {rid}: /* {users} */")
        for cat in CATS[:-1]:
            k = f"pluralRule-count-{cat}"
            if k in pr:
                o.append(f"        if ({rule_to_c(pr[k])}) return ZUI_PLURAL_{cat.upper()};")
        o.append("        return ZUI_PLURAL_OTHER;")
    o.append("    default:")
    o.append("        return ZUI_PLURAL_OTHER;")
    o.append("    }")
    o.append("}")
    o.append("")
    for cid, w in words_c:
        o.append(f"static const char *const W_{cid}[ZUI_WORD_COUNT] = {{")
        for k in WORD_KEYS:
            o.append(f"    {c_str(w['words'][k])},")
        o.append("};")
        o.append(f"static const char *const C_{cid}[ZUI_COUNT_NOUNS][ZUI_PLURAL_COUNT] = {{")
        for noun in NOUNS:
            forms = w["counts"][noun]
            o.append("    {" + ", ".join(c_str(forms[c]) if c in forms else "0" for c in CATS) + "},")
        o.append("};")
    o.append("")
    o.append(f"#define ZUI_LOCALE_COUNT {len(rows)}")
    o.append("static const struct zui_locale LOCALES[ZUI_LOCALE_COUNT] = {")
    calname = {"gregorian": "CAL_GREGORIAN", "persian": "CAL_PERSIAN", "buddhist": "CAL_BUDDHIST"}
    for r in rows:
        ns = ", ".join("{SYS_%s, %s, %s, %s}" % (n[0].upper(), c_str(n[1]), c_str(n[2]), c_str(n[3]))
                       for n in r["nums"])
        o.append(f"    {{{c_str(r['tag'])}, {c_str(r['name'])}, \"{r['script']}\", {int(r['rtl'])}, {r['rule']}, {r['first']}, "
                 f"{calname[r['cal']]}, {r['g1']}, {r['g2']}, {r['ming']}, {r['review']}, {r['bridge_i']},")
        o.append(f"     {{{ns}}},")
        o.append(f"     {c_str(r['date'])}, {c_str(r['time'])}, {c_str(r['am'])}, {c_str(r['pm'])}, "
                 f"{c_str(r['sep'])}, W_{r['cid']}, C_{r['cid']}}}, /* region {r['region']} */")
    o.append("};")
    o.append("")
    o.append("/* legacy tags: \"\" means no locale here (falls to en) */")
    o.append("static const char *const ALIASES[][2] = {")
    for a, b in ALIASES:
        o.append(f"    {{\"{a}\", \"{b}\"}},")
    o.append("};")
    o.append("// clang-format on")
    with open(OUT_DATA, "w", encoding="utf-8") as f:
        f.write("\n".join(o) + "\n")

    # ---- write zui_locale_samples.inc ----
    s = ["/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */",
         "/* SPDX-License-Identifier: Apache-2.0 */",
         f"/* Generated by tools/zui/gen_locales.py: the @integer samples of every CLDR {CLDR_VERSION}",
         " * cardinal plural rule, per locale. Do not edit. */",
         "// clang-format off",
         "static const struct { const char *tag; uint64_t n; zui_plural_t want; } PLURAL_SAMPLES[] = {"]
    for tag, n, cat in samples:
        s.append(f"    {{\"{tag}\", {n}u, ZUI_PLURAL_{cat.upper()}}},")
    s.append("};")
    s.append("// clang-format on")
    with open(OUT_SAMPLES, "w", encoding="utf-8") as f:
        f.write("\n".join(s) + "\n")
    print(f"{len(rows)} locales, {len(rules)} plural rules, {len(systems)} numbering systems, "
          f"{len(samples)} plural samples")


if __name__ == "__main__":
    main()
