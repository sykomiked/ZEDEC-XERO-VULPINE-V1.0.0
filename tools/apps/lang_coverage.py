#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Which of the 104 Lingua languages the built-in apps use, and for what.

Reads kernel/src/apps/LANGUAGE_FIT.tsv (what each language is best at) and
every app manifest and block (kernel/src/apps/*.zxapp, blocks/*.zxblock),
and writes docs/APP_LANGUAGE_COVERAGE.md. --check rebuilds it in memory and
fails when the committed file differs, or when:

  * a manifest names a language whose fit-table runtime is "-" (no
    front-end yet: a language is never claimed before it can run);
  * a WebAssembly entry has no C, C++, Rust or Zig source beside its .wasm,
    or a precompiled Lingua (zlir) entry does not say its language
    (the source language is what the table counts, so it must be there);
  * the fit table and kernel/src/lingua/catalogue.tsv (when the tree has
    it) do not list the same 104 languages in the same order.
"""
import argparse
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
APPS = os.path.join(ROOT, "kernel", "src", "apps")
SRC = os.path.join(ROOT, "kernel", "src")
FIT = os.path.join(APPS, "LANGUAGE_FIT.tsv")
CATALOGUE = os.path.join(SRC, "lingua", "catalogue.tsv")
OUT = os.path.join(ROOT, "docs", "APP_LANGUAGE_COVERAGE.md")

# zxapp runtime names (and aliases, zxapp.c) to the fit table's runtime column
RUNTIME_ALIAS = {
    "cobol": "sutra", "ada": "eve", "apl": "kron", "k": "kron", "fortran": "trebago",
    "mumps": "myra", "m": "myra", "c++": "cpp", "csharp": "dotnet", "c#": "dotnet",
    "fsharp": "dotnet", "f#": "dotnet", "js": "typescript", "javascript": "typescript",
    "ts": "typescript", "golang": "go", "py": "python", "kt": "kotlin",
    "fortran77": "trebago",  # Lingua's FORTRAN front-end runs the same language
    "f77": "trebago", "reckonwyrm": "trebago",  # AI twins name their legacy language
    "hatchling": "basic", "cairndrake": "forth",
}
# a .wasm entry's language is its source's
WASM_SOURCE = {".cpp": "C++", ".cc": "C++", ".rs": "Rust", ".zig": "Zig", ".c": "C"}
# a precompiled Lingua entry's language is the one its "lang N" line names (zl.h)
ZLIR_LANG = {"1": "FORTRAN", "2": "BASIC", "3": "Forth"}
# runtimes the kernel itself carries (the rest need their SDK's process)
IN_KERNEL = {"c", "sutra", "eve", "kron", "trebago", "myra", "wasm", "basic", "forth"}


def read_fit():
    rows = []
    with open(FIT, encoding="utf-8") as f:
        for n, line in enumerate(f, 1):
            if line.startswith("#") or not line.strip():
                continue
            cols = line.rstrip("\n").split("\t")
            if len(cols) != 4:
                sys.exit(f"{FIT}:{n}: want 4 columns, got {len(cols)}")
            lang, runtime, owns, best = cols
            rows.append({"lang": lang, "runtime": runtime, "owns": owns.split(","),
                         "best": best, "apps": {}})
    return rows


def read_manifests():
    """[(file, id, name, slot, [(runtime, entry, path)], has_c)]"""
    found = []
    paths = [os.path.join(APPS, f) for f in sorted(os.listdir(APPS)) if f.endswith(".zxapp")]
    blocks = os.path.join(APPS, "blocks")
    paths += [os.path.join(blocks, f) for f in sorted(os.listdir(blocks)) if f.endswith(".zxblock")]
    for p in paths:
        meta = {"id": "", "name": "", "slot": 0}
        logic, has_c = [], False
        inline = False
        with open(p, encoding="utf-8") as f:
            for line in f:
                if inline:
                    inline = line.strip() != "."
                    continue
                w = line.split("#", 1)[0].split()
                if not w:
                    continue
                if w[0] in ("id", "slot") and len(w) == 2:
                    meta[w[0]] = int(w[1], 10) if w[0] == "slot" else w[1]
                elif w[0] == "name":
                    meta["name"] = " ".join(w[1:])
                elif w[0] in ("view", "model"):
                    has_c = True
                elif w[0] == "logic" and len(w) >= 4:
                    logic.append((w[1].lower(), w[2], w[3]))
                    inline = w[3] == "<<"
        found.append((os.path.relpath(p, ROOT), meta["id"], meta["name"] or meta["id"],
                      meta["slot"], logic, has_c))
    return found


def build():
    fit = read_fit()
    errors = []
    twin = {}  # a language's AI twin (catalogue.tsv), "-" when it has none yet
    if os.path.exists(CATALOGUE):
        cat = []
        with open(CATALOGUE, encoding="utf-8") as f:
            for line in f:
                if not line.startswith("#") and line.strip():
                    col = line.rstrip("\n").split("\t")
                    cat.append(col[1])
                    if col[4] != "-":
                        twin[col[1]] = col[4]
        if cat != [r["lang"] for r in fit]:
            errors.append("LANGUAGE_FIT.tsv and lingua/catalogue.tsv list different languages")
    for r in fit:
        r["twin"] = twin.get(r["lang"], "-")
    by_runtime = {}
    for r in fit:
        if r["runtime"] != "-":
            by_runtime.setdefault(r["runtime"], []).append(r)
    by_name = {r["lang"]: r for r in fit}
    for file, ident, name, slot, logic, has_c in read_manifests():
        kind = "block" if file.endswith(".zxblock") else "example"
        who = f"{slot:03d} {name}" if slot else f"{kind} {ident}"
        if has_c:
            by_name["C"]["apps"].setdefault(who, set()).add("view/model")
        for runtime, entry, path in logic:
            rt = RUNTIME_ALIAS.get(runtime, runtime)
            if rt == "wasm":
                stem = os.path.splitext(os.path.join(SRC, path))[0]
                src = [ext for ext in WASM_SOURCE if os.path.exists(stem + ext)]
                if not src:
                    errors.append(f"{file}: {entry}: {path} has no source beside it")
                    continue
                by_name[WASM_SOURCE[src[0]]]["apps"].setdefault(who, set()).add(entry)
                by_name["WebAssembly"]["apps"].setdefault(who, set()).add(entry)
                continue
            if rt == "zlir":
                lang = None
                try:
                    with open(os.path.join(SRC, path), encoding="utf-8") as z:
                        for zl in z:
                            if zl.startswith("lang "):
                                lang = ZLIR_LANG.get(zl.split()[1])
                                break
                except OSError:
                    pass
                if not lang:
                    errors.append(f"{file}: {entry}: {path} names no Lingua language")
                    continue
                by_name[lang]["apps"].setdefault(who, set()).add(entry)
                continue
            langs = by_runtime.get(rt)
            if not langs:
                errors.append(f"{file}: {entry}: '{runtime}' is no runtime in LANGUAGE_FIT.tsv")
                continue
            # an alias names its own language (apl, k); otherwise the runtime's first
            pick = langs[0]
            for r in langs:
                if r["lang"].lower() == runtime:
                    pick = r
            pick["apps"].setdefault(who, set()).add(entry)
    return fit, errors


def render(fit):
    used = [r for r in fit if r["apps"]]
    ready = [r for r in fit if not r["apps"] and r["runtime"] != "-"]
    waiting = [r for r in fit if r["runtime"] == "-"]
    out = [
        "<!--",
        "Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC",
        "SPDX-License-Identifier: Apache-2.0",
        "-->",
        "",
        "# Languages in the built-in apps",
        "",
        "Generated by `tools/apps/lang_coverage.py` from",
        "`kernel/src/apps/LANGUAGE_FIT.tsv` and the app manifests. Do not edit by hand.",
        "",
        "Every one of the 104 languages in the Lingua catalogue gets the app features",
        "it is best at, and only those. A language is counted here only when an app",
        "actually runs logic in it; one with no front-end yet is listed as waiting,",
        "never claimed. Critical answers run in two languages and are joined by the",
        "paraconsistent witness (`zxapp_call_witnessed`): when they disagree the",
        "answer is BOTH and the app holds it. A language's AI twin is its canonical",
        "spelling (kernel/src/lingua: zl_twin); Lingua blocks are committed in it.",
        "",
        f"- **In use by an app:** {len(used)} of {len(fit)}",
        f"- **Can run, no app uses it yet:** {len(ready)}",
        f"- **Waiting for a front-end (Lingua):** {len(waiting)}",
        "",
        "| Language | AI twin | Runs as | Best at | Owns | Apps |",
        "|---|---|---|---|---|---|",
    ]
    for r in fit:
        if r["runtime"] == "-":
            runs = "waiting"
        elif r["runtime"] in IN_KERNEL:
            runs = r["runtime"]
        else:
            runs = f"{r['runtime']} (SDK)"
        apps = "; ".join(f"{a} ({', '.join(sorted(e))})" for a, e in sorted(r["apps"].items()))
        out.append(f"| {r['lang']} | {r['twin']} | {runs} | {r['best']} | {', '.join(r['owns'])} | "
                   f"{apps or '-'} |")
    return "\n".join(out) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    fit, errors = build()
    text = render(fit)
    for e in errors:
        print("lang_coverage:", e, file=sys.stderr)
    if a.check:
        have = open(OUT, encoding="utf-8").read() if os.path.exists(OUT) else ""
        if have != text:
            print("lang_coverage: docs/APP_LANGUAGE_COVERAGE.md is stale; run "
                  "tools/apps/lang_coverage.py", file=sys.stderr)
            return 1
        return 1 if errors else 0
    with open(OUT, "w", encoding="utf-8") as f:
        f.write(text)
    print(f"wrote {os.path.relpath(OUT, ROOT)}")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
