#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""The repair loop: build the app waves by themselves, inside out, and bring
back to people only what the gate cannot decide.

For each wave given (tools/server-tasks/wave<N>/, in order) and each task in
it (NNN-<id>.md, in file order), the loop runs the builder (--runner, a shell
command; {task}, {context}, {id} and {attempt} are filled in), then asks the
gate for the app's verdict (app_gate.py --app <id> --verdict) and acts on it:

  T  fits and functions: the app's files are committed with what every
     new app changes: its entries' Alexandria slots (build_index.py --assign
     <id> auto auto) and the two generated docs (lang_coverage.py,
     concepts.py); a failure of any of them makes the attempt F. The next
     task starts from them.
  F  a clear failure: the builder runs again on its own work, the context
     file carrying what failed.
  N  nothing usable: the builder starts again from a clean tree, the
     context saying why the last attempt gave nothing.

Every attempt's context file also carries the task's matrix line, the
manifests of the parts it may use, what to read, the test guide with a
whole sample app that passes and the compile rules, the capabilities a
manifest may need, zxm.h (the whole daemon API) and the daemon methods
that exist: wave 1's first server run failed on invented calls, struct
members and signatures, and on warnings -Werror refuses.
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

--waves graph builds by the app graph (tools/apps/app_graph.py) instead of
by wave: each round takes the apps that unlock the most among those whose
parts are all built (tools/server-tasks/BUILD_ORDER.tsv's order), --jobs of
them at once (one when --jobs is 1), and works out the graph again after
the round, so every app accepted changes what comes next. It stops when a
round accepts nothing new, or after --limit apps have been tried. Every T
also regenerates docs/APP_GRAPH.md and BUILD_ORDER.tsv, so the weighted
completion moves in the commit that moves it.

--on-accept CMD runs after each T commit ({id} and {commit} filled in), for
work that should follow an accepted app (pack it, say); each run is listed
in build/wave_loop/accepted.tsv with its exit code. A hook that fails is
reported and does not undo the accept.

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
DOCS = ("docs/APP_BLOCKS.md", "docs/APP_LANGUAGE_COVERAGE.md",
        "kernel/src/alexandria/SLOTS.tsv", "kernel/src/alexandria/alx_slots.h",
        "docs/APP_GRAPH.md", "tools/server-tasks/BUILD_ORDER.tsv")
HOOK = {"cmd": None}  # --on-accept


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


def regenerate(root, app_id):
    """What every new app changes besides its own files: its entries' slots on the
    Alexandria app shelf (build_index.py --assign APP auto auto, so the slot numbers
    follow the order apps are committed in), the two generated docs and the app graph
    (docs/APP_GRAPH.md, BUILD_ORDER.tsv). Their failures, if any."""
    bad = []
    # slots from an earlier attempt may name entries since renamed: assign afresh
    git(root, "checkout", "-q", "HEAD", "--", *DOCS[2:])
    p = subprocess.run([sys.executable, os.path.join(root, "tools", "alexandria", "build_index.py"),
                        "--assign", app_id, "auto", "auto", "--out-hdr"], cwd=root,
                       capture_output=True, text=True)
    if p.returncode and "has no entry without a slot" not in p.stdout + p.stderr:
        bad += [f"build_index.py: {x}" for x in (p.stdout + p.stderr).strip().splitlines()[-8:]]
    for tool in ("lang_coverage.py", "concepts.py", "app_graph.py"):
        p = subprocess.run([sys.executable, os.path.join(root, "tools", "apps", tool)], cwd=root,
                           capture_output=True, text=True)
        if p.returncode:
            out = [x for x in (p.stdout + p.stderr).strip().splitlines() if not x.startswith("reuse ")]
            bad += [f"{tool}: {x}" for x in out[-8:]]
    return bad


def judge(root, app_id, runs):
    """The docs regenerated, then the gate's verdict; a doc that fails makes T into F."""
    docs = regenerate(root, app_id)
    r = verdict(root, app_id, runs)
    if docs and r["verdict"] in ("T", "B"):
        r["fits"] += docs
        if r["verdict"] == "T":
            r["verdict"], r["action"] = "F", "repair"
    return r


def reasons(r):
    return [f"fits: {x}" for x in r["fits"]] + [f"functions: {x}" for x in r["functions"]]


def more_context(root, task):
    """What the builder needs besides the task: where to read, its place, its parts."""
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


def api(root):
    """What the builder compiles against, read from the tree so it is never stale: the
    test guide with its verified sample app and the compile rules (APP_TESTS.md), the
    capabilities a manifest may need, and zxm.h, the whole daemon API of a model."""
    guide = open(os.path.join(root, "tools", "server-tasks", "APP_TESTS.md"), encoding="utf-8").read()
    guide = guide[guide.index("# One test file per app"):]
    src = open(os.path.join(apps(root), "zxapp.c"), encoding="utf-8").read()
    table = src[src.index("static const char *const CAPS[]"):]
    caps = re.findall(r'"([a-z.]+)"', table[:table.index("};")])
    zxm = open(os.path.join(apps(root), "zxm.h"), encoding="utf-8").read()
    zxm = zxm[zxm.index("#define ZXM_REQ_MAX"):zxm.rindex("#endif")].rstrip()
    return ["## The guide to the two test files, with a whole app that passes", "",
            "#" + guide.replace("\n## ", "\n### ").rstrip(), "",
            "## The capabilities a manifest may need (`needs`); no others parse", "",
            ", ".join(f"`{c}`" for c in caps), "",
            "## zxm.h: the whole daemon API a model has (nothing else exists)", "", "```c", zxm,
            "```", ""]


def methods(root):
    """The daemon methods that exist, grouped by their first word: a model may call
    these and no others (an invented call fails the gate)."""
    spec = json.load(open(os.path.join(root, "api", "zxvd.openrpc.json"), encoding="utf-8"))
    groups = {}
    for name in sorted(x["name"] for x in spec["methods"]):
        groups.setdefault(name.split(".")[0], []).append(name)
    out = ["## The daemon methods that exist", "",
           "Call only these (zxm_begin). One you need that is not here goes in your report; "
           "never invent one.", ""]
    out += [f"- {g}: " + ", ".join(f"`{n}`" for n in v) for g, v in sorted(groups.items())]
    return out + [""]


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
    lines += more_context(root, task) + api(root) + methods(root)
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    return path


def accepted(app_id, commit):
    """--on-accept: the hook after a T commit, listed in accepted.tsv; it never undoes it."""
    rc = "-"
    if HOOK["cmd"]:
        p = subprocess.run(HOOK["cmd"].format(id=app_id, commit=commit), shell=True, cwd=ROOT)
        rc = str(p.returncode)
        if p.returncode:
            print(f"wave_loop: --on-accept for {app_id} at {commit} exited {rc} "
                  "(the app stays accepted)", file=sys.stderr, flush=True)
    with open(os.path.join(OUT, "accepted.tsv"), "a", encoding="utf-8") as f:
        f.write(f"{app_id}\t{commit}\t{rc}\n")


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
            sha = git(root, "rev-parse", "--short", "HEAD").strip()
            if root == ROOT:  # a worktree's T is accepted when it merges
                accepted(app_id, sha)
            return "T", attempt, sha
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
    """A worktree's T into the main tree: its own files only; the slots and the generated
    docs are made again here, in merge order, and the app is judged again here."""
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
        accepted(app_id, git(ROOT, "rev-parse", "--short", "HEAD").strip())
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


def graph_rounds(a):
    """--waves graph: rounds of the apps that unlock the most among those ready."""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import app_graph
    tried, out = set(), []
    while not a.limit or len(tried) < a.limit:
        nodes, deps, _, _, _, _, rank, _ = app_graph.analyse()
        ready = [k for k in rank if k[4:] not in tried and
                 all(nodes[d]["done"] for d in deps[k] if d.startswith("app:"))]
        take = ready[:max(1, a.jobs)]
        if a.limit:
            take = take[:a.limit - len(tried)]
        if not take:
            break
        todo = [(os.path.join(TASKS, nodes[k]["task"]), k[4:]) for k in take]
        tried |= {i for _, i in todo}
        res = wave_parallel(todo, a) if a.jobs > 1 else {i: build(ROOT, t, i, a) for t, i in todo}
        out.append((f"g{len(out) + 1}", todo, res))
        if not any(v == "T" for v, _, _ in res.values()):
            break
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--waves", required=True,
                    help="the waves to build, e.g. 1-11 or 3,4; or graph (by unlocks, BUILD_ORDER.tsv)")
    ap.add_argument("--runner", required=True,
                    help="the builder, a shell command; {task} {context} {id} {attempt} are filled in")
    ap.add_argument("--tries", type=int, default=4, help="attempts per app before it is set aside")
    ap.add_argument("--runs", type=int, default=2, help="test runs the gate compares (witnesses)")
    ap.add_argument("--timeout", type=int, default=3600, help="seconds one builder run may take")
    ap.add_argument("--jobs", type=int, default=1,
                    help="apps of one wave built at once, each in its own worktree")
    ap.add_argument("--limit", type=int, default=0, help="--waves graph: at most this many apps")
    ap.add_argument("--on-accept", help="a shell command after each T commit; {id} {commit}")
    a = ap.parse_args()
    HOOK["cmd"] = a.on_accept
    if dirty(ROOT):
        print("wave_loop: the tree is not clean; commit or set aside the changes first",
              file=sys.stderr)
        return 2
    os.makedirs(OUT, exist_ok=True)
    report = ["# Wave loop report", "", "| wave | app | verdict | attempts | result |",
              "|---|---|---|---|---|"]
    held, unfinished = [], []
    if a.waves == "graph":
        batches = graph_rounds(a)
    else:
        waves = []
        for part in a.waves.split(","):
            lo, _, hi = part.partition("-")
            waves += range(int(lo), int(hi or lo) + 1)
        batches = []
        for w in waves:
            d = os.path.join(TASKS, f"wave{w}")
            if not os.path.isdir(d):
                print(f"wave_loop: no tasks for wave {w}", file=sys.stderr)
                return 2
            res, todo, every = {}, [], []
            for f in sorted(os.listdir(d)):
                m = re.fullmatch(r"\d{3}-([a-z0-9_]+)\.md", f)
                if not m:
                    continue
                app_id = m.group(1)
                every.append((os.path.join(d, f), app_id))
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
            batches.append((w, every, res))
    for w, every, res in batches:
        rows = ["app\tverdict\tattempts\tresult"]
        for _, app_id in every:
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
