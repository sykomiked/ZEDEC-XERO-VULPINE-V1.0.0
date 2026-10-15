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

--jobs N builds up to N apps of a wave at once, each in its own git worktree
(build/wave_loop/trees/) made from the same commit. Apps of one wave are
independent except where an app's layer leans on one built before it in the
same wave (MATRIX_ORDER.tsv): such an app waits for the next round, which
starts from the merged tree. Each round's T apps are merged into the main
tree in task order, the generated docs are made again there and each app is
judged again beside the others; one that is no longer T (two apps claimed
the same concept, say) is built again, one at a time, on the merged tree.
So the result does not depend on which app finished first. A wave is done
before the next starts.

The tree must be clean when the loop starts: everything it saves, commits or
puts back is then the builder's work. It never pushes.
"""
import argparse
import concurrent.futures
import json
import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
TASKS = os.path.join(ROOT, "tools", "server-tasks")
OUT = os.path.join(ROOT, "build", "wave_loop")
DOCS = ("docs/APP_BLOCKS.md", "docs/APP_LANGUAGE_COVERAGE.md")


def apps(root):
    return os.path.join(root, "kernel", "src", "apps")


def git(root, *args, check=True, stdin=None):
    p = subprocess.run(["git", "-C", root, *args], capture_output=True, text=True, input=stdin)
    if check and p.returncode:
        raise SystemExit(f"wave_loop: git {' '.join(args)}: {p.stderr.strip()}")
    return p.stdout if check else p


def dirty(root):
    """Changed and new paths, build/ aside (git ignores it)."""
    return [x[3:] for x in git(root, "status", "--porcelain", "--untracked-files=all").splitlines()]


def put_back(root):
    """The tree as HEAD has it. Safe: the tree was clean when the loop began."""
    new = [x[3:] for x in git(root, "status", "--porcelain", "--untracked-files=all").splitlines()
           if x.startswith("??")]
    git(root, "reset", "-q", "--hard", "HEAD")
    for p in new:
        git(root, "clean", "-fdq", "--", p)


def save(root, app_id, why):
    """The builder's work as a patch beside why it was kept back."""
    held = os.path.join(OUT, "held")
    os.makedirs(held, exist_ok=True)
    git(root, "add", "-A", "--", ".")
    patch = git(root, "diff", "--cached", "--binary")
    git(root, "reset", "-q")
    with open(os.path.join(held, f"{app_id}.patch"), "w", encoding="utf-8") as f:
        f.write(patch)
    with open(os.path.join(held, f"{app_id}.why.txt"), "w", encoding="utf-8") as f:
        f.write(why)
    put_back(root)
    return os.path.relpath(os.path.join(held, f"{app_id}.patch"), ROOT)


def verdict(root, app_id, runs):
    path = os.path.join(OUT, f"{app_id}.verdict.json")
    gate = os.path.join(root, "tools", "apps", "app_gate.py")
    p = subprocess.run([sys.executable, gate, "--app", app_id, "--verdict", "--runs", str(runs),
                        "--json", path], cwd=root, capture_output=True, text=True)
    if not os.path.exists(path):
        return {"verdict": "N", "action": "requeue", "fits": [],
                "functions": ["the gate gave no verdict: " + (p.stderr or p.stdout).strip()[-2000:]]}
    with open(path, encoding="utf-8") as f:
        r = json.load(f)["apps"][app_id]
    os.remove(path)
    return r


def regenerate(root):
    """The two generated docs every new app changes; their failures, if any."""
    bad = []
    for tool in ("lang_coverage.py", "concepts.py"):
        p = subprocess.run([sys.executable, os.path.join(root, "tools", "apps", tool)], cwd=root,
                           capture_output=True, text=True)
        if p.returncode:
            out = [x for x in (p.stdout + p.stderr).strip().splitlines() if not x.startswith("reuse ")]
            bad += [f"{tool}: {x}" for x in out[-8:]]
    return bad


def judge(root, app_id, runs):
    """The docs regenerated, then the gate's verdict; a doc that fails makes T into F."""
    docs = regenerate(root)
    r = verdict(root, app_id, runs)
    if docs and r["verdict"] in ("T", "B"):
        r["fits"] += docs
        if r["verdict"] == "T":
            r["verdict"], r["action"] = "F", "repair"
    return r


def reasons(r):
    return [f"fits: {x}" for x in r["fits"]] + [f"functions: {x}" for x in r["functions"]]


def more_context(root, task):
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
        man = os.path.join(apps(root), f"{p}.zxapp")
        if os.path.exists(man):
            out += [f"## The manifest of {p}, which this app may use", "", "```",
                    open(man, encoding="utf-8").read().rstrip(), "```", ""]
    return out


def context(root, task, app_id, attempt, last):
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
            lines += more_context(root, task)
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    return path


def build(root, task, app_id, a, last=None):
    """One app in the tree at root: (verdict, attempts, note); T is committed there."""
    for attempt in range(1, a.tries + 1):
        ctx = context(root, task, app_id, attempt, last)
        cmd = a.runner.format(task=task, context=ctx, id=app_id, attempt=attempt)
        try:
            subprocess.run(cmd, shell=True, cwd=root, timeout=a.timeout)
        except subprocess.TimeoutExpired:
            pass  # whatever it wrote is judged like any other attempt
        last = judge(root, app_id, a.runs)
        v = last["verdict"]
        if v == "T":
            git(root, "add", "-A", "--", ".")
            git(root, "commit", "-q", "-m", f"App {app_id}: verdict T, fits and functions "
                f"(wave loop, attempt {attempt})")
            return "T", attempt, git(root, "rev-parse", "--short", "HEAD").strip()
        if v == "B":
            return "B", attempt, save(root, app_id, "\n".join(reasons(last)) + "\n")
        if v == "N":
            put_back(root)
    return last["verdict"], a.tries, save(root, app_id, "\n".join(reasons(last)) + "\n")


def matrix():
    """MATRIX_ORDER.tsv: id -> (position, the slots it leans on); slot -> id."""
    pos, slot_id = {}, {}
    path = os.path.join(TASKS, "MATRIX_ORDER.tsv")
    if os.path.exists(path):
        for line in open(path, encoding="utf-8"):
            c = line.rstrip("\n").split("\t")
            if line.startswith("#") or c[0] == "position" or len(c) < 7:
                continue
            slot_id[int(c[2], 10)] = c[3]
            pos[c[3]] = (int(c[0], 10), [] if c[6] == "-" else [int(x, 10) for x in c[6].split(",")])
    return pos, slot_id


def levels(todo):
    """The wave's apps in rounds: an app waits for the apps of its wave that its layer leans
    on and that are built before it (MATRIX_ORDER.tsv); every other pair is independent."""
    pos, slot_id = matrix()
    ids = [i for _, i in todo]
    wait = {}
    for _, i in todo:
        p, lean = pos.get(i, (0, []))
        wait[i] = {slot_id[s] for s in lean if slot_id.get(s) in ids and pos[slot_id[s]][0] < p}
    out, done = [], set()
    while len(done) < len(todo):
        level = [(t, i) for t, i in todo if i not in done and wait[i] <= done]
        out.append(level)
        done |= {i for _, i in level}
    return out


def merge(app_id, base, sha, a):
    """A worktree's T into the main tree, the generated docs made here, judged again here."""
    patch = git(ROOT, "diff", "--binary", base, sha, "--", ".",
                *[f":(exclude){d}" for d in DOCS])
    p = git(ROOT, "apply", "--index", "-", check=False, stdin=patch)
    if p.returncode:
        put_back(ROOT)
        return {"verdict": "F", "action": "repair", "fits": [],
                "functions": ["it did not merge with the apps built beside it: " + p.stderr.strip()]}
    r = judge(ROOT, app_id, a.runs)
    if r["verdict"] == "T":
        git(ROOT, "add", "-A", "--", ".")
        git(ROOT, "commit", "-q", "-m", f"App {app_id}: verdict T, fits and functions "
            "(wave loop, built in parallel)")
    else:
        r["functions"].append("it was T alone and is not T beside the apps built with it")
        put_back(ROOT)
    return r


def wave_parallel(todo, a):
    """Build the wave's apps a.jobs at a time, each in its own worktree from the same commit,
    then merge them in task order, so the result does not depend on which finished first.
    An app that is not T after its merge is built again on the merged tree."""
    res, again = {}, []
    for level in levels(todo):
        base = git(ROOT, "rev-parse", "HEAD").strip()
        trees = {}
        for _, i in level:
            wt = os.path.join(OUT, "trees", i)
            if os.path.exists(wt):
                git(ROOT, "worktree", "remove", "--force", wt)
            git(ROOT, "worktree", "add", "-q", "--detach", wt, base)
            trees[i] = wt
        with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as pool:
            jobs = {i: pool.submit(build, trees[i], t, i, a) for t, i in level}
            built = {i: f.result() for i, f in jobs.items()}
        for t, i in level:  # task order, whatever order they finished in
            v, n, note = built[i]
            if v == "T":
                r = merge(i, base, git(trees[i], "rev-parse", "HEAD").strip(), a)
                if r["verdict"] == "T":
                    note = git(ROOT, "rev-parse", "--short", "HEAD").strip()
                else:
                    again.append((t, i, n, r))
                    continue
            res[i] = (v, n, note)
        for wt in trees.values():
            git(ROOT, "worktree", "remove", "--force", wt)
    for t, i, n, r in again:  # one at a time, on the merged tree
        v, m, note = build(ROOT, t, i, a, last=r)
        res[i] = (v, n + m, note)
    return res


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--waves", required=True, help="the waves to build, e.g. 1-11 or 3,4")
    ap.add_argument("--runner", required=True,
                    help="the builder, a shell command; {task} {context} {id} {attempt} are filled in")
    ap.add_argument("--tries", type=int, default=4, help="attempts per app before it is set aside")
    ap.add_argument("--runs", type=int, default=2, help="test runs the gate compares (witnesses)")
    ap.add_argument("--timeout", type=int, default=3600, help="seconds one builder run may take")
    ap.add_argument("--jobs", type=int, default=1,
                    help="apps of one wave built at once, each in its own worktree")
    a = ap.parse_args()
    waves = []
    for part in a.waves.split(","):
        lo, _, hi = part.partition("-")
        waves += range(int(lo), int(hi or lo) + 1)
    if dirty(ROOT):
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
        res, todo = {}, []
        for f in sorted(os.listdir(d)):
            m = re.fullmatch(r"\d{3}-([a-z0-9_]+)\.md", f)
            if not m:
                continue
            app_id = m.group(1)
            if os.path.exists(os.path.join(apps(ROOT), f"{app_id}.zxapp")) and \
                    verdict(ROOT, app_id, a.runs)["verdict"] == "T":
                res[app_id] = ("T", 0, "already built")
            else:
                todo.append((os.path.join(d, f), app_id))
        if a.jobs > 1:
            res.update(wave_parallel(todo, a))
        else:
            for t, i in todo:
                res[i] = build(ROOT, t, i, a)
        rows = ["app\tverdict\tattempts\tresult"]
        for f in sorted(os.listdir(d)):
            m = re.fullmatch(r"\d{3}-([a-z0-9_]+)\.md", f)
            if not m:
                continue
            app_id = m.group(1)
            v, n, note = res[app_id]
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
