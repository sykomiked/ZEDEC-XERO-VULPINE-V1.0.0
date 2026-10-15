#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""The repair loop: build the app waves by themselves, inside out, and bring
back to people only what the gate cannot decide.

For each wave given (tools/server-tasks/wave<N>/, in order) and each task in
it (NNN-<id>.md, in file order), the loop runs the builder (--runner, a shell
command; {task}, {context}, {id} and {attempt} are filled in), then asks the
gate for the app's verdict (app_gate.py --app <id> --verdict) and acts on it:

  T  fits and functions: the app's files are committed with the two docs
     every new app changes (lang_coverage.py and concepts.py regenerate
     them; a failure of either makes the attempt F), and the next task
     starts from them.
  F  a clear failure: the builder runs again on its own work, the context
     file carrying what failed.
  N  nothing usable: the builder starts again from a clean tree, the
     context carrying more: the task's matrix line, the parts it uses (their
     manifests), the test guide, and why the last attempt gave nothing.
  B  the witnesses contradict each other: held. The work is saved as a patch
     under build/wave_loop/held/, the tree is put back, and the app is listed
     for a person; the loop goes on to the next task.

An app still F or N after --tries attempts is saved the same way and listed
as unfinished; the next run of the loop takes it up again (an app already
committed and still T is skipped). Each wave's verdicts are written to
build/wave_loop/wave<N>.tsv, and the run's to build/wave_loop/report.md.

The tree must be clean when the loop starts: everything it saves, commits or
puts back is then the builder's work. It never pushes.
"""
import argparse
import json
import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
TASKS = os.path.join(ROOT, "tools", "server-tasks")
APPS = os.path.join(ROOT, "kernel", "src", "apps")
OUT = os.path.join(ROOT, "build", "wave_loop")
GATE = os.path.join(ROOT, "tools", "apps", "app_gate.py")


def git(*args, check=True):
    p = subprocess.run(["git", "-C", ROOT, *args], capture_output=True, text=True)
    if check and p.returncode:
        raise SystemExit(f"wave_loop: git {' '.join(args)}: {p.stderr.strip()}")
    return p.stdout


def dirty():
    """Changed and new paths, build/ aside (git ignores it)."""
    return [x[3:] for x in git("status", "--porcelain", "--untracked-files=all").splitlines()]


def put_back():
    """The tree as HEAD has it. Safe: the tree was clean when the loop began."""
    new = [x[3:] for x in git("status", "--porcelain", "--untracked-files=all").splitlines()
           if x.startswith("??")]
    git("reset", "-q", "--hard", "HEAD")
    for p in new:
        git("clean", "-fdq", "--", p)


def save(app_id, why):
    """The builder's work as a patch beside why it was kept back."""
    held = os.path.join(OUT, "held")
    os.makedirs(held, exist_ok=True)
    git("add", "-A", "--", ".")
    patch = git("diff", "--cached", "--binary")
    git("reset", "-q")
    with open(os.path.join(held, f"{app_id}.patch"), "w", encoding="utf-8") as f:
        f.write(patch)
    with open(os.path.join(held, f"{app_id}.why.txt"), "w", encoding="utf-8") as f:
        f.write(why)
    put_back()
    return os.path.relpath(os.path.join(held, f"{app_id}.patch"), ROOT)


def verdict(app_id, runs):
    path = os.path.join(OUT, f"{app_id}.verdict.json")
    p = subprocess.run([sys.executable, GATE, "--app", app_id, "--verdict", "--runs", str(runs),
                        "--json", path], cwd=ROOT, capture_output=True, text=True)
    if not os.path.exists(path):
        return {"verdict": "N", "action": "requeue", "fits": [],
                "functions": ["the gate gave no verdict: " + (p.stderr or p.stdout).strip()[-2000:]]}
    with open(path, encoding="utf-8") as f:
        r = json.load(f)["apps"][app_id]
    os.remove(path)
    return r


def regenerate():
    """The two generated docs every new app changes; their failures, if any."""
    bad = []
    for tool in ("lang_coverage.py", "concepts.py"):
        p = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "apps", tool)], cwd=ROOT,
                           capture_output=True, text=True)
        if p.returncode:
            out = [x for x in (p.stdout + p.stderr).strip().splitlines() if not x.startswith("reuse ")]
            bad += [f"{tool}: {x}" for x in out[-8:]]
    return bad


def reasons(r):
    return [f"fits: {x}" for x in r["fits"]] + [f"functions: {x}" for x in r["functions"]]


def more_context(task):
    """For N: what the builder needs to start over with more to go on."""
    text = open(task, encoding="utf-8").read()
    out = ["## What to read again", "",
           "- `tools/server-tasks/APP_WAVE_SPEC.md` (Done list, Inside out, never outward)",
           "- `tools/server-tasks/APP_TESTS.md` (the two test files, a worked pair)",
           "- `docs/APP_BLOCKS.md` and `kernel/src/apps/CONCEPTS.tsv` (what already exists)", ""]
    m = re.search(r"^Matrix: .*$", text, re.M)
    if m:
        out += ["## This app's place in the grid", "", m.group(0), ""]
    parts = sorted(set(re.findall(r"`use ([a-z0-9_]+) 1`", m.group(0) if m else "")))
    for p in parts:
        man = os.path.join(APPS, f"{p}.zxapp")
        if os.path.exists(man):
            out += [f"## The manifest of {p}, which this app may use", "", "```",
                    open(man, encoding="utf-8").read().rstrip(), "```", ""]
    return out


def context(task, app_id, attempt, last):
    path = os.path.join(OUT, f"{app_id}.context.md")
    lines = [f"# {app_id}: attempt {attempt}", "",
             f"The task: `{os.path.relpath(task, ROOT)}`. Deliver the app so that "
             f"`python3 tools/apps/app_gate.py --app {app_id} --verdict` says T.", ""]
    if last:
        v = last["verdict"]
        lines += [f"## The last attempt's verdict: {v}", ""]
        if v == "F":
            lines += ["Your files are still in the tree. Fix what failed, nothing else:", ""]
        else:
            lines += ["Nothing usable was kept; the tree is back as it was. Start again:", ""]
        lines += [f"- {x}" for x in reasons(last)] + [""]
        if v == "N":
            lines += more_context(task)
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    return path


def build(task, app_id, a):
    """One app: (verdict, attempts, note)."""
    last = None
    for attempt in range(1, a.tries + 1):
        ctx = context(task, app_id, attempt, last)
        cmd = a.runner.format(task=task, context=ctx, id=app_id, attempt=attempt)
        try:
            subprocess.run(cmd, shell=True, cwd=ROOT, timeout=a.timeout)
        except subprocess.TimeoutExpired:
            pass  # whatever it wrote is judged like any other attempt
        docs = regenerate()
        last = verdict(app_id, a.runs)
        if docs and last["verdict"] in ("T", "B"):
            last["fits"] += docs
            if last["verdict"] == "T":
                last["verdict"], last["action"] = "F", "repair"
        v = last["verdict"]
        if v == "T":
            git("add", "-A", "--", ".")
            git("commit", "-q", "-m", f"App {app_id}: verdict T, fits and functions "
                f"(wave loop, attempt {attempt})")
            return "T", attempt, git("rev-parse", "--short", "HEAD").strip()
        if v == "B":
            return "B", attempt, save(app_id, "\n".join(reasons(last)) + "\n")
        if v == "N":
            put_back()
    return last["verdict"], a.tries, save(app_id, "\n".join(reasons(last)) + "\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--waves", required=True, help="the waves to build, e.g. 1-11 or 3,4")
    ap.add_argument("--runner", required=True,
                    help="the builder, a shell command; {task} {context} {id} {attempt} are filled in")
    ap.add_argument("--tries", type=int, default=4, help="attempts per app before it is set aside")
    ap.add_argument("--runs", type=int, default=2, help="test runs the gate compares (witnesses)")
    ap.add_argument("--timeout", type=int, default=3600, help="seconds one builder run may take")
    a = ap.parse_args()
    waves = []
    for part in a.waves.split(","):
        lo, _, hi = part.partition("-")
        waves += range(int(lo), int(hi or lo) + 1)
    if dirty():
        print("wave_loop: the tree is not clean; commit or set aside the changes first",
              file=sys.stderr)
        return 2
    os.makedirs(OUT, exist_ok=True)
    report = ["# Wave loop report", "", "| wave | app | verdict | attempts | result |",
              "|---|---|---|---|---|"]
    held, unfinished = [], []
    for w in waves:
        d = os.path.join(TASKS, f"wave{w}")
        if not os.path.isdir(d):
            print(f"wave_loop: no tasks for wave {w}", file=sys.stderr)
            return 2
        rows = ["app\tverdict\tattempts\tresult"]
        for f in sorted(os.listdir(d)):
            m = re.fullmatch(r"\d{3}-([a-z0-9_]+)\.md", f)
            if not m:
                continue
            app_id = m.group(1)
            if os.path.exists(os.path.join(APPS, f"{app_id}.zxapp")) and \
                    verdict(app_id, a.runs)["verdict"] == "T":
                v, n, note = "T", 0, "already built"
            else:
                v, n, note = build(os.path.join(d, f), app_id, a)
            print(f"wave {w}: {app_id} {v} after {n} attempt(s): {note}", flush=True)
            rows.append(f"{app_id}\t{v}\t{n}\t{note}")
            report.append(f"| {w} | {app_id} | {v} | {n} | {note} |")
            if v == "B":
                held.append((w, app_id, note))
            elif v != "T":
                unfinished.append((w, app_id, note))
        with open(os.path.join(OUT, f"wave{w}.tsv"), "w", encoding="utf-8") as f:
            f.write("\n".join(rows) + "\n")
    report += ["", "## Held for a person (B: the witnesses contradict each other)", ""]
    report += [f"- wave {w}, {i}: `{p}` (why: `{p[:-6]}.why.txt`)" for w, i, p in held] or ["- none"]
    report += ["", "## Unfinished (still F or N; the next run takes them up again)", ""]
    report += [f"- wave {w}, {i}: `{p}`" for w, i, p in unfinished] or ["- none"]
    with open(os.path.join(OUT, "report.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(report) + "\n")
    print(f"wave_loop: {len(held)} held, {len(unfinished)} unfinished; build/wave_loop/report.md")
    return 1 if held or unfinished else 0


if __name__ == "__main__":
    sys.exit(main())
