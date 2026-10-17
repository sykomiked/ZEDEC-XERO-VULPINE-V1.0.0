#!/usr/bin/env python3
# agent_loop — checkpointed task runner for the A6000 coding engine.
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Run a queue of model tasks with checkpoint/resume.

For each pending task in queue.yaml, prompt the model via llama-cli,
save the reply to results/, and record the task id in queue.state.
--once runs a single pass; without it the loop re-checks the queue
until killed (a quiet sleep between cycles). Determinism: fixed seed,
prompt hash and model id are written into every result file.

Usage: run.sh  (thin wrapper)  |  python3 run_tasks.py [--once]
"""
import argparse
import hashlib
import os
import subprocess
import sys
import time

import yaml

HERE = os.path.dirname(os.path.abspath(__file__))
QUEUE = os.path.join(HERE, "queue.yaml")
STATE = os.path.join(HERE, "queue.state")
RESULTS = os.path.join(HERE, "results")
LOG = os.path.join(RESULTS, "run.log")
LLAMA_BIN = os.environ.get("LLAMA_BIN", os.path.expanduser("~/llama.cpp/llama-cli"))
LLAMA_MODEL = os.environ.get(
    "LLAMA_MODEL", os.path.expanduser("~/zxv-cuda/models/qwen2.5-1.5b-instruct-q4_k_m.gguf")
)
LLAMA_NGPU = os.environ.get("LLAMA_NGPU", "0")
LLAMA_CTX = os.environ.get("LLAMA_CTX", "16384")
LLAMA_SEED = os.environ.get("LLAMA_SEED", "5921622")  # 0x5A5856
LLAMA_THREADS = os.environ.get("LLAMA_THREADS", "2")  # this host: 2 > 6 (oversubscribed vCPUs)
MAX_TOKENS = int(os.environ.get("LLAMA_MAX_TOKENS", "2048"))


def log(msg):
    with open(LOG, "a") as f:
        f.write("%s %s\n" % (time.strftime("%Y-%m-%d %H:%M:%S"), msg))
    print(msg, flush=True)


def prompt_for(task):
    lines = [
        "You are the drafting engine for ZEDEC XERO VULPINE (ZXV), a freestanding",
        "C11 operating system. You produce proposals for review: precise,",
        "concise, grounded in the files you are given. Never claim a change",
        "was made or tested. When asked for code, give complete code with no",
        "TODO, no stubs. Exact arithmetic only; no floating point for money.",
        "",
        "TASK: %s" % task["task"],
        "",
    ]
    for f in task.get("files", []):
        p = os.path.join(HERE, "..", "..", f)
        if os.path.isfile(p):
            lines.append("=== FILE %s ===" % f)
            try:
                with open(p, "r", errors="replace") as fh:
                    body = fh.read()
                lines.append(body[:8000])
                if len(body) > 8000:
                    lines.append("... (truncated)")
            except OSError:
                lines.append("(unreadable)")
            lines.append("")
    if task.get("context"):
        lines.append("CONTEXT: %s" % task["context"])
        lines.append("")
    lines.append("Write the proposal now. Keep it under %d tokens." % task.get("tokens", MAX_TOKENS))
    return "\n".join(lines)


def strip_banner(s):
    """llama-cli prints an interactive-mode help banner before the reply;
    drop it (everything through the 'available commands:' block)."""
    i = s.find("available commands:")
    if i < 0:
        return s
    j = s.find("\n\n", i)
    return s[j + 2:] if j >= 0 else s[i:]


def run_task(task):
    prompt = prompt_for(task)
    ph = hashlib.sha256(prompt.encode()).hexdigest()[:16]
    tag = "%02d_%s" % (task["id"], task["title"].replace(" ", "_")[:40])
    out_path = os.path.join(RESULTS, tag + ".txt")
    if os.path.exists(out_path):
        log("task %s already has a result file; skipping" % task["id"])
        return True
    n = task.get("tokens", MAX_TOKENS)
    cmd = [
        LLAMA_BIN,
        "-m", LLAMA_MODEL,
        "--prompt", prompt,
        "-n", str(n),
        "--temp", "0.6",
        "--top-k", "40",
        "--top-p", "0.9",
        "--seed", LLAMA_SEED,
        "-c", LLAMA_CTX,
        "--single-turn",
        "--no-display-prompt",
    ]
    if LLAMA_NGPU != "0":
        cmd += ["-ngl", LLAMA_NGPU]
    cmd += ["-t", LLAMA_THREADS, "-tb", LLAMA_THREADS]
    log("task %s: %s (tokens=%d, prompt sha256 %s)" % (task["id"], task["title"], n, ph))
    t0 = time.time()
    try:
        r = subprocess.run(cmd, capture_output=True, timeout=3600)
    except subprocess.TimeoutExpired:
        log("task %s: TIMEOUT" % task["id"])
        return False
    dt = time.time() - t0
    body = r.stdout.decode("utf-8", errors="replace")
    body = strip_banner(body).strip()
    if r.returncode != 0:
        log("task %s: llama-cli rc=%d (%s)" % (task["id"], r.returncode, r.stderr.decode(errors="replace")[-400:]))
        return False
    head = (
        "# ZXV agent_loop result\n"
        "# task: %s\n"
        "# model: %s\n"
        "# seed: %s\n"
        "# prompt sha256: %s\n"
        "# tokens: %d, %.1fs\n\n"
        % (task["title"], os.path.basename(LLAMA_MODEL), LLAMA_SEED, ph, n, dt)
    )
    with open(out_path, "w") as f:
        f.write(head + body)
    with open(STATE, "a") as f:
        f.write("%d\n" % task["id"])
    log("task %s: done -> %s (%.1fs)" % (task["id"], os.path.basename(out_path), dt))
    return True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--once", action="store_true")
    args = ap.parse_args()
    if not os.path.exists(LLAMA_BIN):
        sys.exit("llama-cli not found at %s (set LLAMA_BIN)" % LLAMA_BIN)
    if not os.path.exists(LLAMA_MODEL):
        sys.exit("model not found at %s (set LLAMA_MODEL)" % LLAMA_MODEL)
    os.makedirs(RESULTS, exist_ok=True)
    done = set()
    if os.path.exists(STATE):
        with open(STATE) as f:
            done = {int(x) for x in f.read().split()}
    while True:
        with open(QUEUE) as f:
            queue = yaml.safe_load(f)
        pending = [t for t in queue if t["id"] not in done]
        if not pending:
            log("queue empty; all %d tasks done" % len(queue))
            if args.once:
                return 0
            time.sleep(60)
            continue
        log("cycle: %d pending of %d" % (len(pending), len(queue)))
        for t in pending:
            run_task(t)
            done.add(t["id"])
        if args.once:
            return 0
        time.sleep(10)


if __name__ == "__main__":
    sys.exit(main())