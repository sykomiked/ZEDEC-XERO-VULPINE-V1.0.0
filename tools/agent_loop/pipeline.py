#!/usr/bin/env python3
# pipeline.py — the full app-drafting pipeline: every pending app, in
# graph order, drafted on the A6000's GPU, judged by the kernel's own app
# runtime, retried with the verdict in the prompt. One PID; resume-safe.
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""usage: pipeline.py [--attempts 3] [--limit N] [--state FILE]

Env: VLLM_URL + VLLM_MODEL (the GPU backend) and ZXVD_URL + ZXVD_TOKEN
(the testbed judge). Without VLLM_URL it uses llama-cli.

The state file lists the apps already PASSED (status 0): rerunning
resumes. A failed app after --attempts lands in results/ with its last
verdict for review; it does not block the run.
"""
import argparse
import csv
import json
import os
import subprocess
import sys
import time
import urllib.request

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))
sys.path.insert(0, os.path.join(ROOT, "tools/agent_loop"))

VLLM_URL = os.environ.get("VLLM_URL", "")
VLLM_MODEL = os.environ.get("VLLM_MODEL", "Qwen/Qwen2.5-Coder-32B-Instruct-AWQ")
ZXVD_URL = os.environ.get("ZXVD_URL", "")
ZXVD_TOKEN = os.environ.get("ZXVD_TOKEN", "")


def log(msg):
    print("%s %s" % (time.strftime("%H:%M:%S"), msg), flush=True)


def spec_text(app_id, node):
    task = node.get("task", "")
    path = os.path.join(ROOT, "tools/server-tasks", task)
    if task and os.path.exists(path):
        return open(path, errors="replace").read()
    return ""


def lang_rows(app_id):
    out = []
    with open(os.path.join(ROOT, "tools/server-tasks/APP_LANGUAGES_407.csv")) as f:
        for r in csv.DictReader(f):
            if r["app"].strip().lower() == app_id.lower():
                out.append("%s/%s: %s" % (r["language"], r["runtime"], r["feature"]))
    return out


REF = open(os.path.join(ROOT, "kernel/src/apps/banker.zxapp"), errors="replace").read()
REF = "\n".join(REF.split("\n")[:34])


def valid_caps():
    """The kernel's one capability table (zxapp.c R3): the only names a
    needs line may use. The model invents names without this list."""
    src = open(os.path.join(ROOT, "kernel/src/apps/zxapp.c"), errors="replace").read()
    i = src.find("CAPS[] = {")
    j = src.find("};", i)
    names = []
    for line in src[i:j].split("\n")[1:]:
        for tok in line.split("/*")[0].replace('"', " ").replace(",", " ").split():
            if tok and not tok.startswith("/"):
                names.append(tok)
    return names


CAPS = valid_caps()


def few_shots():
    """The real, passing manifests: the best grammar teachers. Two short
    ones (an inline-source shape and a path shape) go in every prompt."""
    out = []
    for name in ("fee_check.zxapp", "brazier.zxapp"):
        path = os.path.join(ROOT, "kernel/src/apps", name)
        if os.path.exists(path):
            txt = open(path, errors="replace").read()
            body = "\n".join(l for l in txt.split("\n") if not l.startswith("#")).strip()
            # the check lines live at the bottom: keep the head AND the tail
            # so the model sees the exact check grammar it keeps failing
            if len(body) > 1400:
                lines = body.split("\n")
                checks = [l for l in lines if l.startswith("check")]
                head = "\n".join(lines[:14])
                body = head + "\n...\n" + "\n".join(checks[:6])
            out.append(body)
    return "\n---\n".join(out)


FEW = few_shots()


def needs_of(app_id, node):
    """The spec's own needs line: machine-extractable, so the model never
    has to guess capabilities."""
    spec = spec_text(app_id, node)
    import re
    m = re.search(r"^needs:\s*(.+)$", spec, re.M)
    if not m:
        return []
    caps = [c.strip().rstrip(".") for c in m.group(1).split(",")]
    return [c for c in caps if c in CAPS]


def criteria_of(app_id):
    """The authored criteria: the generated particulars plus, when it
    exists, the hand-authored entry table (the exact checks)."""
    parts = []
    for name in ("%s.md" % app_id, "%s.authored.md" % app_id):
        path = os.path.join(ROOT, "tools/server-tasks/criteria", name)
        if os.path.exists(path):
            parts.append(open(path, errors="replace").read())
    return "\n\n".join(parts)


def skeleton(app_id, node):
    """The deterministic parts of the manifest, pre-filled and correct:
    the header, the matrix, the needs. The model fills provides, logic
    and checks into a grammar-annotated scaffold."""
    # the graph's slot IS the matrix layer (the spec's "Matrix: layer N");
    # the graph's layer is the L1/L2/L3 icon group, not the matrix
    sk = ["zxapp 1", "id %s" % app_id, "matrix %s" % (node.get("slot", 0)),
          "name %s" % app_id.replace("_", " ").title()]
    for c in needs_of(app_id, node):
        sk.append("needs %s" % c)
    sk.append("")
    sk.append("# COMPLETE the manifest below this line. EXACT grammar — every")
    sk.append("# line must match one of these shapes (no other spellings):")
    sk.append("#   provides <entry> num <arg>[,<arg>...]")
    sk.append("#   provides <entry> truth <arg>[,<arg>...]")
    sk.append("#   logic <lang> <entry> <<")
    sk.append("#   <the source for that entry, then a line with only a dot:>")
    sk.append("#   .")
    sk.append("#   check <entry> <arg>=<int>[,<int>...] = <answer>[,<answer>...]")
    sk.append("# Answers: integers, n/d, true, false, neither, or the word both.")
    sk.append("# Each check line names ONE input group and the answers, one per")
    sk.append("# input row. Provide a check for EVERY provided entry.")
    return "\n".join(sk)


def prompt_for(app_id, node, verdict, last_text=""):
    spec = spec_text(app_id, node)
    lang = lang_rows(app_id)
    p = (
        "You are the drafting engine for ZEDEC XERO VULPINE (ZXV), a freestanding C11 "
        "operating system. COMPLETE the manifest skeleton below (its header, matrix and "
        "needs are already correct — keep them exactly) for the application '%s' "
        "(layer %s, slot %s). Replace the # guidance lines with real provides, logic "
        "and check lines; output the whole manifest starting with 'zxapp 1'.\n"
        "--- skeleton ---\n%s\n--- end skeleton ---\n"
        "NO C code, NO explanations, NO markdown fences — the raw manifest only. "
        "It is parsed and TESTED by a running node; one syntax error fails it.\n"
        "Reference manifests (follow exactly; the second is a real composed "
        "app that passes the node's checks):\n---\n%s\n---\n%s\n---\n"
        "Rules:\n"
        "1. Header: zxapp 1 / id / name / version / slot / layer.\n"
        "2. needs lines for every capability the spec names.\n"
        "3. provides + logic lines in the assigned languages. CRITICAL: this app's "
        "blocks DO NOT EXIST yet — every logic entry MUST use an INLINE source "
        "(logic <lang> <entry> << then the source, then a line with only '.'). "
        "NEVER write a path like apps/blocks/... — the loader will fail.\n"
        "4. check lines for EVERY provided entry. THE CRITERIA'S INSTANTIATED "
        "TABLE BELOW CARRIES THE EXACT CHECK FOR EACH PATTERN — copy it verbatim, "
        "changing only the entry name to yours and the input names to your "
        "provides' arg names in order. Do NOT invent the inputs or the answers. "
        "Shape (verbatim from the criteria): "
        "EXACTLY this grammar: check <entry> <input>=<v>[,<v>...] <input>=<v>... "
        "= <answer>[,<answer>...]. The inputs' values are INTEGERS only; the "
        "answer is the rows comma-separated: whole numbers, exact fractions "
        "'n/d', or the truths true/false/neither, or the one word 'both' (the "
        "languages disagree). Example: check heat_ok temp=0,450,451 = "
        "true,true,false\n"
        "5. needs lines may use ONLY these capabilities (no invented names): %s.\n"
        "6. TRUTH entries are paraconsistent: their answers and check values are "
        "true, false, both or neither (lpres) — NEVER 0/1 booleans. A claim that "
        "holds and is contradicted is BOTH; one with no evidence is NEITHER. "
        "Numeric entries answer exact rationals.\n"
        "Assigned languages: %s.\n"
        "The wave spec (the authority):\n---\n%s\n---\n"
        "The authored criteria — the exact entry tables and checks (follow "
        "these precisely; they were written for this app):\n---\n%s\n---\n"
        % (app_id, node.get("layer"), node.get("slot"), skeleton(app_id, node), REF, FEW,
           ", ".join(CAPS), ", ".join(lang), spec[:3000], criteria_of(app_id)[:4000])
    )
    if verdict:
        p += ("\nYour previous attempt failed the node's test with:\n%s\n" % verdict)
        # name the offending line's own text: the strongest fix signal
        import re
        m = re.search(r"line (\d+)", verdict)
        if m and last_text:
            ln = int(m.group(1))
            lines = last_text.split("\n")
            if 0 < ln <= len(lines):
                p += ("The manifest's line %d is:\n%s\n" % (ln, lines[ln - 1]))
        p += "Fix exactly that and return the full corrected manifest.\n"
    return p


def vllm(prompt, tokens):
    body = json.dumps({"model": VLLM_MODEL,
                       "messages": [{"role": "user", "content": prompt}],
                       "max_tokens": tokens, "temperature": 0.6, "top_p": 0.9}).encode()
    req = urllib.request.Request(VLLM_URL.rstrip("/") + "/v1/chat/completions", data=body,
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=3600) as r:
        d = json.loads(r.read())
    return d["choices"][0]["message"]["content"]


def judge(text):
    i = text.find("zxapp 1")
    if i < 0:
        return (1, "no manifest in the draft")
    man = text[i:i + 8192]
    j = man.find("```")
    if j >= 0:
        man = man[:j]
    req = urllib.request.Request(ZXVD_URL.rstrip("/") + "/api/app", data=man.encode(),
                                 method="POST")
    req.add_header("X-ZXV-Token", ZXVD_TOKEN)
    req.add_header("Host", "127.0.0.1:8799")
    with urllib.request.urlopen(req, timeout=60) as r:
        d = r.read()
    st = d[0] if d else 9
    n = (d[1] << 8 | d[2]) if len(d) >= 3 else 0
    return (st, d[3:3 + n].decode(errors="replace"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--attempts", type=int, default=3)
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--state", default=os.path.join(ROOT, "tools/agent_loop/pipeline.state"))
    args = ap.parse_args()
    if not VLLM_URL or not ZXVD_URL or not ZXVD_TOKEN:
        log("need VLLM_URL, ZXVD_URL and ZXVD_TOKEN")
        return 1
    g = "/tmp/appgraph_pipeline.json"
    subprocess.run([sys.executable, os.path.join(ROOT, "tools/apps/app_graph.py"),
                    "--json", g], check=True, capture_output=True)
    graph = json.load(open(g))
    nodes, order = graph["nodes"], graph.get("order", [])
    done = set()
    if os.path.exists(args.state):
        done = set(x.strip() for x in open(args.state) if x.strip())
    passed = failed = skipped = 0
    n_run = 0
    for o in order:
        node = nodes.get("app:" + o) or nodes.get(o)
        if not node or node.get("done") or node.get("kind") != "app" or o in done:
            skipped += 1
            continue
        if args.limit and n_run >= args.limit:
            break
        n_run += 1
        verdict = ""
        last_text = ""
        ok = False
        for attempt in range(1, args.attempts + 1):
            t0 = time.time()
            try:
                text = vllm(prompt_for(o, node, verdict, last_text), 2400)
            except Exception as e:
                log("%s attempt %d: vllm error %s" % (o, attempt, e))
                time.sleep(10)
                continue
            try:
                st, verdict = judge(text)
            except Exception as e:
                log("%s attempt %d: testbed error %s" % (o, attempt, e))
                continue
            log("%s attempt %d: %s (%.0fs)" % (o, attempt,
                "PASSED" if st == 0 else "testbed[%d]: %s" % (st, verdict[:90]),
                time.time() - t0))
            last_text = text
            if st == 0:
                ok = True
                break
        out = os.path.join(ROOT, "tools/agent_loop/results", "app_%s.txt" % o)
        with open(out, "w") as f:
            f.write("# app %s: %s\n# verdict: %s\n\n%s\n" %
                    (o, "PASSED" if ok else "FAILED", verdict, text))
        if ok:
            passed += 1
            with open(args.state, "a") as f:
                f.write(o + "\n")
        else:
            failed += 1
        log("progress: %d passed, %d failed, %d skipped" % (passed, failed, skipped))
    log("PIPELINE DONE: %d passed, %d failed, %d skipped" % (passed, failed, skipped))
    return 0


if __name__ == "__main__":
    sys.exit(main())