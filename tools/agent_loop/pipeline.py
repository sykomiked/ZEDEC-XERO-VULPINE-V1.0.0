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


def lang_examples():
    """The real blocks from the built apps: the exact spellings the
    pillars compile. One per language, straight from the tree."""
    out = []
    for name, path in (("trebago", "blocks/fee_check/total.tb"),
                       ("eve", "blocks/fee_check/fee_ok.eve"),
                       ("kron", "blocks/fee_check/fee.kron"),
                       ("sutra", "blocks/fee_check/fee.sutra"),
                       ("myra", "blocks/banker/entry.myra")):
        p = os.path.join(ROOT, "kernel/src/apps", path)
        if os.path.exists(p):
            body = "\n".join(l for l in open(p, errors="replace").read().split("\n")
                             if not l.strip().startswith(("#", "!", ";", "//")))
            out.append("--- %s ---\n%s" % (name, body.strip()[:400]))
    return "\n".join(out)


def checks_of(app_id):
    """The exact check lines the assembled manifest will carry — the model
    must write bodies that satisfy them."""
    node = {"task": ""}
    g = "/tmp/appgraph_chk.json"
    import subprocess as sp
    sp.run([sys.executable, os.path.join(ROOT, "tools/apps/app_graph.py"), "--json", g],
           check=True, capture_output=True)
    import json as _j
    graph = _j.load(open(g))
    node = graph["nodes"].get("app:" + app_id) or graph["nodes"].get(app_id) or {}
    from mk_criteria_v2 import logic_clauses, spec_of
    from patterns import match_patterns
    rows = []
    for c in logic_clauses(spec_of(node.get("task", ""))):
        for pat in match_patterns(c)[:2]:
            if pat not in rows:
                rows.append(pat)
    return "\n".join(p["checks"] for p in rows) or "(no library checks for this app)"


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
        "You are the code engine for ZEDEC XERO VULPINE (ZXV), a freestanding C11 "
        "operating system. The manifest's structure is ALREADY ASSEMBLED for the "
        "application '%s' (matrix %s) — your ONLY job is the LOGIC CODE for each "
        "entry in the criteria's table. Output one section per entry, exactly:\n"
        "=== <entry> (<lang>)\n<the source>\n===\n"
        "Write REAL code in the named language, matching the entry's args and the "
        "check's exact expectations. The languages' real spellings (from the built "
        "apps):\n"
        "  trebago: 'trebago <entry>' then 'in  a, b' / 'out r' / 'r = <expr>' / 'end'\n"
        "  eve: 'eve <app>' then '<domain> = <lo> .. <hi>' then '<block> { <arg>: <domain> <lo> .. <hi> }'\n"
        "  kron: one line: '<answer-for-the-first-row>|<expression with _arg names>'\n"
        "  myra: one line: '^<GLOBAL>(<arg>:<pic>, <arg>:<pic>) = <name>:<pic>!'\n"
        "  sutra: the record (pictures) then the procedure (COMPUTE / IF)\n"
        "THE REAL BLOCKS from the built apps — copy these spellings exactly:\n%s\n"
        "The checks that will judge you (exact inputs and answers):\n%s\n"
        "--- skeleton (the assembled structure, for context) ---\n%s\n---\n"
        "The wave spec (the authority):\n---\n%s\n---\n"
        "The criteria (the exact entry table):\n---\n%s\n---\n"
        % (app_id, node.get("slot"), lang_examples(), checks_of(app_id),
           skeleton(app_id, node), spec[:2500], criteria_of(app_id)[:2500])
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


IO_KEY = os.environ.get("IO_INTEL_KEY", "")
IO_MODEL = os.environ.get("IO_MODEL", "moonshotai/Kimi-K2.7-Code")
IO_URL = "https://api.intelligence.io.solutions/api/v1/chat/completions"


def intelligence(prompt, tokens, tries=4):
    """The IO Intelligence backend: the hosted strong coders (Kimi, GLM,
    DeepSeek). The API refuses very large single requests, so the retry
    backs off and the callers keep prompts small (per entry)."""
    import time as _t
    for a in range(tries):
        try:
            body = json.dumps({"model": IO_MODEL,
                               "messages": [{"role": "user", "content": prompt}],
                               "max_tokens": max(tokens, 3000),
                               "reasoning": {"exclude": True},
                               "temperature": 0.3}).encode()
            req = urllib.request.Request(IO_URL, data=body,
                                         headers={"Authorization": "Bearer " + IO_KEY,
                                                  "Content-Type": "application/json"})
            with urllib.request.urlopen(req, timeout=300) as r:
                d = json.loads(r.read())
            return d["choices"][0]["message"]["content"]
        except Exception as e:
            if a == tries - 1:
                raise
            _t.sleep(3 * (a + 1))
    return ""


OR_KEY = os.environ.get("OPENROUTER_API_KEY", "")
OR_MODEL = os.environ.get("OR_MODEL", "poolside/laguna-s-2.1:free")
OR_URL = "https://openrouter.ai/api/v1/chat/completions"


def openrouter(prompt, tokens, tries=4):
    """The OpenRouter backend: NVIDIA Nemotron (the free variant when the
    account's paid credits are exhausted). Retries with backoff on 429."""
    import time as _t
    for a in range(tries):
        try:
            body = json.dumps({"model": OR_MODEL,
                               "messages": [{"role": "user", "content": prompt}],
                               "max_tokens": max(tokens, 2000),
                               "reasoning": {"exclude": True},
                               "temperature": 0.3}).encode()
            req = urllib.request.Request(OR_URL, data=body,
                                         headers={"Authorization": "Bearer " + OR_KEY,
                                                  "Content-Type": "application/json"})
            with urllib.request.urlopen(req, timeout=300) as r:
                d = json.loads(r.read())
            return d["choices"][0]["message"]["content"]
        except Exception as e:
            if a == tries - 1:
                log("openrouter failed: %s" % e)
                return ""
            _t.sleep(4 * (a + 1))
    _t.sleep(2)  # paced: the free tier wants one request at a time
    return ""


def vllm(prompt, tokens):
    # IO Intelligence first: it sustained 3/3 where the OpenRouter free tier
    # throttled; OpenRouter is the fallback.
    if IO_KEY:
        return intelligence(prompt, tokens)
    if OR_KEY:
        return openrouter(prompt, tokens)
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
    if not (VLLM_URL or IO_KEY or OR_KEY) or not ZXVD_URL or not ZXVD_TOKEN:
        log("need a backend (VLLM_URL, IO_INTEL_KEY or OPENROUTER_API_KEY) plus "
            "ZXVD_URL and ZXVD_TOKEN")
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
            if not text or not text.strip():
                log("%s attempt %d: the model returned nothing" % (o, attempt))
                continue
            try:
                from assemble import assemble_manifest
                man = assemble_manifest(o, node, text)
                st, verdict = judge(man)
                text = man
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