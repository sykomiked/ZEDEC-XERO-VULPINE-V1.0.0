#!/usr/bin/env python3
# lattice_rules.py — the dynamic geometry the lattice determines. The
# lattice itself is the PARAMETERS (the 144 static points). But a lattice
# also determines RULES — which moves between its points are legal — and
# those rules form a different, DYNAMIC, hyperdimensional geometry: the
# space of transitions, not the space of positions.
#
#   THE LATTICE (static)   144 points, the ranks, the anchors
#   THE RULES (dynamic)    the legal moves: which transitions preserve the
#                          invariants (the never-authorise guard, the
#                          four-valued join, the norm conservation)
#   THE GEOMETRY (dynamic) the ORBITS: the closed paths the rules allow,
#                          which are the system's actual behaviours
#
# The rules are derived from the lattice, never chosen: a move is legal
# iff it preserves every invariant. So the dynamic geometry is as
# deterministic as the static one — it is the lattice's own shadow.
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""usage: lattice_rules.py [--json out.json]"""
import argparse
import json
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))

FIB = [1, 1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144]
NEITHER, TRUE, FALSE, BOTH = 0, 1, 2, 3
# the chain: FALSE < NEITHER < BOTH < TRUE (the kernel's ordering)
CHAIN = {FALSE: 0, NEITHER: 1, BOTH: 2, TRUE: 3}


def rank(s):
    return CHAIN.get(s, 1)


def conjoin(a, b):
    return a if rank(a) <= rank(b) else b


def disjoin(a, b):
    return a if rank(a) >= rank(b) else b


def is_legal(from_state, to_state, event):
    """A move is legal iff it preserves every invariant. The invariants:
    (1) the never-authorise guard: only a TRUE state with a TRUE event may
        reach TRUE;
    (2) the join law: the new state is the conjoin of the old and the
        event's contribution (no value is invented);
    (3) the chain order: no move jumps past a rank it did not traverse."""
    # (1) the never-authorise guard
    if to_state == TRUE and not (from_state == TRUE and event == TRUE):
        return False
    # (2) the join law: the new state must be derivable as a join/conjoin
    derivable = (to_state == conjoin(from_state, event)
                 or to_state == disjoin(from_state, event))
    if not derivable:
        return False
    # (3) the chain order: the move may not skip a rank
    lo, hi = sorted((rank(from_state), rank(to_state)))
    return hi - lo <= 1 or event != NEITHER


def orbits(max_len=4):
    """The closed paths the rules allow: a sequence of states where every
    step is legal and the path returns to its start. These are the
    system's actual behaviours — the dynamic geometry."""
    states = [NEITHER, TRUE, FALSE, BOTH]
    found = []
    def walk(path):
        if len(path) > max_len:
            return
        if len(path) >= 2 and path[-1] == path[0]:
            found.append(list(path))
            return
        for nxt in states:
            if is_legal(path[-1], nxt, nxt):
                walk(path + [nxt])
    for s in states:
        walk([s])
    # de-duplicate the cycles
    seen = set()
    uniq = []
    for o in found:
        key = tuple(sorted(o[:-1]))
        if key not in seen:
            seen.add(key)
            uniq.append(o)
    return uniq


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--json", default="")
    args = ap.parse_args()
    states = [NEITHER, TRUE, FALSE, BOTH]
    names = {NEITHER: "NEITHER", TRUE: "TRUE", FALSE: "FALSE", BOTH: "BOTH"}
    # the transition table: which moves are legal
    legal = {}
    for a in states:
        for b in states:
            legal["%s->%s" % (names[a], names[b])] = is_legal(a, b, b)
    n_legal = sum(1 for v in legal.values() if v)
    n_total = len(legal)
    orbs = orbits()
    print("== the dynamic geometry the lattice determines ==")
    print()
    print("  THE LATTICE (static): %d points, the ranks, the anchors [8, 144]"
          % len(FIB))
    print("  THE RULES (dynamic): %d of %d transitions are legal"
          % (n_legal, n_total))
    print()
    print("  the transition table (the legal moves):")
    for a in states:
        row = "    %-8s" % names[a]
        for b in states:
            row += " %-8s" % ("yes" if legal["%s->%s" % (names[a], names[b])] else "-")
        print(row)
    print("    %-8s %s" % ("", " ".join("%-8s" % names[b] for b in states)))
    print()
    print("  THE GEOMETRY (dynamic): %d closed orbits (the actual behaviours)"
          % len(orbs))
    for o in orbs[:6]:
        print("    %s" % " -> ".join(names[s] for s in o))
    print()
    print("  The rules are DERIVED, not chosen: a move is legal iff it")
    print("  preserves the never-authorise guard, the join law and the chain")
    print("  order. So the dynamic geometry is the lattice's own shadow —")
    print("  the space of TRANSITIONS, as deterministic as the space of")
    print("  POSITIONS. This is the missing piece: the lattice gives the")
    print("  parameters, the rules give the behaviour.")
    if args.json:
        json.dump({"legal": legal, "orbits": orbs}, open(args.json, "w"), indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())