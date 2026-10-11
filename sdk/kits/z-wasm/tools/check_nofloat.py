# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""R11 check: the Z-Wasm validator and meter sources contain no float type.

Comments and string/character literals are removed first, so messages such
as "never floats" do not count; then any float, double, long double,
_Float*, _Complex token or a <math.h>/<float.h> include fails the check.

usage: python3 tools/check_nofloat.py FILE...
"""
import re
import sys

BAD = re.compile(r"\b(float|double|_Float\d+|_Complex|_Imaginary)\b|<(math|float|complex|fenv)\.h>")


def strip(src: str) -> str:
    out = []
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if src.startswith("/*", i):
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("\n" * src.count("\n", i, j))
            i = j
        elif src.startswith("//", i):
            j = src.find("\n", i)
            i = n if j < 0 else j
        elif c in "\"'":
            j = i + 1
            while j < n and src[j] != c:
                j += 2 if src[j] == "\\" else 1
            out.append(c + c)
            i = j + 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def main(paths):
    bad = 0
    for p in paths:
        with open(p, encoding="utf-8") as f:
            code = strip(f.read())
        for ln, line in enumerate(code.splitlines(), 1):
            # keep #include <math.h> visible: includes survive stripping
            # only if written with <>, which is what we look for
            if BAD.search(line):
                print(f"{p}:{ln}: R11: {line.strip()}")
                bad += 1
    if bad:
        print("check-nofloat: FAILED")
        return 1
    print(f"check-nofloat: OK ({len(paths)} files, no float types)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
