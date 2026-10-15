#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""T1's hand-written test set (tools/tiamat/TEST_PLAN.md): 200 four-valued items from
the 50 scenarios in t1_scenarios.tsv, in the S4 data format (make_tfbn_data.py), all
in the "test" split. Each scenario gives four items:
    TRUE     the supporting source and an unrelated one
    FALSE    the contradicting source and an unrelated one
    BOTH     the supporting and the contradicting source
    NEITHER  the unrelated source only
Unlike S4's data, the sources paraphrase the claim instead of repeating it.

    python3 tools/tiamat/bench/make_t1_set.py --out t1_handset.jsonl
"""
import argparse
import json
import pathlib
import random
import sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "server"))
from make_tfbn_data import prompt  # noqa: E402

PARTS = {"TRUE": (1, 3), "FALSE": (2, 3), "BOTH": (1, 2), "NEITHER": (3,)}


def items(seed=15):
    r = random.Random(seed)
    out = []
    for line in (HERE / "t1_scenarios.tsv").read_text().splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        f = line.split("\t")
        if len(f) != 4:
            raise SystemExit(f"t1_scenarios.tsv: need 4 fields: {line[:60]}")
        for base, parts in PARTS.items():
            srcs = [f[i] for i in parts]
            r.shuffle(srcs)
            ex = {"context": " ".join(srcs), "claim": f[0], "base": base,
                  "mu": int(base in ("TRUE", "BOTH")), "lambda": int(base in ("FALSE", "BOTH")),
                  "split": "test", "set": "t1_handset"}
            ex["prompt"] = prompt(ex)
            out.append(ex)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    xs = items()
    with open(a.out, "w") as f:
        for x in xs:
            f.write(json.dumps(x) + "\n")
    print(f"wrote {len(xs)} items to {a.out}")


if __name__ == "__main__":
    main()
