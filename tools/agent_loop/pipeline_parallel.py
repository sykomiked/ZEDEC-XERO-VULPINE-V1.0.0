#!/usr/bin/env python3
# pipeline_parallel.py — the batched pipeline: N apps drafted concurrently
# against one vLLM endpoint (continuous batching keeps the GPU busy), each
# assembled and judged by the kernel's own runtime, with the verdict fed
# back. The sequential pipeline leaves the GPU at 0% between calls; this
# one keeps it loaded.
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""usage: pipeline_parallel.py [--workers 16] [--attempts 4] [--limit N]

Env: VLLM_URL, VLLM_MODEL, ZXVD_URL, ZXVD_TOKEN (as pipeline.py).
State: tools/agent_loop/pipeline.state (the passed apps, resume-safe).
"""
import argparse
import json
import os
import sys
import threading
import time
import urllib.request
from concurrent.futures import ThreadPoolExecutor, as_completed

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import pipeline as P  # noqa: E402  (prompt_for, checks_of, skeleton, criteria_of)

ROOT = P.ROOT
LOCK = threading.Lock()
PROGRESS = {"passed": 0, "failed": 0}


def log(msg):
    with LOCK:
        print("%s %s" % (time.strftime("%H:%M:%S"), msg), flush=True)


def one_app(o, node, attempts):
    """Draft, assemble, judge, retry — one app, N attempts."""
    verdict = ""
    last_text = ""
    for attempt in range(1, attempts + 1):
        try:
            text = P.vllm(P.prompt_for(o, node, verdict, last_text), 2400)
        except Exception as e:
            log("%s attempt %d: model error %s" % (o, attempt, str(e)[:80]))
            time.sleep(2)
            continue
        if not text or not text.strip():
            log("%s attempt %d: the model returned nothing" % (o, attempt))
            continue
        try:
            from assemble import assemble_manifest
            man = assemble_manifest(o, node, text)
            st, verdict = P.judge(man)
            text = man
        except Exception as e:
            log("%s attempt %d: judge error %s" % (o, attempt, str(e)[:80]))
            continue
        log("%s attempt %d: %s" % (o, attempt,
            "PASSED" if st == 0 else "testbed[%d]: %s" % (st, verdict[:80])))
        last_text = text
        if st == 0:
            return o, True, verdict
    return o, False, verdict


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--workers", type=int, default=16)
    ap.add_argument("--attempts", type=int, default=4)
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--state", default=os.path.join(ROOT, "tools/agent_loop/pipeline.state"))
    args = ap.parse_args()
    g = "/tmp/appgraph_par.json"
    import subprocess
    subprocess.run([sys.executable, os.path.join(ROOT, "tools/apps/app_graph.py"), "--json", g],
                   check=True, capture_output=True)
    graph = json.load(open(g))
    nodes, order = graph["nodes"], graph.get("order", [])
    done = set()
    if os.path.exists(args.state):
        done = set(x.strip() for x in open(args.state) if x.strip())
    todo = []
    for o in order:
        node = nodes.get("app:" + o) or nodes.get(o)
        if not node or node.get("done") or node.get("kind") != "app" or o in done:
            continue
        todo.append((o, node))
        if args.limit and len(todo) >= args.limit:
            break
    log("batched pipeline: %d apps, %d workers" % (len(todo), args.workers))
    failed = []
    with ThreadPoolExecutor(max_workers=args.workers) as ex:
        futs = {ex.submit(one_app, o, n, args.attempts): (o, n) for o, n in todo}
        for f in as_completed(futs):
            o, n = futs[f]
            o, ok, verdict = f.result()
            # every failure keeps its verdict for review
            with open(os.path.join(ROOT, "tools/agent_loop/results",
                                   "app_%s.txt" % o), "w") as fh:
                fh.write("# app %s: %s\n# verdict: %s\n" %
                         (o, "PASSED" if ok else "FAILED", verdict))
            if ok:
                PROGRESS["passed"] += 1
                with open(args.state, "a") as fh:
                    fh.write(o + "\n")
            else:
                PROGRESS["failed"] += 1
                failed.append((o, n))
            log("progress: %d passed, %d failed (of %d)" %
                (PROGRESS["passed"], PROGRESS["failed"],
                 PROGRESS["passed"] + PROGRESS["failed"]))
    # the retry pass: every failure is requeued immediately, with the fresh
    # verdict already in the prompt (the resume-safe state skips the passed)
    if failed:
        log("== retry pass: %d apps ==" % len(failed))
        with ThreadPoolExecutor(max_workers=max(4, args.workers // 2)) as ex:
            futs = {ex.submit(one_app, o, n, args.attempts + 2): (o, n) for o, n in failed}
            for f in as_completed(futs):
                o, n = futs[f]
                o, ok, verdict = f.result()
                with open(os.path.join(ROOT, "tools/agent_loop/results",
                                       "app_%s.txt" % o), "w") as fh:
                    fh.write("# app %s: %s (retry)\n# verdict: %s\n" %
                             (o, "PASSED" if ok else "FAILED", verdict))
                if ok:
                    PROGRESS["passed"] += 1
                    PROGRESS["failed"] -= 1
                    with open(args.state, "a") as fh:
                        fh.write(o + "\n")
                log("retry progress: %d passed, %d failed" %
                    (PROGRESS["passed"], PROGRESS["failed"]))
    log("BATCHED DONE: %d passed, %d failed" % (PROGRESS["passed"], PROGRESS["failed"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())