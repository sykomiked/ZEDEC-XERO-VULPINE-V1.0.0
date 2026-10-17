# The lattice hardening layer — beyond post-quantum, cheaply

Post-quantum cryptography (ML-KEM, ML-DSA) rests on the hardness of
lattice problems in *fixed, published* lattices (the NIST modules). The
ZXV discovery — that the Fibonacci ladder up to F12 = 144 is itself a
lattice spine with two self-dual anchors — suggests a hardening layer
that stands **behind** the standard handshake, in the system's own
geometry. The goal: full security, no insane compute, no reliance on a
single published assumption.

## The principle: never one assumption

A handshake is safe only while its single hardness assumption holds. The
layer adds a **second, independent** assumption in series:

```
handshake = ML-KEM-1024 (the PQ standard)  THEN  the lattice factor (ZXV's own)
```

An attacker who breaks the PQ standard (a future algorithm, a quantum
advance) still faces the lattice factor. An attacker who studies the
lattice factor still faces the PQ standard. Neither alone opens the door.

## The lattice factor — what it is

The factor is a **proof-of-lattice-work**, not a new cipher. It uses the
one property the discovery gives us: the ladder's points are **canonical
and collision-free** (Zeckendorf), and recovering a point from its
invariants is a **short-vector / closest-vector problem** in the
144-dimensional ladder — a problem in NP ∩ coNP whose best known
algorithms are exponential in the dimension.

The factor works like this:

1. **The secret** is a lattice point `v` (a Zeckendorf vector over the
   Fibonacci ladder), generated from the node's identity seed.
2. **The public** is an invariant of `v`: its norm and its residue
   pattern under a few moduli (a projection, losing information).
3. **The proof** is that the peer can produce a vector `w` with the same
   invariants that is **close** to `v` in the lattice metric — a closest-
   vector instance at the dimension. Finding `w` without `v` is the hard
   problem; verifying `w` given the invariants is a single exact-integer
   comparison.

Because the arithmetic is exact integer (the no-float law), the cost is
**linear in the dimension** for the honest peer and **exponential** for
an attacker without the secret. That is the cheapness: the legitimate
party does a hash and a compare; the adversary does a lattice search.

## Why it is cheap for the honest peer

- **The honest peer knows `v`.** Producing `w` is one hash (the identity
  seed) plus one integer projection. Microseconds.
- **The adversary does not.** The closest-vector problem at dimension 144
  has no known polynomial algorithm; the honest peer never solves it.
- **The verification is exact.** `verify(w, public) = (norm(w) == the
  public norm) and (w mod m_i == the public residues)`. No floating
  point, no probabilistic check that can be gamed.

## The layered handshake

```
1. the identity seed  ->  the lattice point v            (the peer's own)
2. the public invariant  = project(v)                   (norm + residues)
3. the PQ handshake       = ML-KEM-1024 + ML-DSA-87     (the standard)
4. the lattice proof      = w, a close vector           (the factor)
5. the session key        = KDF(PQ secret || v)         (both, in series)
```

The session key depends on **both** the PQ secret and the lattice point,
so neither layer alone reconstructs it.

## What the H100 must build and test

1. **The point generator**: `lattice_point(identity_seed)` — deterministic
   from the seed, exact integers (already prototyped in
   `mk_lattice_apps.py`).
2. **The projection**: `public_invariant(v)` — the norm and a fixed set of
   moduli; lossy by design.
3. **The close-vector producer**: given `v`, produce `w` cheaply.
4. **The verifier**: `verify(w, invariant)` — exact, no floats.
5. **The tests, in all three spaces**:
   - *positive*: the honest peer's `w` verifies;
   - *negative*: a `w` with the wrong norm or a wrong residue is refused,
     and a brute-force search cannot find a valid `w` within a bounded
     budget (the hardness, measured);
   - *neutral*: the empty seed, the zero vector, the boundary dimension
     (8 and 144, the anchors) behave deterministically.
6. **The independence proof**: a test that breaking either layer alone
   does not yield the session key (the series construction holds).

## The honest boundary

This is a **design and a prototype direction**, not a finished cipher.
The lattice factor's hardness must be *measured* (the tests above), not
asserted; the 144-dimensional closest-vector problem's difficulty is the
security claim, and it must be stated as a measured bound, never as a
slogan. The layer's value is architectural: **it removes the single
point of failure** the standard handshake has, at a cost the honest peer
does not pay. That is the safe, cheap hardening — full security behind
post-quantum, in the system's own geometry.
