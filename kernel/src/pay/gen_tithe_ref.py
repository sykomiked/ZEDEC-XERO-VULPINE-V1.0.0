#!/usr/bin/env python3
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""gen_tithe_ref.py — reference values for the exact phi-percent tithe.

Writes "<a> <tithe>" lines for test_pay.c, where tithe = floor(a * phi / 100)
and phi = (1 + sqrt 5) / 2. Every value is computed THREE independent ways
and the script aborts unless all three agree:

  1. decimal: Decimal arithmetic at 90 significant digits, floor.
  2. isqrt:   (a + math.isqrt(5 a^2)) // 200  (the formula the C code uses).
  3. exact:   an integer-only proof that t is the floor, using fractions-free
              squared comparisons:  200 t - a <= a sqrt5 < 200 (t+1) - a.

Usage: gen_tithe_ref.py OUT [COUNT] [SEED]   (defaults 120000, 20261009)
The amounts include 0..4096, values next to multiples of 200, powers of two
+-3, 2^63 +-k and 2^64-1-k, and uniformly random values below 2^63 and 2^64.
"""
import math
import random
import sys
from decimal import Decimal, getcontext, ROUND_FLOOR

getcontext().prec = 90
PHI = (Decimal(1) + Decimal(5).sqrt()) / 2


def by_decimal(a):
    return int((Decimal(a) * PHI / 100).to_integral_value(rounding=ROUND_FLOOR))


def by_isqrt(a):
    return (a + math.isqrt(5 * a * a)) // 200


def is_floor(a, t):
    # lower: 200t - a <= a*sqrt5  (a*sqrt5 >= 0)
    lo = 200 * t - a
    if lo > 0 and lo * lo > 5 * a * a:
        return False
    # upper: a*sqrt5 < 200(t+1) - a
    hi = 200 * (t + 1) - a
    if hi <= 0 or 5 * a * a >= hi * hi:
        return False
    return True


def amounts(count, seed):
    rng = random.Random(seed)
    s = set(range(0, 4097))
    for k in range(1, 2000):
        for d in (-2, -1, 0, 1, 2):
            v = 200 * k * 1000003 + d
            if 0 <= v < 2**64:
                s.add(v)
    for e in range(0, 64):
        for d in range(-3, 4):
            v = (1 << e) + d
            if 0 <= v < 2**64:
                s.add(v)
    for k in range(0, 64):
        s.add(2**63 - 1 - k)
        s.add(2**63 + k)
        s.add(2**64 - 1 - k)
    while len(s) < count // 2:
        s.add(rng.randrange(0, 2**63))
    while len(s) < count:
        s.add(rng.randrange(0, 2**64))
    return sorted(s)


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    count = int(sys.argv[2]) if len(sys.argv) > 2 else 120000
    seed = int(sys.argv[3]) if len(sys.argv) > 3 else 20261009
    vals = amounts(count, seed)
    with open(sys.argv[1], "w") as f:
        for a in vals:
            t1, t2 = by_decimal(a), by_isqrt(a)
            if t1 != t2 or not is_floor(a, t1):
                sys.exit("MISMATCH at a=%d: decimal=%d isqrt=%d" % (a, t1, t2))
            f.write("%d %d\n" % (a, t1))
    print("gen_tithe_ref: %d amounts, three methods agree, max a = %d" % (len(vals), vals[-1]))


if __name__ == "__main__":
    main()
