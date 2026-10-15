#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""T4 profile checks (tools/tiamat/TEST_PLAN.md) for one machine.

    python3 tools/tiamat/bench/run_t4.py --host PATH/zxv-host --conf tiamat.conf \
        --out bench/results/DATE-tiamat/t4_<machine> [--budget-bytes 4000000000] [--compare OTHER/t4_results.json]

Measures, with the H1 swarm:
  first answer   one question in a fresh process, twice: the first run after the
                 models were last read (cold as the OS allows without root) and
                 the second (warm); seconds from start to answer
  resident peak  the process's peak resident memory over a 10-question batch
  same answers   the 10 questions answered twice; every reply hashed (SHA-256);
                 both runs must match, and with --compare, the other machine too
Pass marks (fixed before the first run): first answer within 20 s on laptop_core,
resident peak within the profile's budget, every hash equal.
"""
import argparse
import hashlib
import json
import pathlib
import platform
import resource
import subprocess
import sys
import time

QUESTIONS = [
    "What is the capital of Australia?",
    "Who wrote Pride and Prejudice?",
    "What is 17 times 23?",
    "Write a Python function that adds two numbers.",
    "How many legs does a spider have?",
    "What is the chemical symbol for gold?",
    "What is 9876 minus 1234?",
    "In one sentence, what is a paraconsistent logic?",
    "Which planet is closest to the Sun?",
    "What will the weather be in Palermo on 1 January 2035?",
]


def host_cmd(a, *extra):
    return [a.host, "--tiamat", a.conf, "--state-dir", "", "--notify", "off", *extra]


def timed(cmd, out_path):
    t0 = time.time()
    with open(out_path, "w") as o:
        p = subprocess.run(cmd, stdout=o, stderr=subprocess.PIPE, text=True)
    return time.time() - t0, p.returncode, p.stderr


def batch(a, out, name):
    q = out / "t4_questions.txt"
    q.write_text("".join(x + "\n" for x in QUESTIONS))
    before = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
    secs, rc, err = timed(host_cmd(a, "--ask-file", str(q)), out / f"{name}.jsonl")
    peak = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
    (out / f"{name}.stderr.txt").write_text(err)
    recs = [json.loads(l) for l in open(out / f"{name}.jsonl") if l.strip()]
    if rc != 0 or len(recs) != len(QUESTIONS):
        raise SystemExit(f"{name}: exit {rc}, {len(recs)} answers")
    # ru_maxrss: kilobytes on Linux, bytes on macOS; the largest child so far
    scale = 1 if sys.platform == "darwin" else 1024
    hashes = [hashlib.sha256((r["lead_reply"] + "\x00" + r["witness_reply"]).encode()).hexdigest() for r in recs]
    return {"seconds": round(secs, 1), "peak_rss_bytes": max(peak, before) * scale,
            "answered": sum(r["lead_rc"] == 1 for r in recs), "hashes": hashes}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", required=True)
    ap.add_argument("--conf", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--budget-bytes", type=int, default=4_000_000_000, help="laptop_core: 4 GB")
    ap.add_argument("--first-answer-limit", type=float, default=20.0)
    ap.add_argument("--compare", help="another machine's t4_results.json")
    a = ap.parse_args()
    out = pathlib.Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    first = []
    for k in ("first", "second"):
        secs, rc, err = timed(host_cmd(a, "--ask", QUESTIONS[0]), out / f"ask_{k}.txt")
        (out / f"ask_{k}.stderr.txt").write_text(err)
        if rc != 0:
            raise SystemExit(f"--ask failed: {err[-400:]}")
        first.append(round(secs, 1))
    b1, b2 = batch(a, out, "batch_1"), batch(a, out, "batch_2")
    same = b1["hashes"] == b2["hashes"]
    doc = {"test": "T4 profile", "machine": {"system": platform.system(), "machine": platform.machine(),
                                             "node": platform.node(), "cpus": __import__("os").cpu_count()},
           "conf": pathlib.Path(a.conf).read_text(),
           "first_answer_seconds": {"after_last_read": first[0], "warm": first[1]},
           "batch_1": b1, "batch_2": b2, "same_answers_twice": same}
    marks = [
        {"test": "first answer (warm)", "mark": f"<= {a.first_answer_limit} s", "value": first[1],
         "pass": first[1] <= a.first_answer_limit},
        {"test": "resident peak", "mark": f"<= {a.budget_bytes} bytes", "value": max(b1["peak_rss_bytes"], b2["peak_rss_bytes"]),
         "pass": max(b1["peak_rss_bytes"], b2["peak_rss_bytes"]) <= a.budget_bytes},
        {"test": "every question answered", "mark": "10 of 10, twice", "value": [b1["answered"], b2["answered"]],
         "pass": b1["answered"] == b2["answered"] == len(QUESTIONS)},
        {"test": "same answers on the same machine", "mark": "all 10 hashes equal", "value": same, "pass": same},
    ]
    if a.compare:
        other = json.load(open(a.compare))
        eq = other["batch_1"]["hashes"] == b1["hashes"]
        diff = [QUESTIONS[i] for i, (x, y) in enumerate(zip(other["batch_1"]["hashes"], b1["hashes"])) if x != y]
        marks.append({"test": f"same answers as {other['machine']['node']} ({other['machine']['machine']})",
                      "mark": "all 10 hashes equal", "value": eq, "pass": eq, "differs": diff})
    doc["pass_marks"] = marks
    (out / "t4_results.json").write_text(json.dumps(doc, indent=2) + "\n")
    for m in marks:
        print(f"{'PASS' if m['pass'] else 'FAIL'}  {m['test']} = {m['value']} (mark {m['mark']})")


if __name__ == "__main__":
    main()
