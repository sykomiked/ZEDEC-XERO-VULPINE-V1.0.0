#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""S4: four-valued training data (TRUE, FALSE, BOTH, NEITHER) that we own.

Each example is a context of short source statements and one claim. The
label says what the context's evidence supports, not what is true in the
world, which is what the evidence head must learn:
    TRUE     some source supports the claim, none contradicts it
    FALSE    some source contradicts it, none supports it
    BOTH     one source supports it and another contradicts it
    NEITHER  no source speaks to it
mu (evidence for) and lambda (evidence against) are labelled separately,
as zt_evh fits them. Entities are invented and numbers drawn at random, so
the model cannot answer from memory and every label is exact. Nothing here
is copied from a third-party dataset, so the arbiter's head carries no
dataset terms (see the NLI note in roster.py).

    python3 make_tfbn_data.py --n 40000 --seed 1 --out tfbn.jsonl
"""
import argparse
import json
import random

SYL = ["ka", "lo", "mi", "ren", "sa", "tor", "vel", "dun", "ia", "bro", "quel", "zan", "or", "fen", "tis"]
SOURCES = ["the ledger", "the harbour log", "the survey", "the clerk's note", "the census", "the inspector",
           "the almanac", "the station report", "the registry", "the archive", "the courier", "the field book"]
RELATIONS = [
    ("lives in", "the town of"), ("works for", "the guild of"), ("was born in", "the village of"),
    ("owns a boat named", ""), ("teaches at", "the school of"),
]
NUMERIC = [("the bridge at {p} is {n} metres long", "metres"), ("the well at {p} is {n} metres deep", "metres"),
           ("the market at {p} has {n} stalls", "stalls"), ("the archive at {p} holds {n} scrolls", "scrolls")]
NEG = {"lives in": "does not live in", "works for": "does not work for", "was born in": "was not born in",
       "owns a boat named": "does not own a boat named", "teaches at": "does not teach at"}
BASES = ["TRUE", "FALSE", "BOTH", "NEITHER"]


def name(r, k=2):
    return "".join(r.choice(SYL) for _ in range(k)).capitalize()


def claim_rel(r):
    who, (rel, pre), where = name(r, 3), r.choice(RELATIONS), name(r)
    obj = f"{pre} {where}".strip()
    pos = f"{who} {rel} {obj}."
    other = f"{pre} {name(r)}".strip()
    neg_forms = [f"{who} {NEG[rel]} {obj}.", f"{who} {rel} {other}, not {obj}."]
    return pos, neg_forms


def claim_num(r):
    tpl, _ = r.choice(NUMERIC)
    p, n = name(r), r.randint(3, 900)
    wrong = n + r.choice([-1, 1]) * r.randint(1, max(2, n // 3))
    return cap(tpl.format(p=p, n=n)) + ".", [cap(tpl.format(p=p, n=wrong)) + "."]


def cap(s):
    return s[0].upper() + s[1:]


def unrelated(r):
    return claim_rel(r)[0] if r.random() < 0.5 else claim_num(r)[0]


def example(r, base):
    pos, negs = (claim_rel if r.random() < 0.6 else claim_num)(r)
    srcs = r.sample(SOURCES, 4)
    lines = []
    if base in ("TRUE", "BOTH"):
        lines.append(f"{cap(srcs[0])} says: {pos}")
    if base in ("FALSE", "BOTH"):
        neg = r.choice(negs)
        lines.append(f"{cap(srcs[1])} says: {neg}")
    for i in range(r.randint(1, 3)):
        lines.append(f"{cap(srcs[2 + i % 2])} says: {unrelated(r)}")
    r.shuffle(lines)
    return {
        "context": " ".join(lines),
        "claim": pos,
        "base": base,
        "mu": int(base in ("TRUE", "BOTH")),
        "lambda": int(base in ("FALSE", "BOTH")),
    }


def prompt(ex):
    return (f"Sources: {ex['context']}\nClaim: {ex['claim']}\n"
            "Using only the sources, is the claim TRUE, FALSE, BOTH (the sources conflict) or NEITHER (no source says)?\nAnswer:")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--n", type=int, default=40000)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    r = random.Random(a.seed)
    with open(a.out, "w") as f:
        for i in range(a.n):
            ex = example(r, BASES[i % 4])
            ex["prompt"] = prompt(ex)
            ex["split"] = "test" if i % 10 == 9 else "train"
            f.write(json.dumps(ex) + "\n")
    print(f"wrote {a.n} examples to {a.out}")


if __name__ == "__main__":
    main()
