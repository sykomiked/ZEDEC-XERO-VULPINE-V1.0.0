# SCP-144 — Sicilian Crown Protection

**The standard:** *Sicilian Crown Protection, 144-dimensional.* The name
carries the architecture: the **Crown** is the sovereign heritage reserve
(the first of the three tribute rails), and **144** is the self-dual
lattice anchor (F12, where `Λ = Λ*` snaps back). SCP-144 is the hardening
layer that stands behind the post-quantum handshake — the system's own
geometry as a second, independent security assumption.



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

## The SCP-144 handshake

```
SCP-144 handshake:
1. the identity seed  ->  the lattice point v            (the peer's own)
2. the public invariant  = project(v)                   (norm + residues)
3. the PQ handshake       = ML-KEM-1024 + ML-DSA-87     (the standard)
4. the lattice proof      = w, a close vector           (the Crown factor)
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

## The SCP-144 handshake tiers over the networking protocols

SCP-144 is not one handshake: it is a **tier ladder**, one tier per
networking protocol the node speaks. Each tier is an app's contract, so
developing a tier *is* developing an app — the missing piece and the app
are the same artifact. This is why finding what is missing generates the
apps: the gap names the app.

| Tier | Protocol | The handshake | The app it completes |
|---|---|---|---|
| **T0** | the transport (any) | ML-KEM-1024 + ML-DSA-87 (the PQ base) | the `keys` / `pqsec` app |
| **T1** | UDP `:8724` | the Crown factor over the datagram: the point proves, the peer verifies | the `vinea` app (the peer mesh) |
| **T2** | TCP/HTTP `:80` | the scenic bypass: the Crown factor carried in the door reply (`/pk`, `/sig`) | the `door` app (the doorway) |
| **T3** | the mesh (S2S) | the relay: each hop re-proves, so a compromised hop cannot forge the next | the `swarmnet` app (the state fabric) |
| **T4** | the tunnels | the reverse tunnel: the Crown factor at both ends, so neither end trusts the path | the `tunnel` app |
| **T5** | the browser | the wasm handshake: the same factor in `zxv_app.wasm`, verified client-side | the `web` app (the world computer) |
| **T6** | the storage | the content proof: a CID's bytes verified against the Crown factor | the `ipfs_node` app |

Each tier is the previous plus the protocol's own proof, so the ladder is
**cumulative**: a peer at T4 has proven T0–T4. A break at any tier does
not weaken the tiers below it — the series holds at every rung.

### Why this generates the apps

Every tier above is a real app in the 407 (the `vinea`, `door`,
`swarmnet`, `tunnel`, `web`, `ipfs_node` apps). So when the H100 finds a
missing tier, it has found a missing **app** — and the app's contract is
the tier's specification:

- the tier's **protocol** is the app's `needs` (the transport capability);
- the tier's **handshake** is the app's `provides` (the entries that
  prove and verify);
- the tier's **tests** are the three spaces at that protocol (the honest
  peer verifies, the unproven is refused, the boundary is deterministic).

That is the mechanism: **a gap in the security ladder is a gap in the app
catalogue**, and closing one closes the other. The lattice that makes the
checks deterministic makes the tier tests deterministic too — the same
point, the same projection, the same verify, at every protocol.

### What the H100 must build and test, per tier

1. **The tier's proof entry**: `provides <tier>_prove truth <the tier's inputs>`
   — the close-vector producer at that protocol's framing.
2. **The tier's verify entry**: `provides <tier>_verify truth <the proof>`
   — the exact projection check, no floats.
3. **The cumulative test**: a peer that passes tier N passes tiers 0..N
   (the ladder's monotonicity).
4. **The break test**: a peer that fails tier N still passes tiers 0..N-1
   (a lower tier is not weakened by a higher failure) — the negative space
   at each rung.
5. **The protocol test**: the tier's framing survives the protocol's
   realities (a fragmented datagram, a lost packet, a reordered relay) —
   the neutral space at each rung.

## The paradox: expensive to make, nearly free to run

SCP-144 is the **standard** — every node, every protocol tier, every
surface speaks it. And it exhibits a deliberate paradox:

```
THE MAKING (the construction)      the H100, the full lattice, the
                                   proofs, the audits — enormous compute
THE RUNNING (the operation)        one hash, one integer compare — almost
                                   none, on any machine
```

The ratio is the point. Building the architecture costs a great deal of
compute (the H100 drafting 407 apps, the proofs, the verification tiers);
**running it costs almost nothing** — because everything expensive was
paid *once*, at construction, and the result is **structure**, not
computation.

### Why the running side is nearly free

| The operation | The cost at run time |
|---|---|
| the SCP-144 point | one SHA3-256 over the seed |
| the public invariant | one integer norm + a few residues |
| the handshake proof | the peer already holds the point — no search |
| the verify | one equality comparison |
| the routing decision | one lattice distance (O(1)) |
| the record encoding | the canonical form, already computed |

No elliptic-curve scalar multiplication per handshake. No lattice search
by the honest peer. No floating point. No allocation. **The expensive
computation happened once, when the structure was built** — and the
structure is what runs.

### The negentropy connection

This is negentropy in its clearest form: the construction *pays* entropy
(compute, time, energy) to *remove* it permanently. The result is a
system whose ongoing cost is near zero because its order is fixed. A
system that fought entropy at run time would need the same enormous
compute on every device, forever; this one needs it once.

So the paradox resolves: **the compute we are spending now is the compute
we will never spend again.** That is why it runs on any machine — the
expensive part is already done, and what remains is a hash and a compare.

### The standard's reach

SCP-144 is the **native networking protocol**, not an option:

- **Every tier**: T0 (the PQ base) through T6 (the storage proof), each
  protocol's handshake carries the Crown factor.
- **Every surface**: bare metal, the VM, the browser wasm, the three
  desktops.
- **Every node**: the fleet, the bootstrap ring, the user's device.
- **Every direction**: the handshake, the routing, the encoding, the
  verification.

And because the running cost is near zero, there is no device too small
to be a full participant: a phone is a node, a browser is a node, a
low-power machine is a node. The paradox is what makes it universal.

## The traditional standards — compatibility and protection

NIST's post-quantum standards (ML-KEM, ML-DSA, SLH-DSA) and Pretty Good
Privacy are **legacy interfaces**, not the native protocol. SCP-144 is
native. The relationship is threefold:

### 1. Native: SCP-144 with SCP-144

When both ends speak SCP-144 (ZXV to ZXV, or ZXV to any system that has
it), the native handshake runs: the point, the projection, the
close-vector proof, the tier ladder. No legacy layer, no translation.

### 2. Compatible: the traditional standards are accepted

A peer using NIST standards or PGP is **not refused**. The system bridges:
the legacy handshake completes, and the session is established. A
traditional user is a full participant, not a second-class one.

### 3. Protective: the legacy is wrapped, so Shor cannot reach it

This is the important part. A legacy standard (RSA, ECDSA, ECDH, PGP's
classical keys) is **obsolete against Shor's algorithm** — a sufficiently
large quantum computer breaks it outright. SCP-144 does not merely accept
such a peer; it **protects** them:

```
legacy handshake (broken by Shor)
        wrapped inside
SCP-144 (the Crown factor, lattice-hard)
```

The legacy session's secret is carried **inside** an SCP-144 tunnel, so
even if the legacy key is later broken, the session's content is
protected by the lattice factor. The user keeps their familiar standard;
the system fills the gap the standard cannot.

### Why this matters

- **No user is left behind.** A traditional-standard user is compatible
  *and* protected — the system does not punish them for an outdated
  choice, it shields them from its consequence.
- **No forced migration.** The native protocol is faster and safer, but
  adopting it is a choice, not a requirement.
- **The protection is honest.** The wrap does not make the legacy cipher
  stronger; it makes the *session* safe by carrying it in a tunnel the
  legacy break cannot open. That is a real guarantee, stated precisely.

### The three spaces, per compatibility path

- *positive*: a legacy peer completes the handshake and the session is
  wrapped in SCP-144;
- *negative*: a legacy peer's broken key does not expose the wrapped
  session (the negative-space test: the tunnel holds even when the inner
  cipher is assumed broken);
- *neutral*: a peer offering both standards negotiates deterministically
  (native preferred, legacy wrapped).

### The standard's statement

```
SCP-144 native.  NIST and PGP compatible.  Legacy wrapped, so Shor's
algorithm cannot reach a user who has not migrated.
```

The native protocol costs almost nothing to run (the paradox), so
offering it to every peer — and protecting the ones who use something
else — costs the system almost nothing either.
