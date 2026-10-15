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
  * it uses a block from a layer no nearer the core than its own (the grid
    is built inside out: the shared blocks are the core, then the L1 apps
    in matrix order, tools/server-tasks/MATRIX_ORDER.tsv, with the wave 0
    apps already built, then L2 and L3 in wave order); an app may use only
    the core's blocks and apps built before it, never one built after it
    or itself;
  * the model calls a daemon method (zxm_begin) that api/zxvd.openrpc.json
    does not list: an app reaches only methods that exist;
and for every app and block, when a file in its blocks/<id>/ folder is used
by no logic line (a .wasm's source beside it counts as used).

An app that is not there passes all of that, so a build that wrote nothing
would pass too. --app ID (repeatable) names the apps a change must deliver,
and --since REF requires the change since REF to add at least one app
manifest and gates every app it adds the same way. Each such app fails
unless it has:
  * kernel/src/apps/<id>.zxapp with a slot, version 1.0.0, a model and at
    least one provides line, every provides line backed by logic (or a
    re-export of a composed entry);
  * a model of real code: apps/<id>_model.c and .h, the .c with at least
    MODEL_LINES lines of code;
  * its own tests: tests/<id>_zxapp.inc with at least one CHECK per entry it
    provides and a wave_same, and tests/<id>_apps.inc with at least two
    CHECKs.

--verdict (with --app or --since) also runs the two test scripts --runs
times and gives each app one of four values, as kernel/src/lpres encodes
them, each with what the repair loop (tools/apps/wave_loop.py) does next:
T fits and functions (accept), F a clear failure (repair), B the witnesses
contradict each other: its tests pass while a link it makes does not
resolve, or two runs of the same tests differ (hold for a person), N
nothing usable (requeue with more context). See verdicts() below.
"""
import argparse
import json
import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SRC = os.path.join(ROOT, "kernel", "src")
APPS = os.path.join(SRC, "apps")
SPEC = os.path.join(ROOT, "api", "zxvd.openrpc.json")
TESTS = os.path.join(APPS, "tests")
WASM_SRC = (".cpp", ".cc", ".rs", ".zig", ".c")
MODEL_LINES = 20


def manifest(path):
    m = {"id": "", "slot": 0, "model": "", "logic": [], "provides": [], "entries": {}, "uses": []}
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
            elif w[0] == "use" and len(w) >= 3:
                m["uses"].append(w[1])
            elif w[0] == "logic" and len(w) >= 4:
                m["entries"][w[2]] = m["entries"].get(w[2], 0) + 1
                inline = w[3] == "<<"
                if not inline:
                    m["logic"].append(w[3])
    return m


TASKS = os.path.join(ROOT, "tools", "server-tasks")


def build_rank(mans):
    """Each manifest's place from the core outward: (layer, position). The shared
    blocks and the examples are the core (0, 0); an L1 app is (1, its place in
    MATRIX_ORDER.tsv), (1, 0) when it was built before the order (wave 0); an L2
    or L3 app is (2 or 3, its wave * 1000 + its place in that wave)."""
    order = {}
    path = os.path.join(TASKS, "MATRIX_ORDER.tsv")
    if os.path.exists(path):
        for line in open(path, encoding="utf-8"):
            c = line.rstrip("\n").split("\t")
            if line.startswith("#") or c[0] == "position" or len(c) < 4:
                continue
            order[int(c[2], 10)] = int(c[0], 10)
    waves = {}
    if os.path.isdir(TASKS):
        for d in os.listdir(TASKS):
            if re.fullmatch(r"wave\d+", d) and os.path.isdir(os.path.join(TASKS, d)):
                for i, f in enumerate(sorted(os.listdir(os.path.join(TASKS, d)))):
                    if re.match(r"\d{3}-", f):
                        waves[int(f[:3], 10)] = int(d[4:], 10) * 1000 + i + 1
    rank = {}
    for p, m in mans:
        if p.endswith(".zxblock") or not m["slot"]:
            rank[m["id"]] = (0, 0)
        elif m["slot"] <= 144:
            rank[m["id"]] = (1, order.get(m["slot"], 0))
        else:
            rank[m["id"]] = (2 if m["slot"] <= 271 else 3, waves.get(m["slot"], 0))
    return rank


def check_layers(mans):
    """The grid is built inside out: a use reaches only nearer the core."""
    errors, rank = [], build_rank(mans)
    for p, m in mans:
        rel = os.path.relpath(p, ROOT)
        for u in m["uses"]:
            if u not in rank:
                errors.append(f"{rel}: uses {u}, which no manifest provides")
            elif rank[u] >= rank[m["id"]]:
                errors.append(f"{rel}: uses {u}, which is no nearer the core (layer {rank[u][0]}, "
                              f"place {rank[u][1]}) than {m['id']} (layer {rank[m['id']][0]}, place "
                              f"{rank[m['id']][1]}); build it here and register it, or wait for "
                              "the layer that owns it")
    return errors


def code_lines(path):
    """Lines that are neither blank nor only a comment."""
    n, block = 0, False
    with open(path, encoding="utf-8") as f:
        for line in f:
            s = line.strip()
            if block:
                if "*/" in s:
                    block = False
                    s = s.split("*/", 1)[1].strip()
                else:
                    continue
            if s.startswith("/*"):
                if "*/" not in s:
                    block = True
                continue
            if s and not s.startswith("//"):
                n += 1
    return n


def delivered(app_id):
    """--app: the app is really there, with code and its own tests."""
    errors = []
    rel = f"kernel/src/apps/{app_id}.zxapp"
    path = os.path.join(APPS, f"{app_id}.zxapp")
    if not os.path.exists(path):
        return [f"{rel}: not there; the app was not delivered"]
    m = manifest(path)
    if not m["slot"]:
        errors.append(f"{rel}: no slot")
    if not re.search(r"^version\s+1\.0\.0\s*$", open(path, encoding="utf-8").read(), re.M):
        errors.append(f"{rel}: no version 1.0.0")
    if not m["provides"]:
        errors.append(f"{rel}: provides nothing")
    for e in m["provides"]:
        if "." not in e and not m["entries"].get(e):
            errors.append(f"{rel}: provides {e} with no logic line")
    model = os.path.join(APPS, f"{app_id}_model.c")
    if m["model"] != f"apps/{app_id}_model.c" or not os.path.exists(model):
        errors.append(f"{rel}: no model apps/{app_id}_model.c")
    elif not os.path.exists(model[:-2] + ".h"):
        errors.append(f"{rel}: no model header apps/{app_id}_model.h")
    elif code_lines(model) < MODEL_LINES:
        errors.append(f"{rel}: apps/{app_id}_model.c has {code_lines(model)} lines of code, "
                      f"fewer than {MODEL_LINES}: not a model")
    zt = os.path.join(TESTS, f"{app_id}_zxapp.inc")
    at = os.path.join(TESTS, f"{app_id}_apps.inc")
    if not os.path.exists(zt):
        errors.append(f"{rel}: no tests/{app_id}_zxapp.inc")
    else:
        text = open(zt, encoding="utf-8").read()
        n = len(re.findall(r"\bCHECK\(", text))
        if n < max(1, len(m["provides"])):
            errors.append(f"{rel}: tests/{app_id}_zxapp.inc has {n} CHECKs for "
                          f"{len(m['provides'])} entries")
        if "wave_same(" not in text:
            errors.append(f"{rel}: tests/{app_id}_zxapp.inc never checks rows (wave_same)")
    if not os.path.exists(at):
        errors.append(f"{rel}: no tests/{app_id}_apps.inc")
    elif len(re.findall(r"\bCHECK\(", open(at, encoding="utf-8").read())) < 2:
        errors.append(f"{rel}: tests/{app_id}_apps.inc has fewer than 2 CHECKs")
    return errors


def added_apps(ref):
    out = subprocess.run(["git", "-C", ROOT, "diff", "--name-only", "--diff-filter=A", ref, "HEAD",
                          "--", "kernel/src/apps/*.zxapp"], capture_output=True, text=True)
    if out.returncode:
        return None
    return [os.path.basename(x)[:-6] for x in out.stdout.split()]


def gate_errors(want):
    """Every error the gate finds, the apps in want held to delivered() too."""
    errors = []
    for app_id in want:
        errors += delivered(app_id)

    methods = {x["name"] for x in json.load(open(SPEC, encoding="utf-8"))["methods"]}
    test_apps_sh = open(os.path.join(APPS, "test_apps.sh"), encoding="utf-8").read()
    test_apps_c = open(os.path.join(APPS, "test_apps.c"), encoding="utf-8").read()
    test_zxapp_c = open(os.path.join(APPS, "test_zxapp.c"), encoding="utf-8").read()
    files = [os.path.join(APPS, f) for f in sorted(os.listdir(APPS)) if f.endswith(".zxapp")]
    blocks = os.path.join(APPS, "blocks")
    files += [os.path.join(blocks, f) for f in sorted(os.listdir(blocks)) if f.endswith(".zxblock")]
    errors += check_layers([(p, manifest(p)) for p in files])
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
    return errors


# The verdict (--verdict): four values, encoded as kernel/src/lpres is
# (NEITHER 0, TRUE 1, FALSE 2, BOTH 3). Each is what happens next, not a
# connective: the gate decides it from two witnesses, and never folds them
# by a truth table.
#   fits:      the gate's own checks above, for this app.
#   functions: the app's two test files run (test_zxapp.sh, test_apps.sh),
#              RUNS times; a run says TRUE when both its tests ran with no
#              failure, FALSE when a failure names the app, NEITHER when
#              they never ran (the build broke elsewhere, or no tests).
# T  fits and functions                         -> accept
# F  a clear failure                            -> repair: try again
# B  the witnesses contradict each other: the
#    tests pass where a link does not resolve,
#    or two runs of the same tests differ       -> hold for a person
# N  nothing usable                             -> requeue with more context
VERDICT = {"N": 0, "T": 1, "F": 2, "B": 3}
ACTION = {"T": "accept", "F": "repair", "B": "hold", "N": "requeue"}
SUITES = (("test_zxapp.sh", "ran app "), ("test_apps.sh", "ran model "))
# A link that must resolve: what an app reaches (a block or app it uses, a
# daemon method, a logic file, its model). The tests can pass while one of
# these is broken only when they do not exercise it: that is BOTH.
LINK = ("uses ", "which zxvd does not have", "logic file", "does not exist")


def names(line, app_id):
    """Whether an error or a test's failure line names this app."""
    e = re.escape(app_id)
    return bool(re.search(r"apps/%s\.zxapp\b|tests/%s_(zxapp|apps)\.inc\b|(^|[/\s\"])%s_model\.[ch]\b|"
                          r"blocks/%s/|(\)|:\d+)\s+%s(:|\s+registers)" % (e, e, e, e, e), line))


def run_suite(script):
    p = subprocess.run(["sh", os.path.join(APPS, script)], cwd=ROOT, capture_output=True, text=True)
    return p.returncode, p.stdout + p.stderr


def functions(app_id, outs):
    """One run's witness for one app: (state, why)."""
    states, why = [], []
    for (script, mark), (rc, out) in zip(SUITES, outs):
        bad = [x.strip() for x in out.splitlines()
               if ("FAIL" in x or "error:" in x) and not x.startswith("app_gate:") and names(x, app_id)]
        if bad:
            states.append("F")
            why += [f"{script}: {x}" for x in bad[:8]]
        elif rc == 3:
            states.append("N")
            why.append(f"{script}: the build broke before any test ran (not in this app's files)")
        elif re.search(r"^\s*%s%s$" % (re.escape(mark), re.escape(app_id)), out, re.M):
            states.append("T")
        else:
            states.append("N")
            why.append(f"{script}: this app's test never ran")
    if "F" in states:
        return "F", why
    return ("T" if states == ["T", "T"] else "N"), why


def verdicts(want, errors, runs):
    """Each app's verdict: {id: {verdict, action, fits, functions}}."""
    outs = [[run_suite(s) for s, _ in SUITES] for _ in range(runs)]
    res = {}
    for app_id in want:
        mine = [e for e in errors if names(e, app_id)]
        path = os.path.join(APPS, f"{app_id}.zxapp")
        m = manifest(path) if os.path.exists(path) else None
        r = {"fits": mine, "functions": []}
        if m is None or not m["provides"]:
            v = "N"
            r["functions"].append("nothing usable: " + ("no manifest" if m is None else "it provides nothing"))
        else:
            seen = [functions(app_id, o) for o in outs]
            for i, (st, why) in enumerate(seen):
                r["functions"] += [f"run {i + 1}: {x}" for x in why]
            fn = {st for st, _ in seen}
            if len(fn) > 1:
                v = "B"
                r["functions"].append("the same tests gave " + ", ".join(st for st, _ in seen) +
                                      " on " + str(runs) + " runs")
            else:
                fn = fn.pop()
                link = [e for e in mine if any(k in e for k in LINK)]
                if not mine:
                    v = fn
                elif fn == "T" and link:
                    v = "B"
                    r["functions"].append("its tests pass, yet a link it makes does not resolve")
                else:
                    v = "F"
        r["verdict"], r["action"] = v, ACTION[v]
        res[app_id] = r
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--app", action="append", default=[], help="an app the change must deliver")
    ap.add_argument("--since", help="the change since this ref must add an app")
    ap.add_argument("--verdict", action="store_true",
                    help="run the tests too and give each app T, F, B or N (and its action)")
    ap.add_argument("--runs", type=int, default=2, help="--verdict: runs of the tests to compare")
    ap.add_argument("--json", help="--verdict: write the verdicts here as JSON")
    a = ap.parse_args()
    errors, want = [], list(a.app)
    if a.since:
        got = added_apps(a.since)
        if got is None:
            errors.append(f"--since {a.since}: not a ref git knows")
        elif not got:
            errors.append(f"nothing delivered: no app manifest added since {a.since}")
        else:
            want += [x for x in got if x not in want]
    errors += gate_errors(want)
    for e in errors:
        print("app_gate:", e, file=sys.stderr)
    if not a.verdict:
        return 1 if errors else 0
    if not want:
        print("app_gate: --verdict needs --app or --since", file=sys.stderr)
        return 2
    res = verdicts(want, errors, max(1, a.runs))
    for app_id, r in res.items():
        print(f"verdict {app_id} {r['verdict']} {r['action']}")
        for x in r["fits"]:
            print(f"  fits: {x}")
        for x in r["functions"]:
            print(f"  functions: {x}")
    if a.json:
        with open(a.json, "w", encoding="utf-8") as f:
            json.dump({"apps": res, "encoding": VERDICT}, f, indent=1)
    return 0 if all(r["verdict"] == "T" for r in res.values()) else 1


if __name__ == "__main__":
    sys.exit(main())
