#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""libzxv ABI stability check (see sdk/docs/STABILITY.md).

  abi_check.py snapshot HEADER OUT.json   record the ABI of a release
  abi_check.py check HEADER BASELINE.json [LIBRARY]

`check` fails if anything in the baseline was removed or changed: a function's
prototype, a callback type, a constant's value, an enum's size, or (when
LIBRARY is given) an exported symbol. Additions are allowed; they must come
with a higher minor version than the baseline's.
"""
import json
import os
import re
import subprocess
import sys
import tempfile


def strip_comments(src):
    src = re.sub(r"/\*.*?\*/", " ", src, flags=re.S)
    return re.sub(r"//[^\n]*", " ", src)


def norm(s):
    s = re.sub(r"\s+", " ", s).strip()
    s = re.sub(r"\s*([(),*])\s*", r"\1", s)
    return s


def types_only(params):
    """Drop parameter names: renaming a parameter does not change the ABI."""
    out = []
    for p in norm(params).split(","):
        if re.search(r"\[\w*\]$", p):  # "uint8_t pk[32]" -> "uint8_t[32]"
            out.append(re.sub(r"\s*[A-Za-z_]\w*\s*(\[\w*\])$", r"\1", p))
            continue
        if len(re.findall(r"[A-Za-z_]\w*", p)) > 1 and re.search(r"[A-Za-z_]\w*$", p) \
                and not re.fullmatch(r"(const )?(unsigned |signed )?\w+", p):
            p = re.sub(r"[A-Za-z_]\w*$", "", p).strip()
        out.append(p)
    return ",".join(out)


def parse(header):
    src = strip_comments(open(header, encoding="utf-8").read())
    funcs = {}
    for m in re.finditer(r"\bZXV_API\b([^;{]*?)\b(zxv_\w+)\s*\(([^;]*?)\)\s*;", src, re.S):
        ret, name, params = m.groups()
        funcs[name] = norm(f"{ret} {name}({types_only(params)})")
    callbacks = {}
    for m in re.finditer(r"typedef([^;]*?)\(\s*\*\s*(zxv_\w+)\s*\)\s*\(([^;]*?)\)\s*;", src, re.S):
        ret, name, params = m.groups()
        callbacks[name] = norm(f"{ret}(*{name})({types_only(params)})")
    macros = sorted(set(re.findall(r"^\s*#\s*define\s+(ZXV_[A-Z0-9_]+)\s+\S", src, re.M)))
    macros = [m for m in macros if m not in ("ZXV_API", "ZXV_API_H")]
    enums = {}
    for m in re.finditer(r"typedef\s+enum\s*\{(.*?)\}\s*(zxv_\w+)\s*;", src, re.S):
        body, name = m.groups()
        enums[name] = re.findall(r"\b(ZXV_[A-Z0-9_]+)\b\s*(?:=|,|$)", body)
    return funcs, callbacks, macros, enums


def values(header, macros, enums):
    """Compile a probe to read every constant's value and every enum's size."""
    consts = list(macros) + [c for cs in enums.values() for c in cs]
    lines = ["#include <stdio.h>", f'#include "{os.path.abspath(header)}"', "int main(void){"]
    for c in consts:
        # Only integer constants; anything else (strings, expressions with
        # casts to pointers) is skipped by the _Generic fallback.
        lines.append(
            f'printf("%s %lld\\n", "{c}", _Generic(({c}), char*: 0LL, const char*: 0LL, default: (long long)({c})));'
        )
    for e in enums:
        lines.append(f'printf("sizeof(%s) %zu\\n", "{e}", sizeof({e}));')
    lines.append("return 0;}")
    cc = os.environ.get("CC", "cc")
    with tempfile.TemporaryDirectory() as d:
        c, exe = os.path.join(d, "probe.c"), os.path.join(d, "probe")
        open(c, "w").write("\n".join(lines))
        subprocess.run([cc, "-std=c11", "-w", c, "-o", exe], check=True)
        out = subprocess.run([exe], check=True, capture_output=True, text=True).stdout
    vals = {}
    for line in out.splitlines():
        k, v = line.rsplit(" ", 1)
        vals[k] = int(v)
    return vals


def snapshot(header):
    funcs, callbacks, macros, enums = parse(header)
    vals = values(header, macros, enums)
    ver = [vals["ZXV_ABI_VERSION_MAJOR"], vals["ZXV_ABI_VERSION_MINOR"], vals["ZXV_ABI_VERSION_PATCH"]]
    # The version macros themselves change on every release; keep them out of
    # the frozen constants.
    consts = {k: v for k, v in vals.items() if not k.startswith(("ZXV_ABI_VERSION_", "sizeof("))}
    sizes = {k: v for k, v in vals.items() if k.startswith("sizeof(")}
    return {
        "abi": ver,
        "functions": dict(sorted(funcs.items())),
        "callbacks": dict(sorted(callbacks.items())),
        "constants": dict(sorted(consts.items())),
        "enum_sizes": dict(sorted(sizes.items())),
    }


def exported(lib):
    if sys.platform == "darwin":
        out = subprocess.run(["nm", "-gU", lib], check=True, capture_output=True, text=True).stdout
        names = [l.split()[-1].lstrip("_") for l in out.splitlines() if l.strip()]
    else:
        out = subprocess.run(["nm", "-D", "--defined-only", lib], check=True, capture_output=True, text=True).stdout
        names = [l.split()[-1] for l in out.splitlines() if l.strip()]
    return set(names)


def check(header, baseline_path, lib=None):
    base = json.load(open(baseline_path))
    cur = snapshot(header)
    errors = []
    if cur["abi"][0] != base["abi"][0]:
        errors.append(f"major version changed {base['abi'][0]} -> {cur['abi'][0]}: a new major is a new library, not an update")
    for kind in ("functions", "callbacks", "constants", "enum_sizes"):
        for k, v in base[kind].items():
            if k not in cur[kind]:
                errors.append(f"{kind}: {k} was removed")
            elif cur[kind][k] != v:
                errors.append(f"{kind}: {k} changed\n    was: {v}\n    now: {cur[kind][k]}")
    added = sorted(set(cur["functions"]) - set(base["functions"]))
    if added and tuple(cur["abi"][:2]) <= tuple(base["abi"][:2]):
        errors.append(f"{len(added)} new functions need a minor version above {base['abi'][0]}.{base['abi'][1]}: {', '.join(added)}")
    if tuple(cur["abi"]) < tuple(base["abi"]):
        errors.append(f"version went backwards: {base['abi']} -> {cur['abi']}")
    if lib:
        syms = exported(lib)
        for f in sorted(cur["functions"]):
            if f not in syms:
                errors.append(f"{f} is declared but not exported by {lib}")
    v = ".".join(map(str, base["abi"]))
    if errors:
        print(f"ABI check against {v} FAILED:")
        for e in errors:
            print("  - " + e)
        return 1
    print(f"ABI check against {v}: ok ({len(base['functions'])} functions, "
          f"{len(base['constants'])} constants unchanged; {len(added)} functions added)")
    return 0


def main(argv):
    if len(argv) >= 4 and argv[1] == "snapshot":
        snap = snapshot(argv[2])
        with open(argv[3], "w") as f:
            json.dump(snap, f, indent=1, sort_keys=False)
            f.write("\n")
        print(f"wrote {argv[3]}: {len(snap['functions'])} functions, {len(snap['constants'])} constants")
        return 0
    if len(argv) in (4, 5) and argv[1] == "check":
        return check(argv[2], argv[3], argv[4] if len(argv) == 5 else None)
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
