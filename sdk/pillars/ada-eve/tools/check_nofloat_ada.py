# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""R11 for Ada: no floating-point (or binary fixed-point) type in the sources.

Outside comments, strings and character literals, a file may not use:
  - the predefined floating-point types (Float, Short_Float, Long_Float,
    Long_Long_Float), Interfaces.C's C_float, double, long_double, or
    Interfaces' IEEE_Float_32 / IEEE_Float_64;
  - a real literal (1.5, 1.0E-4, 16#1.8#);
  - a floating-point or fixed-point type declaration ("is digits",
    "is delta"): the binding holds decimals as integers with a scale.

The one exception is a region between the lines
    -- R11-BOUNDARY-BEGIN ...
    -- R11-BOUNDARY-END
of which a file may have at most one (the binding's single IEEE-to-rational
conversion, and the tests that call it). Compile-fail cases, which exist to
show that a float is rejected, are not passed to this check.

usage: python3 tools/check_nofloat_ada.py FILE...
"""
import re
import sys

BANNED_NAMES = {
    "float", "short_float", "long_float", "long_long_float",
    "c_float", "double", "long_double", "ieee_float_32", "ieee_float_64", "ieee_extended_float",
}
REAL = re.compile(r"\b\d[\d_]*(#[0-9a-fA-F_]*\.[0-9a-fA-F_]*#|\.\d[\d_]*)([eE][+-]?\d+)?")
WORD = re.compile(r"[A-Za-z][A-Za-z0-9_]*")
DECL = re.compile(r"\bis\s+(digits|delta)\b", re.IGNORECASE)


def strip(line):
    """The line without its comment, strings and character literals."""
    out, i, n = [], 0, len(line)
    while i < n:
        c = line[i]
        if c == "-" and line.startswith("--", i):
            break
        if c == '"':
            j = i + 1
            while j < n:
                if line[j] == '"':
                    if j + 1 < n and line[j + 1] == '"':
                        j += 2
                        continue
                    break
                j += 1
            out.append('""')
            i = j + 1
            continue
        if c == "'" and i + 2 < n and line[i + 2] == "'" and (i == 0 or not (line[i - 1].isalnum() or line[i - 1] in "_)")):
            out.append("' '")
            i += 3
            continue
        out.append(c)
        i += 1
    return "".join(out)


def check(path):
    errors = []
    regions = 0
    inside = False
    with open(path, encoding="utf-8") as f:
        lines = f.read().split("\n")
    for no, raw in enumerate(lines, 1):
        tag = raw.strip()
        if tag.startswith("-- R11-BOUNDARY-BEGIN"):
            if inside:
                errors.append(f"{path}:{no}: nested R11-BOUNDARY-BEGIN")
            inside = True
            regions += 1
            if regions > 1:
                errors.append(f"{path}:{no}: more than one R11 boundary region in one file")
            continue
        if tag.startswith("-- R11-BOUNDARY-END"):
            if not inside:
                errors.append(f"{path}:{no}: R11-BOUNDARY-END without BEGIN")
            inside = False
            continue
        if inside:
            continue
        code = strip(raw)
        m = REAL.search(code)
        if m:
            errors.append(f"{path}:{no}: real literal '{m.group(0)}': {raw.strip()}")
        for w in WORD.findall(code):
            if w.lower() in BANNED_NAMES:
                errors.append(f"{path}:{no}: floating-point type '{w}': {raw.strip()}")
        m = DECL.search(code)
        if m:
            errors.append(f"{path}:{no}: '{m.group(0)}' declares a float or fixed-point type: {raw.strip()}")
    if inside:
        errors.append(f"{path}: R11-BOUNDARY-BEGIN without END")
    return errors, regions


def main(paths):
    if not paths:
        print(__doc__)
        return 2
    errors, regions = [], 0
    for p in paths:
        e, r = check(p)
        errors += e
        regions += r
    for e in errors:
        print(e)
    if errors:
        print(f"check-nofloat-ada: {len(errors)} violation(s) in {len(paths)} files")
        return 1
    print(f"check-nofloat-ada: {len(paths)} files clean (R11); {regions} marked boundary region(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
