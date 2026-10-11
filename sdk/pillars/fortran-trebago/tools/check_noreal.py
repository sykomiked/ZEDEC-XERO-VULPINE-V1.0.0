# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""R11 check for Fortran: no floating-point type outside the marked boundary.

Comments and character literals are removed first, so a comment such as
"no REAL here" does not count. Then any REAL, COMPLEX or DOUBLE PRECISION
keyword, any REAL()/CMPLX()/DBLE()/FLOAT() conversion and any floating
kind (real32, real64, real128, c_float, c_double, c_long_double,
c_float_complex, ...) fails the check, except between the comment lines
"! R11-BOUNDARY-BEGIN" and "! R11-BOUNDARY-END" (fortran/zxv.f90 has one:
rational_from_ieee754, the explicit exact path from a REAL(REAL64)). A
file may have at most one boundary.

usage: python3 tools/check_noreal.py FILE...
"""
import re
import sys

BAD = re.compile(
    r"\b(real|complex|double\s*precision|double\s*complex|cmplx|dble|float|"
    r"real32|real64|real128|c_float|c_double|c_long_double|c_float_complex|"
    r"c_double_complex|c_long_double_complex|ieee_arithmetic)\b",
    re.IGNORECASE,
)


def strip(line: str) -> str:
    """Remove character literals and the trailing comment of one line."""
    out, i, q = [], 0, None
    while i < len(line):
        c = line[i]
        if q:
            if c == q:
                if i + 1 < len(line) and line[i + 1] == q:  # doubled quote
                    i += 2
                    continue
                q = None
                out.append(c)
            i += 1
            continue
        if c in "'\"":
            q = c
            out.append(c)
        elif c == "!":
            break
        else:
            out.append(c)
        i += 1
    return "".join(out)


def main(paths):
    bad = 0
    for p in paths:
        inside, boundaries = False, 0
        with open(p, encoding="utf-8") as f:
            lines = f.read().splitlines()
        for ln, raw in enumerate(lines, 1):
            s = raw.strip()
            if s.upper().startswith("! R11-BOUNDARY-BEGIN"):
                inside = True
                boundaries += 1
                continue
            if s.upper().startswith("! R11-BOUNDARY-END"):
                inside = False
                continue
            if inside:
                continue
            m = BAD.search(strip(raw))
            if m:
                print(f"{p}:{ln}: R11: '{m.group(0)}' outside the marked boundary: {s}")
                bad += 1
        if inside:
            print(f"{p}: R11-BOUNDARY-BEGIN without R11-BOUNDARY-END")
            bad += 1
        if boundaries > 1:
            print(f"{p}: {boundaries} R11 boundaries; at most one is allowed")
            bad += 1
    if bad:
        print("check-noreal: FAILED")
        return 1
    print(f"check-noreal: OK ({len(paths)} files, no REAL outside the one marked boundary)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
