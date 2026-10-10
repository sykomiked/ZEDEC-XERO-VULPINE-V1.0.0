#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""mutate.py: a deterministic source-level mutation harness for C files.

One target file, one test command:
    python3 tests/harness/mutator/mutate.py --target kernel/src/rmag/rmag_core.c \\
        --test "python3 tests/harness/tiers.py --mode fast --fail-fast -q t1_rational"

A batch over a config file of (target, test command) pairs:
    python3 tests/harness/mutator/mutate.py --config tests/harness/mutator/targets.json \\
        [--only rational,rmag_core] [--phase before|after|both] [--out DIR]

The CI gate (a small fixed subset, fails when a score drops below the
baseline recorded in the config):
    python3 tests/harness/mutator/mutate.py --config tests/harness/mutator/targets.json --ci

How it works
  The target is scanned with comments, string/char literals and preprocessor
  lines masked out, so only real code is mutated. Each mutant changes exactly
  one token or statement:
    rel     < <= > >= == != : boundary shift (< <-> <=, > <-> >=) and == <-> !=
    arith   binary + <-> -, * <-> /, += <-> -=, ++ <-> --
    const   an integer literal N -> N+1 and N-1
    delchk  delete a guard `if (...) return/break/continue/goto ...;`
    ret     negate a literal return code: -N -> 0, 0 -> -1, N -> 0, true <-> false
    cond    replace an `if` condition with 1 and with 0; a `while` condition with 0
  Mutants are generated in source order and deduplicated, so the same file
  always yields the same numbered list. Each mutant is applied alone to a
  private copy of the tree (one copy per --jobs worker), the test command is
  run there, and the outcome is recorded:
    killed         the command failed (exit status other than 0 and the build code)
    survived       the command passed: no test noticed the change
    timeout        the command ran past the timeout (counted as killed)
    compile-error  the command exited with --build-code (tiers.py exits 3 when
                   only builds failed): not a valid mutant, excluded from the score
  score = (killed + timeout) / (killed + timeout + survived)

Output: <out>/<name>.<phase>.json (every mutant and its outcome) and a text
summary with the survivors as file:line, operator, original -> replacement.
"""
import argparse
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__)))))

# ---------------------------------------------------------------- scanning


def mask_source(src):
    """Return src with comments, string/char literals and preprocessor lines
    replaced by spaces (newlines kept), so offsets are unchanged."""
    out = list(src)
    i, n = 0, len(src)
    at_line_start = True
    while i < n:
        c = src[i]
        if at_line_start and c in " \t":
            i += 1
            continue
        if at_line_start and c == "#":
            # preprocessor line, with backslash continuations
            while i < n and not (src[i] == "\n" and src[i - 1] != "\\"):
                if src[i] != "\n":
                    out[i] = " "
                i += 1
            continue
        at_line_start = False
        if c == "\n":
            at_line_start = True
            i += 1
        elif src.startswith("//", i):
            while i < n and src[i] != "\n":
                out[i] = " "
                i += 1
        elif src.startswith("/*", i):
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            for k in range(i, j):
                if src[k] != "\n":
                    out[k] = " "
            i = j
        elif c in "\"'":
            j = i + 1
            while j < n and src[j] != c:
                j += 2 if src[j] == "\\" else 1
            j = min(j + 1, n)
            for k in range(i, j):
                if src[k] != "\n":
                    out[k] = " "
            i = j
        else:
            i += 1
    return "".join(out)


TOKEN = re.compile(
    r"(?P<num>0[xX][0-9a-fA-F]+[uUlL]*|\d+\.\d*(?:[eE][-+]?\d+)?[fFlL]?|\d+[eE][-+]?\d+|\d+[uUlL]*)"
    r"|(?P<id>[A-Za-z_]\w*)"
    r"|(?P<op><<=|>>=|->|\+\+|--|<<|>>|<=|>=|==|!=|&&|\|\||\+=|-=|\*=|/=|%=|&=|\|=|\^=|"
    r"[-+*/%<>=!&|^~?:;,.(){}\[\]])")


def tokenize(masked):
    return [(m.start(), m.end(), m.lastgroup, m.group()) for m in TOKEN.finditer(masked)]


def line_of(src, pos):
    return src.count("\n", 0, pos) + 1


def match_paren(toks, i):
    """toks[i] is '('; return the index of its matching ')' or -1."""
    depth = 0
    for j in range(i, len(toks)):
        if toks[j][3] == "(":
            depth += 1
        elif toks[j][3] == ")":
            depth -= 1
            if depth == 0:
                return j
    return -1


REL = {"<": ["<="], "<=": ["<"], ">": [">="], ">=": [">"], "==": ["!="], "!=": ["=="]}
ARITH = {"+=": ["-="], "-=": ["+="], "++": ["--"], "--": ["++"]}
C_WORDS = {"return", "case", "sizeof", "void", "char", "short", "int", "long", "float", "double",
           "signed", "unsigned", "const", "volatile", "struct", "union", "enum", "bool",
           "static", "extern", "register", "restrict", "inline"}
OPS_ALL = ["rel", "arith", "const", "delchk", "ret", "cond"]


def gen_mutants(src, ops=OPS_ALL, lines=None):
    masked = mask_source(src)
    toks = tokenize(masked)
    muts = []

    def add(start, end, repl, op):
        ln = line_of(src, start)
        if lines and not any(a <= ln <= b for a, b in lines):
            return
        muts.append(dict(start=start, end=end, orig=src[start:end], repl=repl, op=op, line=ln))

    for i, (s, e, kind, t) in enumerate(toks):
        prev = toks[i - 1] if i else None
        if "rel" in ops and kind == "op" and t in REL:
            for r in REL[t]:
                add(s, e, r, "rel")
        if "arith" in ops and kind == "op":
            if t in ARITH:
                for r in ARITH[t]:
                    add(s, e, r, "arith")
            elif t in ("+", "-", "*", "/") and prev and \
                    (prev[2] in ("id", "num") or prev[3] in (")", "]")):
                if prev[2] == "id" and (prev[3] in C_WORDS or prev[3].endswith("_t")):
                    pass  # `return -1`, `uint8_t *p`: unary or a declarator
                else:
                    add(s, e, {"+": "-", "-": "+", "*": "/", "/": "*"}[t], "arith")
        if "const" in ops and kind == "num" and re.fullmatch(r"(0[xX][0-9a-fA-F]+|\d+)[uUlL]*", t):
            m = re.fullmatch(r"(0[xX][0-9a-fA-F]+|\d+)([uUlL]*)", t)
            v = int(m.group(1), 0)
            suf = m.group(2)
            # skip array-size / bit-field declarators on a line that declares a type
            add(s, e, "%d%s" % (v + 1, suf), "const")
            if v > 0:
                add(s, e, "%d%s" % (v - 1, suf), "const")
        if kind == "id" and t in ("if", "while") and i + 1 < len(toks) and toks[i + 1][3] == "(":
            j = match_paren(toks, i + 1)
            if j < 0:
                continue
            cs, ce = toks[i + 1][1], toks[j][0]
            if "cond" in ops:
                if t == "if":
                    add(cs, ce, "1", "cond")
                add(cs, ce, "0", "cond")
            if "delchk" in ops and t == "if" and j + 1 < len(toks):
                k = j + 1
                brace = toks[k][3] == "{"
                if brace:
                    k += 1
                if k < len(toks) and toks[k][3] in ("return", "break", "continue", "goto"):
                    m = k
                    while m < len(toks) and toks[m][3] != ";":
                        if toks[m][3] in ("{", "}"):
                            m = -1
                            break
                        m += 1
                    if m > 0 and m < len(toks):
                        end = toks[m][1]
                        ok = True
                        if brace:
                            ok = m + 1 < len(toks) and toks[m + 1][3] == "}"
                            end = toks[m + 1][1] if ok else end
                        # a following `else` would dangle: skip those
                        nxt = None
                        for q in range(len(toks)):
                            if toks[q][0] >= end:
                                nxt = toks[q][3]
                                break
                        if ok and nxt != "else":
                            add(s, end, ";", "delchk")
        if "ret" in ops and kind == "id" and t == "return":
            k = i + 1
            m = k
            while m < len(toks) and toks[m][3] != ";":
                m += 1
            if m >= len(toks):
                continue
            expr = "".join(x[3] for x in toks[k:m])
            es, ee = (toks[k][0], toks[m - 1][1]) if m > k else (None, None)
            if es is None:
                continue
            if re.fullmatch(r"-\d+[uUlL]*", expr) or re.fullmatch(r"-\(\d+\)", expr):
                add(es, ee, "0", "ret")
            elif re.fullmatch(r"0[uUlL]*", expr):
                add(es, ee, "-1", "ret")
            elif re.fullmatch(r"[1-9]\d*[uUlL]*", expr):
                add(es, ee, "0", "ret")
            elif expr == "true":
                add(es, ee, "false", "ret")
            elif expr == "false":
                add(es, ee, "true", "ret")
    # dedupe by the resulting text, keep source order
    seen, out = set(), []
    for m in sorted(muts, key=lambda x: (x["start"], x["end"], x["op"], x["repl"])):
        key = (m["start"], m["end"], m["repl"])
        if key in seen or m["orig"] == m["repl"]:
            continue
        seen.add(key)
        out.append(m)
    for n_, m in enumerate(out):
        m["id"] = n_
    return out


def apply(src, m):
    return src[:m["start"]] + m["repl"] + src[m["end"]:]

# ---------------------------------------------------------------- running


def copy_tree(dst):
    ign = shutil.ignore_patterns("*.o", "*.elf", "*.bin", "*.iso", "__pycache__", ".git")
    os.makedirs(dst, exist_ok=True)
    for d in ("kernel", "tests"):
        s = os.path.join(ROOT, d)
        if os.path.isdir(s) and not os.path.exists(os.path.join(dst, d)):
            shutil.copytree(s, os.path.join(dst, d), ignore=ign, symlinks=True)
    return dst


def run_cmd(cmd, cwd, timeout, env):
    t0 = time.time()
    p = subprocess.Popen(cmd, shell=True, cwd=cwd, env=env, stdout=subprocess.DEVNULL,
                         stderr=subprocess.DEVNULL, start_new_session=True)
    try:
        rc = p.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(p.pid, signal.SIGKILL)
        except OSError:
            pass
        p.wait()
        return None, time.time() - t0
    return rc, time.time() - t0


def run_target(name, target, cmd, lines, ops, jobs, workers, build_code, out_dir, phase,
               timeout_factor, max_mutants, verbose):
    src_path = os.path.join(ROOT, target)
    with open(src_path) as f:
        src = f.read()
    muts = gen_mutants(src, ops, lines)
    if max_mutants:
        muts = muts[:max_mutants]
    env = dict(os.environ)
    env["ZXV_MUT_TARGET"] = target
    env["ZXV_OBJ_CACHE"] = os.path.join(out_dir, "objcache")
    # baseline: the unmutated tree must pass, and sets the timeout
    rc, dt = run_cmd(cmd, workers[0], None, env)
    if rc != 0:
        print("[ERROR] %s: the test command fails on the UNMUTATED tree (exit %s): %s"
              % (name, rc, cmd))
        return None
    rc2, dt2 = run_cmd(cmd, workers[0], None, env)  # warm the object cache
    timeout = max(20.0, timeout_factor * min(dt, dt2) + 10.0)
    print("[%s/%s] %d mutants, baseline %.1fs, timeout %.0fs, %d jobs"
          % (name, phase, len(muts), min(dt, dt2), timeout, jobs), flush=True)
    free = list(workers)
    import threading
    lock = threading.Lock()
    done = [0]

    def one(m):
        with lock:
            w = free.pop()
        try:
            path = os.path.join(w, target)
            with open(path, "w") as f:
                f.write(apply(src, m))
            rc, el = run_cmd(cmd, w, timeout, env)
            with open(path, "w") as f:
                f.write(src)
        finally:
            with lock:
                free.append(w)
        if rc is None:
            st = "timeout"
        elif rc == 0:
            st = "survived"
        elif build_code is not None and rc == build_code:
            st = "compile-error"
        else:
            st = "killed"
        m["status"] = st
        m["seconds"] = round(el, 2)
        with lock:
            done[0] += 1
            if verbose or st == "survived":
                print("  [%s] #%d %s:%d %s '%s' -> '%s'" % (st, m["id"], target, m["line"],
                                                          m["op"], m["orig"], m["repl"]),
                      flush=True)
            elif done[0] % 25 == 0:
                print("  ... %d/%d" % (done[0], len(muts)), flush=True)
        return m

    with ThreadPoolExecutor(max_workers=jobs) as ex:
        list(ex.map(one, muts))
    cnt = {k: 0 for k in ("killed", "survived", "timeout", "compile-error")}
    for m in muts:
        cnt[m["status"]] += 1
    valid = cnt["killed"] + cnt["timeout"] + cnt["survived"]
    score = (cnt["killed"] + cnt["timeout"]) / valid if valid else 0.0
    lines_src = src.splitlines()
    res = dict(name=name, target=target, phase=phase, command=cmd, lines=lines, ops=ops,
               counts=cnt, mutants=len(muts), score=round(score, 4), mutant_list=muts)
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, "%s.%s.json" % (name, phase)), "w") as f:
        json.dump(res, f, indent=1)
    with open(os.path.join(out_dir, "%s.%s.txt" % (name, phase)), "w") as f:
        f.write("%s (%s): score %.1f%% = %d killed + %d timeout of %d valid; %d compile-error; "
                "%d mutants\n" % (target, phase, 100 * score, cnt["killed"], cnt["timeout"],
                                  valid, cnt["compile-error"], len(muts)))
        f.write("command: %s\nsurvivors:\n" % cmd)
        for m in muts:
            if m["status"] == "survived":
                f.write("  %s:%d  %-6s '%s' -> '%s'    | %s\n"
                        % (target, m["line"], m["op"], m["orig"], m["repl"],
                           lines_src[m["line"] - 1].strip()[:100]))
    print("[%s/%s] score %.1f%% (killed %d, timeout %d, survived %d, compile-error %d)"
          % (name, phase, 100 * score, cnt["killed"], cnt["timeout"], cnt["survived"],
             cnt["compile-error"]), flush=True)
    return res


SELFTEST_SRC = """#include <stdint.h>
#define LIMIT (10 - 1) /* a < b in a comment */
static const char *msg = "x < y + 1";
int f(int a, int b) {
    if (a < 0) return -1;
    int s = a - b * 2;
    while (s > b) s--;
    return s == 3 ? 0 : 1;
}
"""


def selftest():
    """Check the scanner and the operators on a fixed snippet (no compiler)."""
    ok = True

    def expect(cond, what):
        nonlocal ok
        if not cond:
            ok = False
            print("  [FAIL] mutate.py selftest: " + what)

    m = mask_source(SELFTEST_SRC)
    expect(len(m) == len(SELFTEST_SRC), "masking keeps offsets")
    expect("LIMIT" not in m and "x < y" not in m and "a < b in" not in m,
           "preprocessor lines, comments and strings are masked")
    ms = gen_mutants(SELFTEST_SRC)
    got = {(x["line"], x["op"], x["orig"], x["repl"]) for x in ms}
    want = {(5, "rel", "<", "<="), (5, "delchk", "if (a < 0) return -1;", ";"),
            (5, "cond", "a < 0", "1"), (5, "cond", "a < 0", "0"), (5, "ret", "-1", "0"),
            (5, "const", "0", "1"), (6, "arith", "-", "+"), (6, "arith", "*", "/"),
            (6, "const", "2", "3"), (6, "const", "2", "1"), (7, "rel", ">", ">="),
            (7, "arith", "--", "++"), (7, "cond", "s > b", "0"), (8, "rel", "==", "!=")}
    expect(want <= got, "expected mutants missing: %s" % sorted(want - got))
    expect(not any(x["op"] == "arith" and x["line"] == 5 for x in ms),
           "the unary minus of `return -1` is not mutated as arithmetic")
    expect(not any(x["line"] in (1, 2, 3) for x in ms), "nothing mutated in masked lines")
    expect(not any(x["op"] == "ret" and x["line"] == 8 for x in ms),
           "a non-literal return expression is not negated")
    expect(not any(x["op"] == "cond" and x["repl"] == "1" and x["line"] == 7 for x in ms),
           "a while condition is never replaced by 1 (an endless loop)")
    expect([x["id"] for x in ms] == list(range(len(ms))), "mutants are numbered in order")
    expect(gen_mutants(SELFTEST_SRC) == gen_mutants(SELFTEST_SRC), "generation is deterministic")
    d = [x for x in ms if x["op"] == "delchk"][0]
    expect("if (a < 0)" not in apply(SELFTEST_SRC, d) and "int s = a - b" in apply(SELFTEST_SRC, d),
           "apply() replaces exactly the mutated span")
    print("mutate.py selftest: %d mutants on the snippet, %s" % (len(ms), "ok" if ok else "FAILED"))
    return 0 if ok else 1


def parse_lines(s):
    if not s:
        return None
    if isinstance(s, list):
        return [tuple(x) for x in s]
    out = []
    for part in s.split(","):
        a, _, b = part.partition("-")
        out.append((int(a), int(b or a)))
    return out


def main(argv):
    ap = argparse.ArgumentParser(description="deterministic C source mutation harness")
    ap.add_argument("--target")
    ap.add_argument("--test", help="build+run command, run from the tree root")
    ap.add_argument("--lines", help="only mutate these line ranges, e.g. 10-40,55-60")
    ap.add_argument("--ops", default=",".join(OPS_ALL))
    ap.add_argument("--config")
    ap.add_argument("--only", help="comma-separated target names from the config")
    ap.add_argument("--phase", default="after", choices=["before", "after", "both"])
    ap.add_argument("--ci", action="store_true",
                    help="run the config's CI subset and fail below its baselines")
    ap.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2)))
    ap.add_argument("--build-code", type=int, default=3,
                    help="exit status of the test command meaning 'did not build' (tiers.py: 3)")
    ap.add_argument("--timeout-factor", type=float, default=4.0)
    ap.add_argument("--max-mutants", type=int, default=0)
    ap.add_argument("--list", action="store_true", help="print the mutants and exit")
    ap.add_argument("--out", default=None, help="results directory")
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("--selftest", action="store_true", help="check the mutation operators")
    a = ap.parse_args(argv)
    if a.selftest:
        return selftest()
    ops = a.ops.split(",")

    jobs = []  # (name, target, lines, phase, cmd)
    cfg = None
    if a.config:
        with open(a.config) as f:
            cfg = json.load(f)
        names = None
        if a.ci:
            names = cfg["ci"]["targets"]
        elif a.only:
            names = a.only.split(",")
        for t in cfg["targets"]:
            if names and t["name"] not in names:
                continue
            phases = ["after"] if a.ci else (["before", "after"] if a.phase == "both"
                                             else [a.phase])
            for ph in phases:
                if not t.get(ph):
                    print("[skip] %s/%s: %s" % (t["name"], ph, t.get(ph + "_note", "no command")))
                    continue
                jobs.append((t["name"], t["target"], parse_lines(t.get("lines")), ph, t[ph]))
    elif a.target and a.test:
        jobs.append((os.path.splitext(os.path.basename(a.target))[0], a.target,
                     parse_lines(a.lines), "run", a.test))
    else:
        ap.error("give --target and --test, or --config")

    if a.list:
        for name, target, lines, ph, cmd in jobs:
            with open(os.path.join(ROOT, target)) as f:
                src = f.read()
            ms = gen_mutants(src, ops, lines)
            print("%s: %d mutants" % (target, len(ms)))
            for m in ms:
                print("  #%d %d %s '%s' -> '%s'" % (m["id"], m["line"], m["op"], m["orig"],
                                                   m["repl"]))
        return 0

    out_dir = os.path.abspath(a.out or tempfile.mkdtemp(prefix="zxv_mutation_"))
    os.makedirs(out_dir, exist_ok=True)
    scratch = tempfile.mkdtemp(prefix="zxv_mut_trees_")
    workers = [copy_tree(os.path.join(scratch, "w%d" % i)) for i in range(a.jobs)]
    results = []
    try:
        for name, target, lines, ph, cmd in jobs:
            r = run_target(name, target, cmd, lines, ops, a.jobs, workers, a.build_code, out_dir,
                           ph, a.timeout_factor, a.max_mutants, a.verbose)
            if r is None:
                return 2
            results.append(r)
    finally:
        shutil.rmtree(scratch, ignore_errors=True)
    print("\nresults in %s" % out_dir)
    print("%-16s %-7s %7s %7s %8s %8s %8s" % ("target", "phase", "score", "killed", "timeout",
                                              "survived", "cc-err"))
    for r in results:
        c = r["counts"]
        print("%-16s %-7s %6.1f%% %7d %8d %8d %8d" % (r["name"], r["phase"], 100 * r["score"],
                                                     c["killed"], c["timeout"], c["survived"],
                                                     c["compile-error"]))
    if a.ci:
        bad = 0
        for r in results:
            base = cfg["ci"]["baseline"].get(r["name"])
            if base is not None and r["score"] + 1e-9 < base:
                print("[FAIL] %s mutation score %.1f%% is below the baseline %.1f%%"
                      % (r["name"], 100 * r["score"], 100 * base))
                bad += 1
            else:
                print("[PASS] %s mutation score %.1f%% (baseline %.1f%%)"
                      % (r["name"], 100 * r["score"], 100 * (base or 0)))
        return 1 if bad else 0
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
