# Verified geometry — what φ actually carries, and what it does not

_2026-08-09. Every claim below is marked [COMPUTED] (exact arithmetic, re-derived
here) or [THEOREM] (cited, not machine-checkable). Nothing is asserted from
memory. Load-bearing algebra was done in exact ℚ(√5), not floating point._

**Methodology in force:** beauty proposes, the law disposes. φ generates the
candidate; verification decides. This document exists because that method
overturned one of my own claims — see §4.

---

## 1. The regular forms, by dimension [COMPUTED]

Enumerated by testing positive-definiteness of the Schläfli/Coxeter matrix
(2 on the diagonal, −2cos(π/pᵢ) off-diagonal) at 50 digits, pᵢ ∈ [3,200],
dims 2–11.

| dim | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 |
|---|---|---|---|---|---|---|---|---|---|---|
| count | ∞ | **5** | **6** | **3** | 3 | 3 | 3 | 3 | 3 | 3 |

The owner's counts are exactly right. In every dimension ≥ 5 only the simplex
{3,…,3}, the hypercube {4,3,…,3} and the cross-polytope {3,…,3,4} survive.

**Why it collapses to 3.** Positive-definiteness is inherited by every principal
submatrix, so *every contiguous sub-symbol must itself be valid* — each facet and
each vertex figure must be a regular polytope one dimension down. That filter
kills the exceptional branches at rank 4: [COMPUTED] det{3,4,3} = +1 but
det{3,4,3,3} = **0 exactly** (affine — it tiles ℝ⁴ instead of closing), and
det{3,3,5} = +0.1459 but det{3,3,3,5} = **−0.4721** (hyperbolic — the cells
over-fill and open into hyperbolic space). Only the infinite families A and B
remain; Bₙ carries two (cube and dual), Aₙ is self-dual. Hence three.

**This is the geometric form of "primes are the scaffolding of what is
possible."** Dimension decides which regular forms *can* exist, the same way a
prime state-count decides which fields can exist. Neither is a preference.

## 2. THE φ CRITERION — the clean statement [COMPUTED]

2cos(π/3) = 1, 2cos(π/4) = √2, **2cos(π/5) = φ exactly**, 2cos(π/6) = √3.
The Schläfli matrix entries generate the polytope's coordinate field. Therefore:

> **φ enters the structure of a regular polytope if and only if a 5 appears in
> its Schläfli symbol.** Everything else lives over ℚ(√2) or ℚ.

| φ-bearing | φ-free |
|---|---|
| icosahedron {3,5}, dodecahedron {5,3} | tetrahedron, cube, octahedron |
| 120-cell {5,3,3}, 600-cell {3,3,5} | 5-cell, tesseract, 16-cell, **24-cell** |

[COMPUTED] icosahedron vertices (0,±1,±φ) cyclic: all 12 have |v|² = φ+2 = 3.618…
(single value ⇒ equidistant), min dist² = 4 ⇒ edge exactly 2, achieved by exactly
**30** pairs = the correct edge count. Dihedral 138.18968510° = 2·arctan(φ²)
exactly; dodecahedron 116.56505118° = 2·arctan(φ) exactly. Tetra/cube/octa
dihedrals are arccos(±1/3) and 90° — **no φ**.

**⚠ TRAP, hit and corrected during verification.** A naïve ℝ⁴ embedding of the
5-cell yields coordinates containing 1/√5, which string-matches as "φ". It is
not. The 5-cell has a fully **rational** realization (e₁…e₅ ∈ ℝ⁵, all pairwise
dist² = 2) and is φ-free. **√5 ≠ φ. Never let √5 masquerade as φ.**

## 3. THE ICOSIAN → E8 CHAIN — ALL THREE PARTS CONFIRMED [COMPUTED]

This is the strongest result in the document and the one worth building on.

**(a)** The 600-cell's 120 vertices, generated exactly: 8×(±1,0,0,0) +
16×(±½,±½,±½,±½) + 96×(±φ/2,±½,±1/(2φ),0) even perms = **120** distinct, all
norm 1, min edge² = **1/φ²** exactly, **720** edges. Bonus: the set is **closed
under quaternion multiplication** — it is the binary icosahedral group 2I.

**(b) The icosian ring IS E8 as a lattice — verified from the ground up, not
taken on authority.** The ℤ-span of the 120 icosians has rank 8 and equals their
ℤ[φ]-span. Under the Conway–Sloane form Q(q) = 2(a+b) the Gram matrix is
**integral, even, det = 1** ⇒ even unimodular of rank 8. Rather than invoke the
uniqueness theorem, the verification went further and computed the theta series
directly: **240 vectors of norm 2, 2160 of norm 4** — E8 exactly — then extracted
**120 positive roots, 8 simple roots**, Cartan matrix simply-laced with det 1,
node degrees [1,1,1,2,2,2,2,3], one branch node with arm lengths **[1,2,4]**.
That is the E8 Dynkin diagram, derived from the icosians.

**(c) E8's 240 roots are two 600-cells in ratio φ.** Under the real embedding
√5 ↦ +2.236…, the 240 roots split into exactly two shells of **120** each, with
norms 1/φ² and 1. Ratio of squared radii = **φ² exactly** (an identity in ℤ[φ],
not a numerical fit). Each shell rescaled to unit radius is *set-identically* the
120 icosians. So: **radius ratio exactly φ, no rotation needed.**

⚠ Honest form: this is a ℤ[φ]-module statement plus a norm formula. The map
ℝ⁸ → ℝ⁴ is the real embedding of the ℤ[φ]-structure, **not** an isometry or an
orthogonal projection. Do not read it as "union two 4-D point sets to get an
8-D lattice." Attribution: Conway & Sloane SPLAG ch. 8; Elser & Sloane 1987.

**So φ does generate the provably-optimal 8-dimensional packing.** That chain —
φ → icosahedron → 600-cell → icosian ring → E8 → optimal packing — is real, and
every link was checked.

## 4. THE FIBONACCI-DIMENSION CLAIM — WEAK. I OVERCLAIMED; HERE IS THE RETRACTION

I told the owner: *"every dimension in which sphere packing is solved is
Fibonacci-generated,"* reading 24 as 3×8 = F(4)·F(6). **Verification does not
support that, and I am withdrawing it.**

[COMPUTED] Solved dimensions S = {1, 2, 3, 8, 24} — proven optimal there and
**nowhere else**.

| | |
|---|---|
| S ∩ Fibonacci | {1, 2, 3, 8} — four |
| S ∖ Fibonacci | **{24}** — 21 and 34 straddle it; not close by any construction |
| Fibonacci ≤ 24 **not** solved | **{5, 13, 21}** — three misses, including 5 |

The overlap is carried almost entirely by {1,2,3} — three consecutive small
integers belonging to nearly every "interesting" integer set. Strip them:

> S′ = {8, 24}. Fibonacci in [4,34] = {5,8,13,21,34}. **S′ ∩ F′ = {8}. One
> number.**

[COMPUTED] chance baseline: naively P(≥4 of 5 hit Fibonacci) = 1.4%, which looks
impressive — but conditioned on the {1,2,3} freebie, P(≥1 of the remaining two is
Fibonacci) = **0.352**, expected hits 0.381, observed 1. **Unremarkable.**

And 24 belongs to a *different* family of special integers entirely: 4!, 2³·3,
the Leech dimension, the critical dimension of the bosonic string. Reading it as
3×8 is post-hoc.

**Ruling: do not build load-bearing structure on "Fibonacci dimension ⇒ special
dimension."** The φ-connection that *is* real is elsewhere and is theorem-grade:
§2 (5 in the symbol ⇔ φ) and §3 (icosian ring = E8, radius ratio exactly φ). Keep
those. Drop the coincidence.

**What survives of the owner's thesis, unharmed:** 5 is prime *and* F(5); 5-fold
symmetry *is* φ (φ = 2cos36°); and a prime state-count admits GF(p). Those are
the scaffolding claims, and they hold. It was only the packing-dimension bridge
that failed.

## 5. Sphere packing and kissing — the ground truth [THEOREM + COMPUTED]

| dim | packing density | lattice | proof |
|---|---|---|---|
| 1 | 1 | ℤ | trivial |
| 2 | π/√12 = 0.9068996821 | A₂ hex | Fejes Tóth 1943 |
| 3 | π/√18 = 0.7404804897 | FCC | Hales 1998/2005, Flyspeck 2017 |
| 8 | π⁴/384 = 0.2536695079 | **E8** | **Viazovska 2016, Annals 185 (2017)** |
| 24 | π¹²/12! = 0.0019295743 | **Leech** | **Cohn–Kumar–Miller–Radchenko–Viazovska 2017** |

Densities re-derived from ball volume ÷ covolume, matching to 30+ digits.
**Dimensions 4–7 and 9–23 are NOT solved.** (The weaker *lattice* problem is
solved in 1–8 and 24.) E8 and Leech are universally optimal (Annals 196, 2022).

**Kissing numbers** are solved in **{1,2,3,4,8,24}** — the packing set **plus 4**:
2, 6, 12, **24** (D₄, Musin 2008), **240** (E8), **196560** (Leech). Note 4 is not
Fibonacci either.

## 6. The 24-cell — the exception worth knowing [COMPUTED]

{3,4,3}, 24 vertices, 96 edges, 96 faces, 24 octahedral cells. **Self-dual** —
f-vector (24,96,96,24) is a palindrome. Its 24 vertices are *set-identically* the
minimal vectors of the **D₄ lattice**, so it realizes the 4-D kissing number 24.

It has **no analogue in any other dimension**: it is the Coxeter group F₄, which
exists only at rank 4, because |W(F₄)|/|W(D₄)| = 1152/192 = 6 = |S₃| = Out(D₄) —
**triality**, the exceptional outer automorphism unique to D₄. The 24-cell is the
geometric shadow of triality.

**And it is φ-free.** The one 4-D polytope with no lower analogue, realizing the
only kissing number solved outside the packing set, is built over ℚ. A reminder
that not every structural gift comes through φ.

## 7. What this licenses us to build

1. **E8 as the 8-dimensional lattice**, constructed *via the icosian ring* — φ is
   the generator, and the construction is verified end to end. `ISOMETRY_LIFT_M8`
   is already declared in `m5_types.h` and **has no implementation**: that is the
   hook, already named, waiting.
2. **The φ ⇔ 5 criterion** as the test for whether a proposed structure genuinely
   carries φ or merely mentions it.
3. **Exact ℤ[φ] arithmetic** — pairs of integers (a,b) meaning a+bφ — as the
   numeric substrate. `rat_t` (overflow-detecting) is the right base; `cyc13_t`
   already proves the pattern for a 13-dimensional exact lattice.
4. **Not** a Fibonacci-dimension privilege rule. §4.
