#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""classify.py: measure what each surviving mutant does.

A mutant that survives the tests is one of four things, and the score alone
cannot tell which:

    CHECKER     the change is inside a self-check body (a function matching the
                probe's checker pattern). It weakens a checker; while the code
                under check is correct the change cannot be observed.
    EQUIVALENT  the probe sees no difference: same exit status and the same
                trace, byte for byte, as the original over the probe's whole
                input set (seeded random walks plus boundary sweeps).
    OPEN        the probe sees a difference. The tests have a gap. A person
                reads the first differing trace line and the spec and decides:
                  negative  the original is right: add a test that kills it
                  positive  the mutant is right: the original is a bug, fix it
                Either way the mutant stops surviving. An OPEN mutant is never
                left OPEN.

A probe is a small C program, built with the target, that drives the module's
public API over a fixed input set and prints every result and every state it
can read. It is built with ASan + UBSan (no recovery), so a mutant that reads
or writes out of bounds, or overflows a signed integer, differs by crashing.
EQUIVALENT is a measurement over the probe's inputs, not a proof; the report
gives the argument for each one.

    python3 tests/harness/mutator/classify.py --config tests/harness/mutator/probes.json \\
        --results DIR [--only hkdf,lpres] [--out DIR] [--jobs N]

DIR holds mutate.py's <name>.after.json files. The metrics, per target:
    raw       = killed / valid                       (mutate.py's score)
    adjusted  = killed / (valid - equivalent)        (equivalent mutants are not faults)
    open      = survivors the probe sees differ      (must be 0 when the work is done)
Exit status: 0 when no target has an OPEN survivor, 1 otherwise, 2 on error.
"""
import argparse
import difflib
import json
import os
import re
import sys
from concurrent.futures import ThreadPoolExecutor

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mutate  # noqa: E402  (same directory)

ROOT = mutate.ROOT


def function_spans(src):
    """(name, first_line, last_line) of every top-level function body."""
    masked = mutate.mask_source(src)
    toks = mutate.tokenize(masked)
    spans, depth, i = [], 0, 0
    while i < len(toks):
        t = toks[i][3]
        if t == "{":
            if depth == 0 and i and toks[i - 1][3] == ")":
                # walk back over the parameter list to the function name
                d, j = 0, i - 1
                while j >= 0:
                    if toks[j][3] == ")":
                        d += 1
                    elif toks[j][3] == "(":
                        d -= 1
                        if d == 0:
                            break
                    j -= 1
                name = toks[j - 1][3] if j > 0 else "?"
                start = mutate.line_of(src, toks[i][0])
                d, k = 0, i
                while k < len(toks):
                    if toks[k][3] == "{":
                        d += 1
                    elif toks[k][3] == "}":
                        d -= 1
                        if d == 0:
                            break
                    k += 1
                spans.append((name, start, mutate.line_of(src, toks[min(k, len(toks) - 1)][0])))
                i = k + 1
                continue
            depth += 1
        elif t == "}":
            depth -= 1
        i += 1
    return spans


def run_probe(cmd, cwd, timeout, env):
    import subprocess
    import signal
    import time
    t0 = time.time()
    p = subprocess.Popen(cmd, shell=True, cwd=cwd, env=env, stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT, start_new_session=True)
    try:
        out, _ = p.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(p.pid, signal.SIGKILL)
        except OSError:
            pass
        p.communicate()
        return None, b"", time.time() - t0
    return p.returncode, out, time.time() - t0


def first_diff(a, b):
    al = a.decode("utf-8", "replace").splitlines()
    bl = b.decode("utf-8", "replace").splitlines()
    for line in bl:  # a sanitizer report says more than the truncated trace
        if "runtime error" in line or "ERROR: AddressSanitizer" in line:
            return line.strip()[:200]
    for d in difflib.unified_diff(al, bl, "original", "mutant", n=0, lineterm=""):
        if d.startswith(("---", "+++", "@@")):
            continue
        return d[:200]
    return "(trace equal; exit status differs)"


def classify(name, entry, res, workers, jobs, out_dir):
    target = entry["target"]
    with open(os.path.join(ROOT, target)) as f:
        src = f.read()
    if res["target"] != target:
        print("[ERROR] %s: result is for %s, probe is for %s" % (name, res["target"], target))
        return None
    surv = [m for m in res["mutant_list"] if m["status"] == "survived"]
    for m in surv:
        if src[m["start"]:m["end"]] != m["orig"]:
            print("[ERROR] %s: the source changed since the mutation run (mutant #%d at line %d); "
                  "re-run mutate.py first" % (name, m["id"], m["line"]))
            return None
    checker_re = re.compile(entry.get("checker", r"$^"))
    spans = function_spans(src)

    def func_of(line):
        for fn, a, b in spans:
            if a <= line <= b:
                return fn
        return ""

    cmd = entry["probe"]
    env = dict(os.environ)
    base = []
    for w in workers[:2]:  # the probe must be deterministic: run it twice, in two trees
        wenv = dict(env, ZXV_TMP=os.path.join(w, ".probe_tmp"))
        os.makedirs(wenv["ZXV_TMP"], exist_ok=True)
        base.append(run_probe(cmd, w, None, wenv))
    if base[0][0] != 0:
        print("[ERROR] %s: the probe fails on the unmutated tree (exit %s):\n%s"
              % (name, base[0][0], base[0][1].decode("utf-8", "replace")[-2000:]))
        return None
    if len(base) > 1 and base[1][:2] != base[0][:2]:
        print("[ERROR] %s: the probe is not deterministic (two runs differ)" % name)
        return None
    rc0, out0, dt = base[0]
    timeout = max(30.0, 6 * dt + 10)
    for w in workers[2:]:
        os.makedirs(os.path.join(w, ".probe_tmp"), exist_ok=True)
    print("[%s] probe: %d trace lines, %.1fs; %d survivors" % (name, out0.count(b"\n"), dt, len(surv)),
          flush=True)

    import threading
    free, lock = list(workers), threading.Lock()

    def one(m):
        fn = func_of(m["line"])
        rec = dict(id=m["id"], line=m["line"], op=m["op"], orig=m["orig"], repl=m["repl"], func=fn,
                   text=src.splitlines()[m["line"] - 1].strip()[:100])
        if checker_re.search(fn):
            rec["class"] = "CHECKER"
            return rec
        with lock:
            w = free.pop()
        try:
            path = os.path.join(w, target)
            with open(path, "w") as f:
                f.write(mutate.apply(src, m))
            wenv = dict(env, ZXV_TMP=os.path.join(w, ".probe_tmp"))
            rc, out, _ = run_probe(cmd, w, timeout, wenv)
            with open(path, "w") as f:
                f.write(src)
        finally:
            with lock:
                free.append(w)
        if rc == rc0 and out == out0:
            rec["class"] = "EQUIVALENT"
        else:
            rec["class"] = "OPEN"
            rec["exit"] = "timeout" if rc is None else rc
            rec["diff"] = first_diff(out0, out) if rc is not None else "(probe timed out)"
            rec["trace"] = out.decode("utf-8", "replace")[-4000:]
        return rec

    with ThreadPoolExecutor(max_workers=jobs) as ex:
        recs = list(ex.map(one, surv))
    c = res["counts"]
    killed = c["killed"] + c["timeout"]
    valid = killed + c["survived"]
    n = {k: sum(1 for r in recs if r["class"] == k) for k in ("CHECKER", "EQUIVALENT", "OPEN")}
    adj = killed / (valid - n["EQUIVALENT"]) if valid - n["EQUIVALENT"] else 1.0
    summary = dict(name=name, target=target, valid=valid, killed=killed, survived=c["survived"],
                   checker=n["CHECKER"], equivalent=n["EQUIVALENT"], open=n["OPEN"],
                   raw=round(killed / valid, 4) if valid else 0.0, adjusted=round(adj, 4),
                   probe=cmd, probe_lines=out0.count(b"\n"), survivors=recs)
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, "%s.classify.json" % name), "w") as f:
        json.dump(summary, f, indent=1)
    with open(os.path.join(out_dir, "%s.classify.txt" % name), "w") as f:
        f.write("%s: raw %.1f%%, adjusted %.1f%%; survivors %d = checker %d + equivalent %d + open %d\n"
                % (target, 100 * summary["raw"], 100 * adj, c["survived"], n["CHECKER"],
                   n["EQUIVALENT"], n["OPEN"]))
        for k in ("OPEN", "EQUIVALENT", "CHECKER"):
            f.write("%s:\n" % k)
            for r in recs:
                if r["class"] != k:
                    continue
                f.write("  #%d %s:%d %-6s '%s' -> '%s'  [%s]  | %s\n"
                        % (r["id"], target, r["line"], r["op"], r["orig"], r["repl"], r["func"],
                           r["text"]))
                if k == "OPEN":
                    f.write("      exit %s; first difference: %s\n" % (r["exit"], r["diff"]))
    print("[%s] raw %.1f%% adjusted %.1f%%: checker %d, equivalent %d, open %d"
          % (name, 100 * summary["raw"], 100 * adj, n["CHECKER"], n["EQUIVALENT"], n["OPEN"]),
          flush=True)
    return summary


def main(argv):
    ap = argparse.ArgumentParser(description="classify surviving mutants with a differential probe")
    ap.add_argument("--config", default=os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                     "probes.json"))
    ap.add_argument("--results", required=True, help="directory of mutate.py <name>.after.json")
    ap.add_argument("--phase", default="after")
    ap.add_argument("--only")
    ap.add_argument("--out")
    ap.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2)))
    a = ap.parse_args(argv)
    with open(a.config) as f:
        cfg = json.load(f)
    names = [n for n in cfg["probes"] if not a.only or n in a.only.split(",")]
    if a.only and len(names) != len(a.only.split(",")):
        print("[ERROR] unknown probe name in --only")
        return 2
    out_dir = a.out or a.results
    import tempfile
    tmp = tempfile.mkdtemp(prefix="zxv_classify_")
    workers = [mutate.copy_tree(os.path.join(tmp, "w%d" % i)) for i in range(max(2, a.jobs))]
    rows, bad = [], False
    for n in names:
        p = os.path.join(a.results, "%s.%s.json" % (n, a.phase))
        if not os.path.exists(p):
            print("[ERROR] %s: no result %s" % (n, p))
            bad = True
            continue
        with open(p) as f:
            res = json.load(f)
        s = classify(n, cfg["probes"][n], res, workers, a.jobs, out_dir)
        if s is None:
            bad = True
            continue
        rows.append(s)
    import shutil
    shutil.rmtree(tmp, ignore_errors=True)
    print("\n%-14s %6s %6s %8s %7s %6s %5s %7s %8s" % ("target", "valid", "killed", "survived",
                                                     "checker", "equiv", "open", "raw", "adjusted"))
    for s in rows:
        print("%-14s %6d %6d %8d %7d %6d %5d %6.1f%% %7.1f%%"
              % (s["name"], s["valid"], s["killed"], s["survived"], s["checker"], s["equivalent"],
                 s["open"], 100 * s["raw"], 100 * s["adjusted"]))
    if bad:
        return 2
    return 1 if any(s["open"] for s in rows) else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
