#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""gen_assure_ref.py — reference values for the 0.08889% assurance fee.

Writes "<a> <fee>" lines for test_pay.c, where fee = floor(a * 8889 / 10^7)
(pay_assure.h F1). Every value is computed TWO independent ways and the
script aborts unless both agree and the result is proven to be the floor:

  1. integer:  (a * 8889) // 10**7  (Python big integers).
  2. fraction: math.floor(Fraction(a) * Fraction(8889, 10**7)).
  3. check:    fee * 10^7 <= a * 8889 < (fee + 1) * 10^7.

Usage: gen_assure_ref.py OUT [COUNT] [SEED]   (defaults 120000, 20261010)
The amounts include 0..4096, values next to every multiple of the 1125-unit
step, powers of two +-3, 2^63 +-k and 2^64-1-k, and uniformly random values
below 2^63 and 2^64.
"""
import math
import random
import sys
from fractions import Fraction

NUM, DEN = 8889, 10**7


def by_int(a):
    return (a * NUM) // DEN


def by_fraction(a):
    return math.floor(Fraction(a) * Fraction(NUM, DEN))


def is_floor(a, t):
    return t * DEN <= a * NUM < (t + 1) * DEN


def amounts(count, seed):
    rng = random.Random(seed)
    s = set(range(0, 4097))
    for k in range(1, 4000):
        # the smallest gross paying k units is ceil(k * 10^7 / 8889)
        edge = -(-k * DEN // NUM)
        for d in (-2, -1, 0, 1, 2):
            s.add(edge + d)
            s.add(edge * 1000003 + d)
    for e in range(0, 64):
        for d in range(-3, 4):
            v = (1 << e) + d
            if 0 <= v < 2**64:
                s.add(v)
    for k in range(0, 64):
        s.add(2**63 - 1 - k)
        s.add(2**63 + k)
        s.add(2**64 - 1 - k)
    s = {v for v in s if 0 <= v < 2**64}
    while len(s) < count // 2:
        s.add(rng.randrange(0, 2**63))
    while len(s) < count:
        s.add(rng.randrange(0, 2**64))
    return sorted(s)


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    count = int(sys.argv[2]) if len(sys.argv) > 2 else 120000
    seed = int(sys.argv[3]) if len(sys.argv) > 3 else 20261010
    vals = amounts(count, seed)
    with open(sys.argv[1], "w") as f:
        for a in vals:
            t1, t2 = by_int(a), by_fraction(a)
            if t1 != t2 or not is_floor(a, t1):
                sys.exit("MISMATCH at a=%d: int=%d fraction=%d" % (a, t1, t2))
            f.write("%d %d\n" % (a, t1))
    print("gen_assure_ref: %d amounts, both methods agree, max a = %d" % (len(vals), vals[-1]))


if __name__ == "__main__":
    main()
