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


def prompt_for(app_id, node, verdict):
    spec = spec_text(app_id, node)
    lang = lang_rows(app_id)
    p = (
        "You are the drafting engine for ZEDEC XERO VULPINE (ZXV), a freestanding C11 "
        "operating system. Produce the COMPLETE, LOADABLE .zxapp manifest for the "
        "application '%s' (layer %s, slot %s), starting with the line 'zxapp 1'. "
        "NO C code, NO explanations, NO markdown fences — the raw manifest only. "
        "It is parsed and TESTED by a running node; one syntax error fails it.\n"
        "Reference manifest (follow exactly):\n---\n%s\n---\n"
        "Rules:\n"
        "1. Header: zxapp 1 / id / name / version / slot / layer.\n"
        "2. needs lines for every capability the spec names.\n"
        "3. provides + logic lines in the assigned languages; self-contained entries "
        "use INLINE sources (logic <lang> <entry> << then the source, then a line "
        "with only '.').\n"
        "4. check lines for EVERY provided entry with exact inputs and expected "
        "outputs; each input is <name>=<value>[,<value>...] (at most 8 values).\n"
        "Assigned languages: %s.\n"
        "The wave spec (the authority):\n---\n%s\n---\n"
        % (app_id, node.get("layer"), node.get("slot"), REF, ", ".join(lang), spec[:3000])
    )
    if verdict:
        p += ("\nYour previous attempt failed the node's test with:\n%s\n"
              "Fix exactly that and return the full corrected manifest.\n" % verdict)
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
        ok = False
        for attempt in range(1, args.attempts + 1):
            t0 = time.time()
            try:
                text = vllm(prompt_for(o, node, verdict), 2400)
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