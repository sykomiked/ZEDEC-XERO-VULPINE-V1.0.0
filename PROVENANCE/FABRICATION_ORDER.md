# Fabrication order — E8 as the parallel schedule, Leech as the frame

_Owner directive: build it the way an object is fabricated — right order, some
tasks in parallel, perpendicularities wired at synchronised steps. E8 scaffolding
to dimension 8, the 24-dimensional construct beyond it, 13 layers native._

_Everything marked [VERIFIED] was computed earlier in this project
(GEOMETRY_VERIFIED.md). Everything marked [ANALOGY] is a way of thinking and
schedules nothing._

---

## 0. The one idea that does real work

A root system is not decoration here. Its **Cartan matrix already answers the
scheduling question**:

> Two nodes with a **zero** entry are non-adjacent. They **commute** — they can be
> built in parallel.
> Two nodes with a **non-zero** entry are adjacent. They interact — they must
> **synchronise**.

That is precisely "perpendicularities wired at synchronised steps", and it is
computable rather than felt. Orthogonal means independent means parallel. This is
the whole of the transferable content, and it is enough.

## 1. E8 gives an 8-node schedule with a known shape

[VERIFIED] The E8 Dynkin diagram was derived from the icosians in this project —
8 simple roots, Cartan determinant 1, simply laced, node degrees
`[1,1,1,2,2,2,2,3]`, one branch node with **arm lengths [1,2,4]**.

Two consequences that matter for building:

**It is a tree, so it is 2-colourable.** Every tree is bipartite. Colour the 8
nodes alternately and you get two independent sets: every node in a set is
non-adjacent to every other node in that set, so **an entire colour class builds
in parallel**, then a synchronisation barrier, then the other class. A two-phase
schedule, forced by the diagram rather than chosen.

**Height gives the canonical ordering.** [VERIFIED] E8 has 120 positive roots and
Coxeter number **h = 30**. Positive roots are graded by height, and height is a
build level: a root of height *k* is a sum of *k* simple roots, so it cannot
exist before its parts. That is a dependency-respecting total order, derived, not
invented.

So E8 scaffolds **8 independent axes** with a proven parallel structure. Not more
than 8 — E8 is 8-dimensional, and pretending otherwise would be the kind of
overreach this project has spent a week removing.

## 2. Why 13 needs the 24-dimensional frame

13 > 8, so the thirteen layers do not fit in E8. They fit in the next frame up,
and the relationship between the two is real:

- [VERIFIED] Sphere packing is **proven optimal in dimensions 1, 2, 3, 8 and 24
  only**. 8 and 24 are the two high-dimensional frames that are *solved*, not
  merely known.
- [THEOREM] The **Leech lattice (24D) is built from three E8 blocks** (Turyn
  construction). 24 = 3 × 8.
- 13 ≤ 24, so thirteen axes embed in the Leech frame with room.

The engineering reading is unusually clean: **E8 is the block you build once and
instantiate three times.** Build the 8-node scaffold, prove it, replicate it. The
13 layers occupy positions in the 24-dimensional frame; the 11 unused positions
are headroom, not waste.

[VERIFIED, and stated to keep the record straight] 13 is prime and F(7), so GF(13)
exists and `cyc13_t` is a genuine exact 13-dimensional lattice with a C₁₂ Galois
group. That is why 13 is native here. It is **not** because 13 is a special
packing dimension — it is not one, and the earlier claim that Fibonacci dimensions
are privileged was retracted after measurement.

## 3. The fabrication rules that transfer

[ANALOGY, but each maps to a rule we already enforce]

| additive manufacturing | here |
|---|---|
| nothing prints in mid-air | no layer before the layers it depends on — the DRC rule |
| layer adhesion is where parts fail | the interface between layers is where defects live; `modbind` declares those bonds |
| support structures are temporary and removed | stubs and mocks must be *deleted* when the real layer lands, not left in |
| print orientation sets anisotropy | build order determines where the system is weak; choose it deliberately |
| you cannot inspect the interior after the fact | hence DFT — every initcall reports a status *as it is laid down* |

The third row is the one this codebase keeps failing. Temporary scaffolding that
was never removed is exactly what the SNES APU fake is: two bytes that convert a
genuine hang into a fake pass, left in because it made things work.

## 4. The schedule, concretely

Thirteen layers, coloured into parallel waves by the adjacency rule of §0. Layers
that share no interface build simultaneously; layers that touch synchronise.

```
wave 1  (no interfaces between them — fully parallel)
        L0 substrate    L4 devices*     L9 composition*
wave 2  barrier: L0 must exist before anything computes
        L1 causality    L5 services*
wave 3  L2 memory       L6 economy*
wave 4  L3 trust        L7 surface*     L10 intelligence*
wave 5  L8 distribution L11 sovereignty*
wave 6  L12 commons*
```

`*` = specification only today; the row exists so dependencies can be stated, not
because it is built.

The barriers are not schedule padding. Each is a **verification gate**: the wave
below must pass DRC, LVS and its triad rule before the next wave integrates. That
is the tape-out discipline applied per wave rather than once at the end.

## 5. What this does not claim

The OS is not E8. It does not compute in 24 dimensions. Nothing here asserts a
physical or metaphysical correspondence.

What is claimed, and all that is claimed: **a root system supplies a canonical
parallel schedule and a canonical build order, both computable from a Cartan
matrix.** Given thirteen interdependent layers to build simultaneously without
tangling them, that is a genuinely useful thing to have, and it is the reason to
use E8 here rather than a preference for the shape.

The test of whether this section is honest: if the Cartan matrix stopped
determining which layers may build in parallel, this document would have no
remaining content. That is the correct amount of content for it to have.
