#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""T2 scenario tests (tools/tiamat/TEST_PLAN.md) on the real engine.

    python3 tools/tiamat/bench/run_t2.py --host PATH/zxv-host --conf tiamat.conf \
        --out bench/results/DATE-tiamat/t2 [--seed 2026]

Runs the hosted app's batch modes with the H1 swarm, keeping every raw line:
  facts_ask      the 50 fact questions, answered by the swarm
  facts_wrong    the same questions with a planted wrong answer (a contradiction)
  facts_right    the same questions with a planted right answer
  gaps_ask       50 questions no source can answer
  sums_ask       50 seeded two-number sums, answered by the swarm
  sums_planted   the same sums with planted answers, half wrong
Pass marks, fixed before the first run (TEST_PLAN.md T2), per organ:
  planted contradictions held >= 0.95; easy truths answered (shown and right) >= 0.90.
"""
import argparse
import json
import pathlib
import random
import re
import subprocess
import sys
import time

HERE = pathlib.Path(__file__).resolve().parent
HELD = {"FALSE", "BOTH"}


def rows(name):
    out = []
    for line in (HERE / name).read_text().splitlines():
        if line.strip() and not line.startswith("#"):
            out.append(line.split("\t"))
    return out


def sums(seed, n=50):
    r = random.Random(seed)
    out = []
    for i in range(n):
        op = ["times", "plus", "minus", "divided by"][i % 4]
        if op == "times":
            a, b = r.randint(12, 999), r.randint(12, 99)
            v = a * b
        elif op == "plus":
            a, b = r.randint(100, 99999), r.randint(100, 99999)
            v = a + b
        elif op == "minus":
            a, b = r.randint(1000, 99999), r.randint(100, 999)
            v = a - b
        else:
            b = r.randint(3, 60)
            v = r.randint(3, 400)
            a = b * v
        out.append((f"What is {a} {op} {b}?", v))
    return out


def run(host, conf, mode, lines, out_dir, name):
    src = out_dir / f"{name}.in.txt"
    src.write_text("".join(l + "\n" for l in lines))
    t0 = time.time()
    with open(out_dir / f"{name}.jsonl", "w") as o, open(out_dir / f"{name}.stderr.txt", "w") as e:
        rc = subprocess.run([host, "--tiamat", conf, mode, str(src), "--state-dir", "",
                             "--notify", "off"], stdout=o, stderr=e).returncode
    secs = time.time() - t0
    recs = [json.loads(l) for l in open(out_dir / f"{name}.jsonl") if l.strip()]
    if rc != 0 or len(recs) != len(lines):
        raise SystemExit(f"{name}: exit {rc}, {len(recs)} of {len(lines)} lines answered")
    print(f"  {name}: {len(recs)} lines in {secs:.0f} s", file=sys.stderr)
    return recs, secs


def shown(r):
    return r["verdict"] in ("TRUE", "NEITHER") and r["lead_rc"] == 1


def last_int(text):
    nums = re.findall(r"-?\d[\d,]*", text)
    return int(nums[-1].replace(",", "")) if nums else None


def rate(k, n):
    return round(k / n, 4) if n else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", required=True)
    ap.add_argument("--conf", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--seed", type=int, default=2026)
    a = ap.parse_args()
    out = pathlib.Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    facts, gaps, sm = rows("facts.tsv"), [g[0] for g in rows("gaps.tsv")], sums(a.seed)
    res, secs = {}, {}

    recs, secs["facts_ask"] = run(a.host, a.conf, "--ask-file", [f[0] for f in facts], out, "facts_ask")
    right = [bool(re.search(f[3], r["lead_reply"], re.I)) for f, r in zip(facts, recs)]
    res["facts_ask"] = {
        "n": len(recs),
        "lead_right": sum(right),
        "answered_shown_and_right": sum(1 for ok, r in zip(right, recs) if ok and shown(r)),
        "shown_but_wrong": sum(1 for ok, r in zip(right, recs) if not ok and shown(r)),
        "shown_as_checked_but_wrong": sum(1 for ok, r in zip(right, recs) if not ok and r["verdict"] == "TRUE"),
        "held_but_right": sum(1 for ok, r in zip(right, recs) if ok and r["verdict"] in HELD),
        "held_and_wrong": sum(1 for ok, r in zip(right, recs) if not ok and r["verdict"] in HELD),
        "verdicts": {v: sum(1 for r in recs if r["verdict"] == v) for v in ("TRUE", "NEITHER", "FALSE", "BOTH")},
        "per_question": [{"q": f[0], "right": ok, "verdict": r["verdict"]} for f, ok, r in zip(facts, right, recs)],
    }
    recs, secs["facts_wrong"] = run(a.host, a.conf, "--check-file", [f"{f[0]}\t{f[2]}" for f in facts], out, "facts_wrong")
    res["facts_wrong"] = {"n": len(recs), "held": sum(r["verdict"] in HELD for r in recs),
                          "shown_as_checked": sum(r["verdict"] == "TRUE" for r in recs),
                          "verdicts": {v: sum(1 for r in recs if r["verdict"] == v) for v in ("TRUE", "NEITHER", "FALSE", "BOTH")}}
    recs, secs["facts_right"] = run(a.host, a.conf, "--check-file", [f"{f[0]}\t{f[1]}" for f in facts], out, "facts_right")
    res["facts_right"] = {"n": len(recs), "shown_as_checked": sum(r["verdict"] == "TRUE" for r in recs),
                          "held": sum(r["verdict"] in HELD for r in recs),
                          "verdicts": {v: sum(1 for r in recs if r["verdict"] == v) for v in ("TRUE", "NEITHER", "FALSE", "BOTH")}}
    recs, secs["gaps_ask"] = run(a.host, a.conf, "--ask-file", gaps, out, "gaps_ask")
    res["gaps_ask"] = {"n": len(recs), "shown_as_checked": sum(r["verdict"] == "TRUE" for r in recs),
                       "verdicts": {v: sum(1 for r in recs if r["verdict"] == v) for v in ("TRUE", "NEITHER", "FALSE", "BOTH")}}

    recs, secs["sums_ask"] = run(a.host, a.conf, "--ask-file", [q for q, _ in sm], out, "sums_ask")
    exact = [last_int(r["lead_reply"]) == v for (_, v), r in zip(sm, recs)]
    res["sums_ask"] = {
        "n": len(recs),
        "reckoner_applied": sum(r["reckon"] is not None for r in recs),
        "lead_right": sum(1 for (_, v), r in zip(sm, recs) if r["reckon"] and r["reckon"]["verdict"] == "TRUE"),
        # the swarm's answer is right when the lead's sum was checked right, or when the
        # reckoner found it wrong and showed the exact result instead (zxv_tiamat.h H4)
        "exact_shown": sum(1 for (_, v), r in zip(sm, recs) if r.get("exact_shown") and last_int(r["reckon"]["expr"]) == v),
        "answered_shown_and_right": sum(1 for (_, v), r in zip(sm, recs)
                                        if r["reckon"] and ((r["verdict"] == "TRUE") or
                                                            (r.get("exact_shown") and last_int(r["reckon"]["expr"]) == v))),
        "held": sum(r["verdict"] in HELD for r in recs),
        "shown_but_wrong": sum(1 for ok, r in zip(exact, recs) if not ok and shown(r) and not (r["reckon"] and r["reckon"]["verdict"] == "TRUE")),
        "by_op": {op: {"n": sum(1 for q, _ in sm if f" {op} " in q),
                       "lead_right": sum(1 for (q, _), r in zip(sm, recs) if f" {op} " in q and r["reckon"] and r["reckon"]["verdict"] == "TRUE")}
                  for op in ("times", "plus", "minus", "divided by")},
    }
    r2 = random.Random(a.seed + 1)
    planted = []
    for i, (q, v) in enumerate(sm):
        wrong = i % 2 == 1
        planted.append((q, v + r2.choice([-1, 1]) * r2.randint(1, 50) if wrong else v, wrong))
    recs, secs["sums_planted"] = run(a.host, a.conf, "--check-file", [f"{q}\tThe answer is {x}." for q, x, _ in planted], out, "sums_planted")
    res["sums_planted"] = {
        "n": len(recs),
        "wrong_planted": sum(w for _, _, w in planted),
        "wrong_held": sum(1 for (_, _, w), r in zip(planted, recs) if w and r["verdict"] in HELD),
        "right_planted": sum(not w for _, _, w in planted),
        "right_shown_as_checked": sum(1 for (_, _, w), r in zip(planted, recs) if not w and r["verdict"] == "TRUE"),
    }

    fa, fw, sa, sp = res["facts_ask"], res["facts_wrong"], res["sums_ask"], res["sums_planted"]
    marks = [
        {"organ": "general (lead + witness)", "test": "planted contradictions held", "mark": ">= 0.95",
         "value": rate(fw["held"], fw["n"]), "pass": fw["held"] / fw["n"] >= 0.95},
        {"organ": "general (lead + witness)", "test": "easy truths answered (shown and right)", "mark": ">= 0.90",
         "value": rate(fa["answered_shown_and_right"], fa["n"]), "pass": fa["answered_shown_and_right"] / fa["n"] >= 0.90},
        {"organ": "reckoner", "test": "planted contradictions held", "mark": ">= 0.95",
         "value": rate(sp["wrong_held"], sp["wrong_planted"]), "pass": sp["wrong_held"] / sp["wrong_planted"] >= 0.95},
        {"organ": "reckoner", "test": "easy truths answered (shown and right)", "mark": ">= 0.90",
         "value": rate(sa["answered_shown_and_right"], sa["n"]), "pass": sa["answered_shown_and_right"] / sa["n"] >= 0.90},
    ]
    doc = {"test": "T2 scenario", "seed": a.seed, "conf": pathlib.Path(a.conf).read_text(),
           "seconds": {k: round(v, 1) for k, v in secs.items()}, "pass_marks": marks, "results": res}
    (out / "t2_results.json").write_text(json.dumps(doc, indent=2) + "\n")
    for m in marks:
        print(f"{'PASS' if m['pass'] else 'FAIL'}  {m['organ']}: {m['test']} = {m['value']} (mark {m['mark']})")
    print(f"      sums: the lead alone was right on {sa['lead_right']} of {sa['n']}; "
          f"the exact result was shown for {sa['exact_shown']}")


if __name__ == "__main__":
    main()
