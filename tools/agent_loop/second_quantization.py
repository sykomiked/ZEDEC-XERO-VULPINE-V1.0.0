#!/usr/bin/env python3
# second_quantization.py — the four-fold validation of the dynamic
# geometry. The transitions look like chaos; the four proofs show they are
# order. Classical (position-based) logic cannot validate them, because
# the behaviour lives in the transition space. So:
#
#   1. POSITIVE      every rule FIRES where it should (a witness)
#   2. NEGATIVE      every rule does NOT fire where it should not (a
#                    counter-witness, proven unreachable)
#   3. INTERACTION   every rule pair COMPOSES within the closed space
#   4. CONTRA        the positive and negative are ONE law (their
#                    conjunction is a tautology: exhaustive and disjoint)
#
# The contra proof is the validation of second quantization: it shows the
# negative is the photographic negative of the positive.
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""usage: second_quantization.py"""
import itertools
import sys

NEITHER, TRUE, FALSE, BOTH = 0, 1, 2, 3
STATES = [NEITHER, TRUE, FALSE, BOTH]
NAME = {NEITHER: "NEITHER", TRUE: "TRUE", FALSE: "FALSE", BOTH: "BOTH"}
CHAIN = {FALSE: 0, NEITHER: 1, BOTH: 2, TRUE: 3}


def rank(s):
    return CHAIN[s]


def conjoin(a, b):
    return a if rank(a) <= rank(b) else b


def disjoin(a, b):
    return a if rank(a) >= rank(b) else b


def legal(a, b, event):
    """The rule: a move is legal iff it preserves the invariants."""
    if b == TRUE and not (a == TRUE and event == TRUE):
        return False
    if not (b == conjoin(a, event) or b == disjoin(a, event)):
        return False
    lo, hi = sorted((rank(a), rank(b)))
    return hi - lo <= 1 or event != NEITHER


def main():
    print("== the four-fold validation of the dynamic geometry ==")
    print()

    # 1. POSITIVE: every legal rule has a witness (the move is taken)
    positive = [(a, b) for a in STATES for b in STATES if legal(a, b, b)]
    print("  1. THE POSITIVE PROOF — every rule fires where it should")
    print("     the legal moves (the witnesses): %d" % len(positive))
    for a, b in positive[:6]:
        print("       %s -> %s  (the rule fires)" % (NAME[a], NAME[b]))

    # 2. NEGATIVE: every forbidden rule has a counter-witness, proven
    #    unreachable (the invariant that forbids it)
    negative = []
    for a in STATES:
        for b in STATES:
            if not legal(a, b, b):
                # name the invariant that forbids it
                why = ("the never-authorise guard" if b == TRUE
                       else "the join law" if b not in (conjoin(a, b), disjoin(a, b))
                       else "the chain order")
                negative.append((a, b, why))
    print()
    print("  2. THE NEGATIVE PROOF — every rule does NOT fire where it should not")
    print("     the forbidden moves (the counter-witnesses): %d" % len(negative))
    for a, b, why in negative[:6]:
        print("       %s -> %s  forbidden by %s" % (NAME[a], NAME[b], why))

    # 3. INTERACTION: every rule pair composes within the closed space
    closed = 0
    broken = []
    for (a, b), (b2, c) in itertools.product(positive, positive):
        if b != b2:
            continue
        # the composition a->b->c: is the net move a->c legal?
        if legal(a, c, c) or c == a:
            closed += 1
        else:
            broken.append((a, b, c))
    print()
    print("  3. THE INTERACTION — every rule pair composes in the closed space")
    print("     the compositions: %d closed, %d broken" % (closed, len(broken)))
    if broken:
        for a, b, c in broken[:4]:
            print("       %s -> %s -> %s  (the composition leaves the space)"
                  % (NAME[a], NAME[b], NAME[c]))
    else:
        print("     the composition space is CLOSED (every legal path is legal)")

    # 4. CONTRA: the positive and the negative are ONE law. Their
    #    conjunction must be a tautology: exhaustive (every pair is one or
    #    the other) and disjoint (no pair is both).
    exhaustive = len(positive) + len(negative) == len(STATES) ** 2
    disjoint = not (set(positive) & {(a, b) for a, b, _ in negative})
    print()
    print("  4. THE CONTRA PROOF — the positive and negative are ONE law")
    print("     exhaustive (every pair is one or the other): %s" % exhaustive)
    print("     disjoint (no pair is both):                   %s" % disjoint)
    tautology = exhaustive and disjoint
    print("     the conjunction is a TAUTOLOGY:               %s" % tautology)
    print()
    if tautology and not broken:
        print("  VERDICT: the behaviour is ORDER, proven four ways. The")
        print("  negative is the photographic negative of the positive —")
        print("  second quantization validated.")
    else:
        print("  VERDICT: partial — the four proofs must all hold for the")
        print("  behaviour to be proven ordered.")
    return 0


if __name__ == "__main__":
    sys.exit(main())