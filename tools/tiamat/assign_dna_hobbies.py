#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""Give an agency member its hobbies, from its DNA seed.

Deterministic: the same seed always gives the same set. 3 to 5 hobbies are
drawn from hobbies.txt by a SHA-256 counter stream over the seed; if the set
is already held by another member of the same agency the counter moves on
and the draw repeats, so no two members of an agency share a set.

    python3 assign_dna_hobbies.py SEED_HEX [--taken agency.json]

agency.json is a JSON list of sets already assigned (lists of hobby names).
Prints the new set as JSON.
"""
import argparse
import hashlib
import json
import pathlib

CATALOGUE = pathlib.Path(__file__).with_name("hobbies.txt")


def catalogue():
    rows = [l.strip() for l in CATALOGUE.read_text().splitlines()]
    return [r for r in rows if r and not r.startswith("#")]


def stream(seed: bytes):
    ctr = 0
    while True:
        block = hashlib.sha256(seed + ctr.to_bytes(8, "big")).digest()
        for i in range(0, 32, 4):
            yield int.from_bytes(block[i:i + 4], "big")
        ctr += 1


def draw(seed: bytes, cat, attempt: int):
    s = stream(seed + attempt.to_bytes(4, "big"))
    k = 3 + next(s) % 3
    picks = []
    while len(picks) < k:
        h = cat[next(s) % len(cat)]
        if h not in picks:
            picks.append(h)
    return sorted(picks)


def assign(seed_hex: str, taken=()):
    cat = catalogue()
    if len(cat) != 144:
        raise SystemExit(f"hobbies.txt has {len(cat)} entries, expected 144")
    seen = {tuple(sorted(t)) for t in taken}
    seed = bytes.fromhex(seed_hex)
    for attempt in range(1 << 16):
        got = draw(seed, cat, attempt)
        if tuple(got) not in seen:
            return got
    raise SystemExit("no free hobby set (agency larger than the catalogue allows)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("seed_hex")
    ap.add_argument("--taken")
    a = ap.parse_args()
    taken = json.load(open(a.taken)) if a.taken else []
    print(json.dumps(assign(a.seed_hex, taken)))


if __name__ == "__main__":
    main()
