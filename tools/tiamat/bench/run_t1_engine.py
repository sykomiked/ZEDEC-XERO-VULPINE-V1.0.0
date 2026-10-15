#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""T1 baseline on the engine: how well an untuned cell, simply asked, sorts the
hand-written four-valued set into TRUE, FALSE, BOTH and NEITHER. This is the
number the evidence head (S5) and the native adapter (S6) have to beat; it is not
T1 itself, which scores those heads (tools/tiamat/server/TESTS.md J4).

    python3 tools/tiamat/bench/run_t1_engine.py --host PATH/zxv-host --model CELL.gguf \
        --name Spinewyrm --out bench/results/DATE-tiamat/t1_engine

One cell alone (no witness): the reply's first TRUE, FALSE, BOTH or NEITHER is
its answer; none counts as NEITHER. Prompts are made single-line for the batch mode.
"""
import argparse
import json
import pathlib
import re
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from make_t1_set import items  # noqa: E402

BASES = ["TRUE", "FALSE", "BOTH", "NEITHER"]
WORD = re.compile(r"\b(TRUE|FALSE|BOTH|NEITHER)\b", re.I)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", required=True)
    ap.add_argument("--model", required=True)
    ap.add_argument("--name", required=True)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    out = pathlib.Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    xs = items()
    conf = out / f"{a.name}.conf"
    conf.write_text(f'slot 0 "{pathlib.Path(a.model).resolve()}"\ncell {a.name} 0 solo -\n')
    q = out / f"{a.name}.in.txt"
    q.write_text("".join(x["prompt"].replace("\n", " ") + "\n" for x in xs))
    with open(out / f"{a.name}.jsonl", "w") as o, open(out / f"{a.name}.stderr.txt", "w") as e:
        rc = subprocess.run([a.host, "--tiamat", str(conf), "--ask-file", str(q), "--state-dir", "",
                             "--notify", "off"], stdout=o, stderr=e).returncode
    recs = [json.loads(l) for l in open(out / f"{a.name}.jsonl") if l.strip()]
    if rc != 0 or len(recs) != len(xs):
        raise SystemExit(f"exit {rc}, {len(recs)} of {len(xs)} answered")
    conf_m = {t: {p: 0 for p in BASES} for t in BASES}
    for x, r in zip(xs, recs):
        m = WORD.search(r["lead_reply"]) if r["lead_rc"] == 1 else None
        conf_m[x["base"]][m.group(1).upper() if m else "NEITHER"] += 1
    n = len(xs)
    acc = sum(conf_m[b][b] for b in BASES) / n
    silent_or_split = sum(sum(conf_m[t].values()) for t in ("BOTH", "NEITHER"))
    false_cert = sum(conf_m[t][p] for t in ("BOTH", "NEITHER") for p in ("TRUE", "FALSE")) / silent_or_split
    doc = {"test": "T1 engine baseline (untuned cell, prompted)", "cell": a.name, "model": a.model, "n": n,
           "answered": sum(r["lead_rc"] == 1 for r in recs), "accuracy": round(acc, 4),
           "false_certainty_rate": round(false_cert, 4), "confusion_truth_by_answer": conf_m,
           "marks_for_the_heads": {"accuracy": ">= 0.85", "false_certainty_rate": "<= 0.05"}}
    (out / f"{a.name}_results.json").write_text(json.dumps(doc, indent=2) + "\n")
    print(f"{a.name}: accuracy {acc:.3f}, false certainty {false_cert:.3f}, answered {doc['answered']}/{n}")


if __name__ == "__main__":
    main()
