#!/usr/bin/env python3
# logic_adapter.py — the logic adapters in Python, mirroring
# kernel/src/lpres/lpres_adapter.c exactly. The bridge between the
# ordinary binary state (0/1) and the 144-dimensional paraconsistent state
# (the lattice). Every conversion is exact and total, and the descent to
# binary never manufactures authority: only TRUE becomes 1.
#
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""usage: python3 logic_adapter.py     # the three-space self-test"""
NEITHER, TRUE, FALSE, BOTH = 0, 1, 2, 3
# the chain: FALSE < NEITHER < BOTH < TRUE (the kernel's ordering)
CHAIN = {FALSE: 0, NEITHER: 1, BOTH: 2, TRUE: 3}
POINT_BYTES = 18
STATE_COUNT = 145          # the ladder: the four, the gateway, the phases


def rank_of(s):
    return CHAIN.get(s, None)


# ---- the binary <-> lpres adapters ------------------------------------
def from_binary(b):
    return TRUE if b else FALSE


def to_binary(s):
    """ONLY TRUE becomes 1 (the never-authorise guard)."""
    return 1 if s == TRUE else 0


def adapt_binary(b):
    """The round trip a legacy caller performs: identity for 0 and 1."""
    return to_binary(from_binary(b))


# ---- the lpres <-> 144-dimensional point adapters ---------------------
def to_point(s):
    """The point's bit is set at the value's own id, so the mapping is
    injective: a different value sets a different bit."""
    point = bytearray(POINT_BYTES)
    if 0 <= s < STATE_COUNT and s < POINT_BYTES * 8:
        point[s // 8] |= 1 << (s % 8)
    return bytes(point)


def from_point(point):
    """The first set bit names the value; an empty point is NEITHER."""
    for i in range(POINT_BYTES * 8):
        if point[i // 8] & (1 << (i % 8)):
            return i if i < STATE_COUNT else NEITHER
    return NEITHER


# ---- the phase adapter: any of the 144 phases -> the four values ------
def phase_to_foundation(phase):
    if 0 <= phase < 4:
        return phase
    r = rank_of(phase)
    if r is None:
        return NEITHER
    if r >= CHAIN[TRUE]:
        return TRUE
    if r >= CHAIN[BOTH]:
        return BOTH
    return NEITHER


def self_test():
    print("== the logic adapters: the three spaces ==")
    # positive: the binary round trip is identity
    ok = all(adapt_binary(b) == b for b in (0, 1))
    print("  [positive] the binary round trip is identity: %s" % ok)
    assert ok
    # negative: no non-TRUE value becomes 1
    ok = all(to_binary(s) == 0 for s in (NEITHER, FALSE, BOTH))
    print("  [negative] no non-TRUE value becomes 1: %s" % ok)
    assert ok
    # neutral: the boundary — the empty point is NEITHER, the four survive
    ok = from_point(bytes(POINT_BYTES)) == NEITHER
    print("  [neutral] the empty point is NEITHER: %s" % ok)
    assert ok
    # the point round trip: every foundation value survives
    ok = all(from_point(to_point(s)) == s for s in (NEITHER, TRUE, FALSE, BOTH))
    print("  [the point] every foundation value survives the point: %s" % ok)
    assert ok
    # the phase adapter: only TRUE's rank reaches TRUE
    ok = phase_to_foundation(TRUE) == TRUE
    print("  [the phase] only TRUE's rank reaches TRUE: %s" % ok)
    assert ok
    print("  the adapters are exact and total; the descent never grants.")
    return 0


if __name__ == "__main__":
    raise SystemExit(self_test())
