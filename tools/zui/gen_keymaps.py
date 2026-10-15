#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Generate zui keyboard layouts from xkeyboard-config.

    python3 tools/zui/gen_keymaps.py

Reads tools/zui/locales/KEYBOARDS.tsv (which layout each locale types with,
and which xkeyboard-config layout and variant a new zui layout comes from),
compiles each of those layouts with libxkbcommon (via ctypes; the xkb-data
and libxkbcommon packages), reads what every character key types at its
four levels (base, Shift, AltGr, AltGr+Shift), and writes them in
zui_keymap.h's text format to kernel/src/zui/zui_keymap_xkb.inc, with the
locale -> layout table.

xkeyboard-config is under the MIT/X11 licence; the layouts it describes are
national and community standards (InScript, KaGaPa, Probhat, ...).
"""
import ctypes
import ctypes.util
import os
import sys
import unicodedata
import xml.etree.ElementTree as ET

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
TSV = os.path.join(ROOT, "tools", "zui", "locales", "KEYBOARDS.tsv")
OUT = os.path.join(ROOT, "kernel", "src", "zui", "zui_keymap_xkb.inc")
EVDEV_XML = "/usr/share/X11/xkb/rules/evdev.xml"

# zui positions (zui_keymap.h K1) and the evdev codes that report them
ROWS = {
    "E": [41] + list(range(2, 14)),          # E00 ` .. E12 =
    "D": [None] + list(range(16, 28)) + [43],  # D01 q .. D12 ], D13 backslash
    "C": [None] + list(range(30, 41)),       # C01 a .. C11 ' (C12 is D13's code)
    "B": [86] + list(range(44, 54)),         # B00 (ISO) .. B10 /
}
LEVELS = ["base", "shift", "altgr", "altgr+shift"]
# dead keysyms the zui standard tables know, by xkb name
STD_DEAD = {"dead_grave": "grave", "dead_acute": "acute", "dead_circumflex": "circumflex",
            "dead_tilde": "tilde", "dead_diaeresis": "diaeresis", "dead_abovering": "ring",
            "dead_cedilla": "cedilla", "dead_caron": "caron", "dead_macron": "macron"}
# other dead keysyms: the combining mark they add
DEAD_MARK = {"dead_belowdot": 0x0323, "dead_hook": 0x0309, "dead_horn": 0x031B,
             "dead_ogonek": 0x0328, "dead_abovedot": 0x0307, "dead_doubleacute": 0x030B,
             "dead_breve": 0x0306, "dead_stroke": None, "dead_belowmacron": 0x0331,
             "dead_belowcomma": 0x0326, "dead_invertedbreve": 0x0311,
             "dead_doublegrave": 0x030F, "dead_belowcircumflex": 0x032D,
             "dead_belowtilde": 0x0330, "dead_belowbreve": 0x032E,
             "dead_belowdiaeresis": 0x0324, "dead_belowring": 0x0325, "dead_iota": 0x0345,
             "dead_greek": None, "dead_currency": None, "dead_semivoiced_sound": 0x309A,
             "dead_voiced_sound": 0x3099, "dead_psili": 0x0313, "dead_dasia": 0x0314,
             "dead_hamza": None, "dead_A": None}


class Names(ctypes.Structure):
    _fields_ = [("rules", ctypes.c_char_p), ("model", ctypes.c_char_p),
                ("layout", ctypes.c_char_p), ("variant", ctypes.c_char_p),
                ("options", ctypes.c_char_p)]


def xkb_lib():
    path = ctypes.util.find_library("xkbcommon") or "libxkbcommon.so.0"
    x = ctypes.CDLL(path)
    x.xkb_context_new.restype = ctypes.c_void_p
    x.xkb_context_new.argtypes = [ctypes.c_int]
    x.xkb_keymap_new_from_names.restype = ctypes.c_void_p
    x.xkb_keymap_new_from_names.argtypes = [ctypes.c_void_p, ctypes.POINTER(Names), ctypes.c_int]
    x.xkb_keymap_unref.argtypes = [ctypes.c_void_p]
    x.xkb_keymap_mod_get_index.restype = ctypes.c_uint32
    x.xkb_keymap_mod_get_index.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    x.xkb_state_new.restype = ctypes.c_void_p
    x.xkb_state_new.argtypes = [ctypes.c_void_p]
    x.xkb_state_unref.argtypes = [ctypes.c_void_p]
    x.xkb_state_update_mask.argtypes = [ctypes.c_void_p] + [ctypes.c_uint32] * 6
    x.xkb_state_key_get_one_sym.restype = ctypes.c_uint32
    x.xkb_state_key_get_one_sym.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
    x.xkb_keysym_to_utf32.restype = ctypes.c_uint32
    x.xkb_keysym_to_utf32.argtypes = [ctypes.c_uint32]
    x.xkb_keysym_get_name.argtypes = [ctypes.c_uint32, ctypes.c_char_p, ctypes.c_size_t]
    return x


def descriptions():
    d = {}
    root = ET.parse(EVDEV_XML).getroot()
    for lay in root.iter("layout"):
        ci = lay.find("configItem")
        name = ci.findtext("name")
        d[(name, "")] = ci.findtext("description")
        vl = lay.find("variantList")
        if vl is not None:
            for v in vl.iter("variant"):
                vc = v.find("configItem")
                d[(name, vc.findtext("name"))] = vc.findtext("description")
    return d


def token(cp):
    if cp == 0:
        return "--"
    cat = unicodedata.category(chr(cp))
    if cp <= 0x20 or cat[0] in "MCZ" or cp in (0x23, 0x5C) or 0xD800 <= cp <= 0xDFFF:
        return "\\u{%X}" % cp
    return chr(cp)


def dead_table(name, mark):
    """deadkey line: base letter + mark -> its precomposed letter; a line holds at
    most 40 tokens (zui_keymap.c TOK_MAX), so at most 37 pairs, small and capital
    letters together"""
    pairs = []
    bases = [ch for c in range(26) for ch in (chr(ord("a") + c), chr(ord("A") + c))]
    for b in bases:
        c = unicodedata.normalize("NFC", b + chr(mark))
        if len(c) == 1:
            pairs.append(b + c)
    return pairs[:37]


def read_layout(x, ctx, layout, variant):
    names = Names(b"evdev", b"pc105", layout.encode(), variant.encode(), None)
    km = x.xkb_keymap_new_from_names(ctx, ctypes.byref(names), 0)
    if not km:
        sys.exit(f"xkbcommon cannot compile {layout}({variant})")
    shift = 1 << x.xkb_keymap_mod_get_index(km, b"Shift")
    l3 = 1 << x.xkb_keymap_mod_get_index(km, b"Mod5")
    masks = [0, shift, l3, l3 | shift]
    st = x.xkb_state_new(km)
    keys = {}  # (row, col) -> [4 values]; a value is a code point or ("dead", name)
    buf = ctypes.create_string_buffer(64)
    for row, codes in ROWS.items():
        for col, code in enumerate(codes):
            if code is None:
                continue
            vals = []
            for m in masks:
                x.xkb_state_update_mask(st, m, 0, 0, 0, 0, 0)
                sym = x.xkb_state_key_get_one_sym(st, code + 8)
                x.xkb_keysym_get_name(sym, buf, 64)
                nm = buf.value.decode()
                if nm.startswith("dead_"):
                    vals.append(("dead", nm))
                else:
                    cp = x.xkb_keysym_to_utf32(sym)
                    vals.append(cp if cp >= 0x20 and cp != 0x7F else 0)
            keys[(row, col)] = vals
    x.xkb_state_unref(st)
    x.xkb_keymap_unref(km)
    return keys


def dead_name(nm):
    if nm in STD_DEAD:
        return STD_DEAD[nm]
    if DEAD_MARK.get(nm):
        return nm[5:][:15]
    return None


def to_text(zid, name, keys):
    lines = [f"keyboard {zid} {name}"]
    # an AltGr level equal to the base or shift level adds nothing
    for (row, col), v in keys.items():
        if v[2] == v[0] and v[3] == v[1]:
            v[2] = v[3] = 0
    # zui keeps at most ZUI_KM_DEAD_MAX (8) dead keys: the base and Shift levels'
    # first, then AltGr's, each in key order; the rest type nothing
    order = []
    for li in range(4):
        for row in "EDCB":
            for col in sorted(c for (r, c) in keys if r == row):
                v = keys[(row, col)][li]
                if isinstance(v, tuple) and dead_name(v[1]) and dead_name(v[1]) not in order:
                    order.append(dead_name(v[1]))
    kept = order[:8]
    for short in kept:
        nm = "dead_" + short
        if nm not in STD_DEAD and short not in STD_DEAD.values():
            mark = DEAD_MARK[[k for k in DEAD_MARK if k[5:][:15] == short][0]]
            lines.append(f"deadkey {short} \\u{{{mark:X}}} " + " ".join(dead_table(short, mark)))
    for li, lvl in enumerate(LEVELS):
        for row in "EDCB":
            cols = [c for (r, c) in keys if r == row]
            vals = {c: keys[(row, c)][li] for c in cols}
            have = [c for c in sorted(vals) if vals[c] != 0 and
                    not (isinstance(vals[c], tuple) and dead_name(vals[c][1]) not in kept)]
            if not have:
                continue
            start, end = have[0], have[-1]
            toks = []
            for c in range(start, end + 1):
                v = vals.get(c, 0)
                if isinstance(v, tuple):
                    d = dead_name(v[1])
                    toks.append("dead:" + d if d in kept else "--")
                else:
                    toks.append(token(v))
            lines.append(f"{lvl} {row}{start:02d} " + " ".join(toks))
    return "\n".join(lines) + "\n"


def c_lit(text):
    out = []
    for line in text.splitlines(True):
        s = line.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")
        out.append(f'    "{s}"')
    return "\n".join(out)


def main():
    x = xkb_lib()
    ctx = x.xkb_context_new(0)
    desc = descriptions()
    made, uses = [], []
    with open(TSV, encoding="utf-8") as f:
        for line in f:
            if line.startswith("#") or not line.strip():
                continue
            cols = line.rstrip("\n").split("\t")
            tag, zid = cols[0], cols[1]
            uses.append((tag, zid))
            if len(cols) > 2 and cols[2]:
                if zid in [m[0] for m in made]:
                    continue
                layout, _, variant = cols[2].partition("(")
                variant = variant.rstrip(")")
                name = (cols[3] if len(cols) > 3 and cols[3] else desc[(layout, variant)])[:47]
                if len(zid) > 15:
                    sys.exit(f"{zid}: a layout id is at most 15 bytes")
                made.append((zid, name, cols[2], read_layout(x, ctx, layout, variant)))
    o = ["/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */",
         "/* SPDX-License-Identifier: Apache-2.0 */",
         "/* Generated by tools/zui/gen_keymaps.py from xkeyboard-config (MIT/X11 licence)",
         " * and tools/zui/locales/KEYBOARDS.tsv. Do not edit. */",
         "// clang-format off"]
    for zid, name, src, keys in made:
        cid = zid.replace("-", "_").upper()
        o.append(f"/* {src} */")
        o.append(f"static const char LX_{cid}[] =")
        o.append(c_lit(to_text(zid, name, keys)) + ";")
    o.append("#define ZUI_KEYMAP_XKB_LAYOUTS \\")
    o.append(" \\\n".join(f'    {{"{zid}", LX_{zid.replace("-", "_").upper()}}},' for zid, *_ in made))
    o.append("")
    o.append("/* the layout each locale types with (zui_keymap_for_locale) */")
    o.append("static const struct {")
    o.append("    const char *tag, *layout;")
    o.append("} LOCALE_KEYMAP[] = {")
    for tag, zid in uses:
        o.append(f'    {{"{tag}", "{zid}"}},')
    o.append("};")
    o.append("// clang-format on")
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(o) + "\n")
    print(f"{len(made)} layouts from xkeyboard-config, {len(uses)} locales")


if __name__ == "__main__":
    main()
