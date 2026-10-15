#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""The gate every built-in app passes before it is merged: it is really built
and really tested, not only present.

For every app manifest with a slot (kernel/src/apps/*.zxapp) it fails when:
  * a logic line names a file that does not exist;
  * the model it names does not exist, or is never tested: either test_apps.sh
    compiles it and test_apps.c includes its header, or the app has its own
    model test kernel/src/apps/tests/<id>_apps.inc (APPS_TEST(<id>), including
    "<id>_model.h"; the model must then be apps/<id>_model.c);
  * its languages are never run and witnessed: either test_zxapp.c loads the
    manifest ("apps/<file>.zxapp"), or the app has its own test
    kernel/src/apps/tests/<id>_zxapp.inc (APP_TEST(<id>, <n>, <slot>) with the
    manifest's slot), which names every entry it provides ("<entry>") and,
    when any entry runs in two languages or more, tests a disagreement
    (ZXAPP_BOTH);
  * a file in kernel/src/apps/tests/ belongs to no app;
  * the model calls a daemon method (zxm_begin) that api/zxvd.openrpc.json
    does not list: an app reaches only methods that exist;
and for every app and block, when a file in its blocks/<id>/ folder is used
by no logic line (a .wasm's source beside it counts as used).
"""
import json
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SRC = os.path.join(ROOT, "kernel", "src")
APPS = os.path.join(SRC, "apps")
SPEC = os.path.join(ROOT, "api", "zxvd.openrpc.json")
TESTS = os.path.join(APPS, "tests")
WASM_SRC = (".cpp", ".cc", ".rs", ".zig", ".c")


def manifest(path):
    m = {"id": "", "slot": 0, "model": "", "logic": [], "provides": [], "entries": {}}
    inline = False
    with open(path, encoding="utf-8") as f:
        for line in f:
            if inline:
                inline = line.strip() != "."
                continue
            w = line.split("#", 1)[0].split()
            if not w:
                continue
            if w[0] == "id" and len(w) == 2:
                m["id"] = w[1]
            elif w[0] == "slot" and len(w) == 2:
                m["slot"] = int(w[1], 10)
            elif w[0] == "model" and len(w) == 2:
                m["model"] = w[1]
            elif w[0] == "provides" and len(w) >= 3:
                m["provides"].append(w[1])
            elif w[0] == "logic" and len(w) >= 4:
                m["entries"][w[2]] = m["entries"].get(w[2], 0) + 1
                inline = w[3] == "<<"
                if not inline:
                    m["logic"].append(w[3])
    return m


def main():
    errors = []
    methods = {x["name"] for x in json.load(open(SPEC, encoding="utf-8"))["methods"]}
    test_apps_sh = open(os.path.join(APPS, "test_apps.sh"), encoding="utf-8").read()
    test_apps_c = open(os.path.join(APPS, "test_apps.c"), encoding="utf-8").read()
    test_zxapp_c = open(os.path.join(APPS, "test_zxapp.c"), encoding="utf-8").read()
    files = [os.path.join(APPS, f) for f in sorted(os.listdir(APPS)) if f.endswith(".zxapp")]
    blocks = os.path.join(APPS, "blocks")
    files += [os.path.join(blocks, f) for f in sorted(os.listdir(blocks)) if f.endswith(".zxblock")]
    claimed = set()
    for p in files:
        rel = os.path.relpath(p, ROOT)
        m = manifest(p)
        used = set()
        for path in m["logic"]:
            full = os.path.join(SRC, path)
            if not os.path.exists(full):
                errors.append(f"{rel}: logic file {path} does not exist")
            used.add(os.path.normpath(full))
            if full.endswith(".wasm"):
                stem = full[:-5]
                used.update(os.path.normpath(stem + e) for e in WASM_SRC)
        folder = os.path.join(blocks, m["id"])
        if m["id"] and os.path.isdir(folder):
            for f in sorted(os.listdir(folder)):
                full = os.path.normpath(os.path.join(folder, f))
                if full not in used:
                    errors.append(f"{rel}: blocks/{m['id']}/{f} is used by no logic line")
        if not m["slot"]:
            continue
        name = os.path.basename(p)
        own = os.path.join(TESTS, f"{m['id']}_zxapp.inc")
        if os.path.exists(own):
            claimed.add(own)
            text = open(own, encoding="utf-8").read()
            if not re.search(r"^APP_TEST\(%s,\s*-?\d+,\s*%d\)" % (re.escape(m["id"]), m["slot"]), text, re.M):
                errors.append(f"{rel}: tests/{m['id']}_zxapp.inc has no APP_TEST({m['id']}, <n>, {m['slot']})")
            for e in m["provides"]:
                if f'"{e}"' not in text:
                    errors.append(f"{rel}: tests/{m['id']}_zxapp.inc never calls \"{e}\"")
            if any(k > 1 for k in m["entries"].values()) and "ZXAPP_BOTH" not in text:
                errors.append(f"{rel}: tests/{m['id']}_zxapp.inc never makes its languages "
                              "disagree (no ZXAPP_BOTH)")
        elif f'"apps/{name}"' not in test_zxapp_c:
            errors.append(f"{rel}: never loaded by a test (add tests/{m['id']}_zxapp.inc)")
        if not m["model"]:
            errors.append(f"{rel}: no model line")
            continue
        model = os.path.join(SRC, m["model"])
        if not os.path.exists(model):
            errors.append(f"{rel}: model {m['model']} does not exist")
            continue
        base = os.path.basename(model)
        own = os.path.join(TESTS, f"{m['id']}_apps.inc")
        if os.path.exists(own):
            claimed.add(own)
            text = open(own, encoding="utf-8").read()
            if m["model"] != f"apps/{m['id']}_model.c":
                errors.append(f"{rel}: with tests/{m['id']}_apps.inc the model must be "
                              f"apps/{m['id']}_model.c (test_apps.sh compiles that name)")
            if f'#include "{m["id"]}_model.h"' not in text or not re.search(
                    r"^APPS_TEST\(%s\)" % re.escape(m["id"]), text, re.M):
                errors.append(f"{rel}: tests/{m['id']}_apps.inc needs #include \"{m['id']}_model.h\" "
                              f"and APPS_TEST({m['id']})")
        else:
            if f"/{base}\"" not in test_apps_sh:
                errors.append(f"{rel}: its model is never compiled (add tests/{m['id']}_apps.inc)")
            if f'#include "{base[:-2]}.h"' not in test_apps_c:
                errors.append(f"{rel}: its model is never tested (add tests/{m['id']}_apps.inc)")
        text = open(model, encoding="utf-8").read()
        for meth in sorted(set(re.findall(r'zxm_begin\(\s*[^,]+,\s*"([^"]+)"', text))):
            if meth not in methods:
                errors.append(f"{rel}: {base} calls {meth}, which zxvd does not have "
                              "(api/zxvd.openrpc.json); use only existing methods")
    if os.path.isdir(TESTS):
        for f in sorted(os.listdir(TESTS)):
            full = os.path.join(TESTS, f)
            if full not in claimed:
                errors.append(f"kernel/src/apps/tests/{f}: belongs to no app with a slot "
                              "(name it <id>_zxapp.inc or <id>_apps.inc)")
    for e in errors:
        print("app_gate:", e, file=sys.stderr)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
