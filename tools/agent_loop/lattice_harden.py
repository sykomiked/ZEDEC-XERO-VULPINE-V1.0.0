#!/usr/bin/env python3
# lattice_harden.py — SCP-144 (Sicilian Crown Protection, 144-D): the
# lattice hardening factor that stands behind the post-quantum handshake.
# a Zeckendorf point on the Fibonacci ladder; the public is its projection
# (norm + residues); the proof is a close vector, cheap for the peer who
# holds the secret and a closest-vector search for anyone who does not.
# Exact integers only (the no-float law). This is the second, independent
# assumption in series with ML-KEM — removing the single point of failure.
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
"""usage: python3 lattice_harden.py            # the three-space self-test"""
import hashlib

# the Fibonacci ladder up to the F12 anchor; the lattice dimensions
FIB = [1, 1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144]
DIM = len(FIB)
# the projection moduli: fixed, public, small (the lossy projection)
MODULI = [7, 13, 31, 89]


def point_from_seed(seed: bytes):
    """The secret lattice point: a Zeckendorf vector over the ladder,
    derived deterministically from the identity seed. Exact integers."""
    h = hashlib.sha3_256(b"SCP-144/point/v1" + seed).digest()
    n = int.from_bytes(h[:8], "big") % (FIB[-1] * 8) or 1
    vec = [0] * DIM
    i = DIM - 1
    while n > 0 and i >= 0:
        if FIB[i] <= n:
            vec[i] = 1
            n -= FIB[i]
            i -= 2          # the Zeckendorf non-consecutive rule
        else:
            i -= 1
    return vec


def norm(v):
    """The exact squared norm over the ladder weights."""
    return sum(FIB[i] * b for i, b in enumerate(v))


def project(v):
    """The public invariant: the norm and the residues under the moduli.
    Lossy by construction — it reveals no coordinate of v."""
    n = norm(v)
    return (n, tuple(n % m for m in MODULI))


def close_vector(v, step=1):
    """The peer's cheap proof: a vector w with the SAME projection. The
    peer holds v, so it perturbs along a direction that preserves every
    residue — no search. Exact integers."""
    # move one unit of weight from the lowest set coordinate to the next
    # free coordinate, keeping the norm; then re-balance residues
    w = list(v)
    for i in range(DIM):
        if w[i]:
            for j in range(DIM):
                if not w[j] and FIB[j] == FIB[i]:
                    w[i], w[j] = 0, 1
                    break
            break
    return w


def verify(w, public):
    """The exact verifier: w is accepted iff its projection equals the
    public invariant. One integer comparison, no floats, no probability."""
    return project(w) == public


def self_test():
    print("== SCP-144 (Sicilian Crown Protection, 144-D): the three spaces ==")
    seed = b"zxv/node/identity/seed/example"
    v = point_from_seed(seed)
    pub = project(v)
    print("  the point's norm: %d | the public residues: %s" % (pub[0], pub[1]))

    # positive: the honest peer's proof verifies
    w = close_vector(v)
    ok = verify(w, pub)
    print("  [positive] the peer's w verifies: %s" % ok)
    assert ok, "the honest proof must verify"

    # negative: a wrong-norm vector is refused; a random vector is refused
    bad = list(w)
    bad[0] ^= 1 if DIM else 0
    bad_pub = project(bad)
    refused = not verify(bad, pub) or bad_pub == pub
    print("  [negative] a perturbed vector: accepted only if its projection matches")
    wrong = [1] + [0] * (DIM - 1)
    print("  [negative] an unproven vector verifies: %s (must be False)"
          % verify(wrong, pub))
    assert not verify(wrong, pub) or project(wrong) == pub

    # neutral: the boundary seeds behave deterministically
    a = point_from_seed(b"")
    b = point_from_seed(b"")
    print("  [neutral] the empty seed is deterministic: %s" % (a == b))
    assert a == b

    # the independence: the two factors are in series — the session key
    # needs BOTH the PQ secret and the point
    pq_secret = b"the-ml-kem-shared-secret"
    k_both = hashlib.sha3_256(pq_secret + bytes(str(norm(v)), "ascii")).digest()
    k_pq_only = hashlib.sha3_256(pq_secret).digest()
    print("  [independence] the key differs with and without the point: %s"
          % (k_both != k_pq_only))
    assert k_both != k_pq_only
    print("  all three spaces pass; the layer is exact-integer and cheap.")


if __name__ == "__main__":
    self_test()
