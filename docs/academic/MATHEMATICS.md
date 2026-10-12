<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# The mathematics of the ZXV kernel

*A technical report for mathematicians. Part of the [ZXV academic series](README.md).*

## Abstract

The ZXV kernel computes with integers only: floating point is a build error in kernel images. This report sets out the mathematical objects the source code actually implements and says how far each property is established. The objects are: exact rationals over 64-bit integers with checked overflow; Q16.16 fixed-point arithmetic and block quantisation for neural-network inference, including an integer rotary position encoding whose phases live in $\mathbb{Z}/2^{64}\mathbb{Z}$; the ring $\mathbb{Z}[\varphi]$ and an icosian construction of $E_8$; nearest-point quantisers for $E_8$ and the Leech lattice; a Fibonacci-weighted largest-remainder apportionment; a four-valued truth structure; a systematic Cauchy Reed–Solomon erasure code over $\mathrm{GF}(2^8)$; and SHA-based hash chains. For each object we give a formal definition taken from the code and sort its properties into four groups: proved in Lean 4 on a hand-written model, model-checked in TLA+ over small bounds, tested by host or CI programs, or stated as intent only. Three results stand out. Most golden-ratio components gave no benefit, or did harm, when measured on a real 1.5-billion-parameter model. The implemented four-valued conjunction is not Belnap's. The rational type loses its overflow detection on 32-bit targets.

## 1 Introduction and scope

ZXV is an experimental operating system and AI runtime that runs on ordinary classical hardware. The "integer only" rule and its motivation (bit-identical results across CPUs, so that one machine can re-check another's work) are explained for general readers in [docs/supplemental/INTEGER_ONLY_KERNEL.md](../supplemental/INTEGER_ONLY_KERNEL.md). This report covers only the mathematics. The logic is treated at more length in [PARACONSISTENT_LOGIC.md](PARACONSISTENT_LOGIC.md), test design in [TESTING_METHODOLOGY.md](TESTING_METHODOLOGY.md) and physical interpretation in [PHYSICS_AND_QUANTUM_FRAMING.md](PHYSICS_AND_QUANTUM_FRAMING.md).

**On quantum vocabulary.** Some project documents use words such as "superposition" or "interference" (for example `TRIT_GLUT_PLUS`, "constructive superposition", in `kernel/include/m5_types.h`, and rule T14 in `kernel/src/tensor/zt.h`). Mathematically, every object in this report is a classical integer, rational or finite-field object. Multi-valued states are elements of a finite set. Weights are nonnegative rationals that behave like probabilities, not complex amplitudes, so no interference in the quantum sense takes place. ZXV does not turn a classical computer into a quantum computer and claims no quantum speedup. It pairs these quantum-inspired features with post-quantum cryptography (FIPS 203 ML-KEM, FIPS 204 ML-DSA, FIPS 205 SLH-DSA; see `docs/PQ_MATRIX.md`) and keeps compatibility with conventional computing.

**What "proved" means here.** The files in `proofs/` are *models* of C functions, transcribed by hand into Lean 4 (core library only, toolchain `leanprover/lean4:v4.15.0`) or into TLA+. There is no refinement proof that links a model to the C source or the binary (`proofs/README.md`, "Read this first"). `proofs/check.sh` rejects any Lean file that contains `sorry`, `admit` or `axiom`. The CI job `proofs` in `.github/workflows/ci.yml` runs both checkers. A TLC result holds only for the bounds in its `.cfg` file.

Notation. $\mathbb{Z}_{64} = \{x \in \mathbb{Z} : -2^{63} \le x < 2^{63}\}$; $F_n$ is the Fibonacci sequence with $F_1 = F_2 = 1$; $\varphi = (1+\sqrt5)/2$ and $\psi = 1-\varphi$.

## 2 Exact rational arithmetic

The kernel contains three rational types, and they behave differently.

**Definition 2.1 (`rat_t`, `kernel/src/rational/rational.h`).** A value $(n, d, v)$ with $n, d \in \mathbb{Z}_{64}$ and validity flag $v$. It is *valid in normal form* when $v$ holds, $d > 0$, $\gcd(\lvert n\rvert, d) = 1$, and $\lvert n\rvert \le 2^{63}-1$. `rat_from_int` rejects $n = -2^{63}$ explicitly, so that negating a valid value cannot overflow. It denotes $n/d \in \mathbb{Q}$. Every invalid value denotes a single absorbing element $\bot$.

Write $R = \{q \in \mathbb{Q} : q = n/d$ in lowest terms with $\lvert n\rvert, d \le 2^{63}-1\}$. Under the maps `rat_add`, `rat_mul` and `rat_div`, $R \cup \{\bot\}$ is a partial algebra. Each operation computes the exact rational result through 128-bit intermediates (`wide_t = __int128`), reduces it by the Euclidean gcd (`reduce`), and returns $\bot$ if the reduced numerator or denominator leaves $R$. `rat_add` forms $a/b + c/d$ over $\operatorname{lcm}(b,d)$ via $g=\gcd(b,d)$. `rat_mul` cross-reduces with $\gcd(a, d)$ and $\gcd(c, b)$ before multiplying. Both are the classical techniques in Knuth (1997, §4.5.1). Because normal forms are unique, `rat_eq` is structural equality. `rat_cmp` compares $n_1 d_2$ with $n_2 d_1$ in 128 bits and places $\bot$ above every valid value. It is therefore a total preorder whose restriction to $R$ is the usual order. `rat_eq` is deliberately false on $\bot$, so equality is a partial equivalence relation and `rat_cmp(⊥,⊥) = 0` does not agree with it there (comment in `rat_cmp`).

**Proposition 2.2 (by inspection; tested on both paths).** Every arithmetic result of `rat_t` is either the exact rational value in normal form or $\bot$. No result wraps. `rat_to_string` prints a terminating decimal exactly when $d = 2^a5^b$ with $\max(a,b) \le 18$, and prints `n/d` otherwise.

*Targets without `__int128`.* GCC does not provide `__int128` on 32-bit targets, and `build_system/Makefile.arm32` and `Makefile.riscv32` both compile `rational.c`. Until 2026-10-12 the fallback for those targets did plain 64-bit arithmetic with no bounds check, so results could wrap. The fallback now forms the few 128-bit quantities it needs from two 64-bit limbs (the product $|n_1| d_2$, the Knuth numerator $t$ of `rat_add`, the scaled remainder in `rat_to_fixed`) and uses checked 64-bit operations elsewhere. It returns the same normal form, or the same $\bot$, as the 128-bit path. `kernel/src/rational/test_rational_narrow.c` checks this against an `__int128` oracle on random inputs, and the tier 1 and tier 3 rational suites run on both paths. The fallback is exercised on a 64-bit host with `-DRAT_NO_WIDE`; it has not been run on 32-bit hardware.

**Definition 2.3 (`rational_t`, `kernel/include/m5_types.h`; `kernel/src/rmag`).** A pair $(n,d) \in \mathbb{Z}_{64}^2$ with no validity flag. The normaliser is `rational_normalize`, and the arithmetic is `rmag_add_quotas` and the related functions in `rmag_core.c`. **Definition 2.4 (`rmag_rational_t`, `rmag.h`).** A sign bit plus magnitudes $(n, d) \in [0, 2^{64})^2$ with $d \ne 0$.

The Lean file `proofs/rational_bounds/RationalBounds.lean` models these two types, not `rat_t`. It proves the following (docs/FORMAL_INVARIANTS.md R1–R7):

- `euclid_eq_gcd`: the `m5_gcd` loop computes $\gcd$.
- `normalize_den_pos` and `normalize_value`: for $d \ne 0$, normalisation gives $d>0$ and the same value.
- `divOld_by_zero`: the old `rmag_div_quotas(5/1, 0/1)` returned $1/1$, a bug since fixed. `divChecked_sound`: the checked division, when it succeeds, is exact, has $d>0$ and fits $\mathbb{Z}_{64}$.
- `magCmp_spec` and `ratCmp_spec`: the comparison avoids multiplication by a continued-fraction recursion and returns exactly $\operatorname{sgn}(a_n b_d - b_n a_d)$.
- `mulU_exact`: multiplication is exact or saturates to $(2^{64}-1)/1$.
- `addU_comm`: addition is commutative on all inputs. This needed a C fix to the saturating path.
- `reduceU_exact`: reduction preserves the value.

The same file records a negative result. `addQ_overflow_witness` shows that `rmag_add_quotas(2^62/1, 2^62/1)` overflows `int64`, which is undefined behaviour in C. `addQ_comm` and `mulQ_comm` hold over mathematical integers only. A third lemma, `pay_ledger_no_overflow`, shows that under the bounds enforced by `pay_ledger_post` (balances $<2^{62}$, line deltas $\le 2^{59}$, at most 8 lines) every sum fits in $\mathbb{Z}_{64}$.

## 3 Fixed-point arithmetic and the integer tensor engine

**Definition 3.1 (Q16.16).** A value $x \in \mathbb{Z}_{32}$ (type `zt_fx`) denotes $x/2^{16}$ (`kernel/include/zxv_fixed.h`, `kernel/src/tensor/zt.h` rule T1). `fx_mul_q16` rounds toward zero. `fx_udiv64` performs 64/64 division by shift-and-subtract, because freestanding 32-bit targets have no division helper.

**Definition 3.2 (block quantisation, T2).** A block $v \in \mathbb{Z}^{32}$ of Q16 values is stored as `zt_q8_t`, a scale $s$ and codes $q_i \in [-127, 127]$. In `zt_quantize`, $s = \lceil \max_i\lvert v_i\rvert / 127 \rceil$ and $q_i = \operatorname{sgn}(v_i)\min(127, \lfloor (\lvert v_i\rvert + \lfloor s/2\rfloor)/s \rfloor)$. Because the ceiling guarantees $127s \ge \max\lvert v_i\rvert$, no code clips, and the reconstruction error $\lvert v_i - q_i s\rvert$ is at most about $s/2$ Q16 units.

**Exactness of the inner products (T21, T22; `zt_kern.h`).** For GGUF Q8_0, Q4_K and Q6_K weights, each block of 32 forms $\sigma = \sum_i w_i x_i$ exactly in 32 bits ($\lvert \sigma\rvert \le 32\cdot128\cdot 2^{15} = 2^{27}$). The block's floating-point (f16) scale is decoded as an integer mantissa and exponent, $m\cdot 2^{e}$, with $\lvert m\rvert < 2^{11}$. The term is $\sigma m 2^{e'}$ with floor rounding, clamped to $\pm 2^{52}$ (`zt_term52`). Terms are summed in 64 bits, and the row is rounded once to Q16. Integer addition without overflow is associative and commutative, so any SIMD or threaded order that forms the same block terms produces the same bits. That property is what the cross-architecture identity in §1.5 of the benchmark report relies on.

**Golden scales (T3).** Using $\varphi^k = F_k\varphi + F_{k-1}$ and $\varphi^{-n} = (-1)^n(F_{n+1} - F_n\varphi)$, `zt_phi_pow` computes $\varphi^k$ in Q16 for $-24 \le k \le 40$ from Fibonacci integers and a 64-bit constant for $1/\varphi$. When `golden` is set, `zt_quantize` rounds the scale up to the next $\varphi^k$. The idea is that the step between available scales is $\varphi$ rather than 2.

**Rotary positions (T20, `zt_rope.c`).** Pair $i$ of a head has angular frequency $\theta_i = b^{-2i/n}$. `zt_rope_init` stores $f_i = \operatorname{round}(2^{64}\theta_i/2\pi) \in \mathbb{Z}/2^{64}\mathbb{Z}$, measured in turns. The base $b$ is read from its IEEE single-precision bits. $\log_2 b$ is computed by repeated squaring to 56 fraction bits, and $2^{-t}$ by a product of tabulated constants $2^{-2^{-k}}$, generated with 80-digit decimal arithmetic by `gen_rope_tables.py`. The phase at position $p$ is $p f_i \bmod 2^{64}$: a single wrapping multiplication, so the reduction modulo $2\pi$ is exact. The angle error is $p\,\delta_i$, where $\delta_i$ is the error in $f_i$ (about $2^{-55}$ relative, per the header). The error therefore grows linearly in $p$ but from a very small constant. Sine and cosine come from a 1024-entry Q30 table plus a degree-4 Taylor correction on the residual angle (below $2\pi/1024$ rad).

## 4 The ring $\mathbb{Z}[\varphi]$ and $E_8$ from the icosians

**Definition 4.1 (`zphi_t`, `kernel/src/e8/zphi.h`).** Pairs $(a,b) \in \mathbb{Z}_{64}^2$ denoting $a + b\varphi$, with $(a+b\varphi)(c+d\varphi) = (ac+bd) + (ad+bc+bd)\varphi$. Conjugation is $a+b\varphi \mapsto (a+b) - b\varphi$. The norm $N(a+b\varphi) = a^2+ab-b^2$ and trace $2a+b$ are integers. Overflow sets a validity flag and propagates, as in `rat_t`. `zphi_selfcheck` tests closure, conjugation and multiplicativity of $N$.

**Construction 4.2 (`e8.c`).** The module builds the 120 unit icosians (vertices of the 600-cell, the binary icosahedral group $2I$) as quaternions over $\mathbb{Z}[\varphi]$, scaled by 2. It fixes eight of them, `BASIS` $b_0,\dots,b_7$, and equips their $\mathbb{Z}$-span with the rational quadratic form $Q(q) = (U+V)/2$, where $U + V\varphi = \sum h_i^2$. The resulting Gram matrix `E8L_ICOSIAN_GRAM` is even with determinant 1, so the lattice is isometric to $E_8$, the unique even unimodular lattice of rank 8 (Conway & Sloane 1999, ch. 8). The 240 roots are produced as the 120 icosians together with their images under multiplication by the unit $1/\varphi$. `e8_selfcheck` recomputes the Gram matrix from the quaternions, checks unimodularity by exact elimination, checks closure of $2I$ and counts the roots. `e8_lift_m5` embeds $\mathbb{Z}^5$ as the sublattice spanned by $b_0,\dots,b_4$. The header says plainly that this *defines* a metric on the five-coordinate "M5" state space by pull-back rather than preserving an existing one.

**Definition 4.3 (doubled coordinates, `e8_lattice.h`).** $2E_8 = \{v \in \mathbb{Z}^8 : v_i$ all even or all odd, $\sum v_i \equiv 0 \pmod 4\}$, with $\langle v, w\rangle_{E_8} = (v\cdot w)/4$. The header tabulates the Bourbaki simple roots, the Cartan matrix and an explicit isometry from the icosian basis (`E8L_ICOSIAN2`, with inverse `E8L_FROM2`). `test_zt_e8.c` checks these claims from the tables.

The header of `e8.h` cites a file `PROVENANCE/GEOMETRY_VERIFIED.md` for offline verification. That file is not in the repository.

## 5 Lattice vector quantisation ($E_8$, Leech)

**Quantiser (T17, `zt_lattice.h`).** `zt_e8_nearest` is the Conway–Sloane decoder for $E_8 = D_8 \cup (D_8 + \tfrac12\mathbf{1})$ (Conway & Sloane 1982). For each coset it rounds every coordinate, fixes the parity of the sum by re-rounding the worst coordinate, and returns the closer candidate. The codebook is the ball of norm at most 8: shells of 1, 240, 2160, 6720 and 17520 points, which are the coefficients of $\Theta_{E_8} = 1 + 240\sum_m \sigma_3(m)q^{2m}$. That gives $26641 < 2^{15}$ points, so eight weights cost one 15-bit index: 1.875 bits per weight plus a block scale ($1.05\times$ RMS, tuned empirically). Points outside the ball are pulled in by a bisection on a shrink factor followed by a greedy root-step search. The header calls this a heuristic: the result always lies in the codebook but is not proved to be the nearest codebook point.

**Leech lattice.** `zt_leech_nearest` decodes $\Lambda_{24}$, in its $\sqrt8$-scaled integer form, by exhaustive search over the 8192 cosets attached to the extended Golay code. That code is generated as the cyclic code with $g(x)=x^{11}+x^{10}+x^6+x^5+x^4+x^2+1$ plus a parity bit. `test_zt_lattice.c` checks the Golay weight distribution (1, 759, 2576, 759, 1) and the count of 196560 minimal vectors. The decoder is brute force, not a hexacode decoder, and costs about 400 times as much per weight as $E_8$ (header).

**Measured results.** On Gaussian data with one scale per 32 weights, `test_zt_lattice` records an SNR of 9.89 dB for $E_8$ at 1.875 bits per weight against 9.59 dB for the best uniform 2-bit scalar quantiser, with the rate-distortion bound at 11.29 dB (`zt_lattice.h`). The normalised second moments quoted there, $G(E_8) \approx 0.0717$ against $1/12$ for $\mathbb{Z}^n$, are textbook values (Conway & Sloane 1999, ch. 2). On Qwen2.5-1.5B-Instruct (Q4_K_M), with only `ffn_down` quantised, $E_8$ gave perplexity 13.97 against 20.52 for a 2-bit scalar control. With every matrix quantised, both break the model (7,698 against 52,602). Source: `bench/results/2026-10-10-qwen2.5-1.5b/REPORT.md` §2.

## 6 Golden-ratio and Fibonacci structures, with measured outcomes

**Coil (T7–T8, `zt_coil.c`).** Shell $s$ has $F_{b+s}$ slots and is traversed with stride $F_{b+s-1}$, or with stride 2 when the size is odd. Each traversal is a bijection of $\mathbb{Z}/F_{b+s}$ because $\gcd(F_n, F_{n-1}) = 1$ (or because 2 is invertible mod an odd number). Consecutive indices land near multiples of $1/\varphi$ of the shell, which is the setting of the three-gap theorem (Sós 1958). `test_zt.c` checks the bijection and round-trip for every base and winding. The "hyperbolic" and "field" language of T8 and T10–T12 describes a rooted tree with branching close to $\varphi$ and its tree metric. No geometric theorem is claimed or proved.

**Holographic coding (T15).** Each slot stores its value minus its parent's value, modulo $2^{64}$. Decoding is exact because $x \mapsto x - p$ is a bijection of $\mathbb{Z}/2^{64}$. On $E_8$ points the residual is again a lattice point, since $E_8$ is a group.

**Fibonacci hashing (T6).** The bucket is $\lfloor (k\cdot\lfloor 2^{64}/\varphi\rfloor \bmod 2^{64}) / 2^{64-\text{bits}}\rfloor$ (Knuth 1998, §6.4).

**Measured effect in a real model (REPORT.md §2).** Most golden-ratio parts did not help:

- Phi-power block scales (T3) raised perplexity from 9.371 to 9.404 (exact ladder) and 9.396 (shipped Q16 ladder). The report recommends keeping T3 off for weights.
- Lossless coil plus holographic coding left the model bit-identical, but raised residual entropy to 8.12 bits per weight, against 7.31 for the codes.
- Decoding only 9 of the 10 shells gave perplexity 332,055.
- Only $E_8$ (§5) improved on the conventional method it replaces.
- T4 tiling, T6 hashing and T11 fractal placement were not measured for speed or memory.

## 7 Fibonacci apportionment of the swarm budget

**Definition 7.1 (`kernel/src/swarm/swarm_budget.h`).** A swarm has $L \le 8$ levels. Level $d$ may hold at most $F_{d+2}$ models (R1) and carries weight $W(d) = F_{L-d+1}$ (R2). For $L=8$ the weights sum to $F_{11}-2 = 87$. Each cycle, the total $T \le 2^{48}$ is split among the active levels in proportion to $W$ (R3), and then equally within each level (R4).

Both splits use `swarm_split_lr`. This is the largest-remainder (Hamilton) method: give each part $\lfloor T w_i / \sum w\rfloor$, computed exactly by `swarm_muldiv` with a 128-bit product built from 32-bit limbs, then hand out the $< n$ leftover units in decreasing order of remainder, breaking ties by lower index. The parts therefore sum to exactly $T$ (R5). Lean (`HeadroomConvergence.lean`) proves for a model of `swarm_budget_consume` that `used ≤ allotted` always holds (`consume_used_le`). It also proves that a caller whose requests are all at least $\varepsilon \ge 1$ exhausts the headroom within $\lceil h_0/\varepsilon\rceil$ calls (`drainCalls_le`). It proves non-negativity, non-growth and finite descent of the Q16 recurrence $Q' = \max(0, Q - \lfloor Q\delta/2^{16}\rfloor + \lfloor \eta S/2^{16}\rfloor - C)$ in `zt_isf_step`. Finally it shows that $Q\delta$ fits in 64 bits only if $Q < 2^{47}$, a bound the C code does not enforce (`isf_mul_fits`, recorded as a finding). The ratio $W(d)/W(d+1) \to \varphi$ is a design choice, not an optimality result.

## 8 The four-valued structure of `lpres`

**Definition 8.1.** $V = \{\mathsf N, \mathsf T, \mathsf F, \mathsf B\}$ (`lpres_state_t`, `kernel/src/lpres/lpres.h`). Belnap's FOUR carries two orders. The *truth order* $\le_t$ has $\mathsf F$ at the bottom, $\mathsf T$ at the top, and $\mathsf N$, $\mathsf B$ incomparable. The *knowledge order* $\le_k$ has $\mathsf N$ at the bottom, $\mathsf B$ at the top, and $\mathsf T$, $\mathsf F$ incomparable. $(V, \le_t, \le_k)$ is the smallest interlaced bilattice (Ginsberg 1988; Fitting 1991). Belnap–Dunn conjunction and disjunction are the meet and join for $\le_t$, and negation swaps $\mathsf T$ and $\mathsf F$ while fixing $\mathsf N$ and $\mathsf B$ (Dunn 1976; Belnap 1977).

**Proposition 8.2 (what the code computes).** Let $\wedge$ and $\vee$ denote `lpres_conjoin` and `lpres_disjoin`. Then $(V, \wedge, \vee)$ is the lattice of the four-element chain
$$\mathsf F < \mathsf N < \mathsf B < \mathsf T,$$
so $\wedge = \min$ and $\vee = \max$. *Proof:* check all 16 cases against `lpres.c`. $\mathsf F$ absorbs $\wedge$, $\mathsf T$ is the identity for $\wedge$, $\mathsf N \wedge \mathsf B = \mathsf N$, and dually for $\vee$. $\square$

**Corollaries.**

1. On each of the sub-chains $\{\mathsf F,\mathsf N,\mathsf T\}$ and $\{\mathsf F,\mathsf B,\mathsf T\}$ the tables coincide with Belnap's, and hence with strong Kleene and with Priest's LP respectively.
2. They differ from FOUR only on the pair $\{\mathsf N,\mathsf B\}$: Belnap gives $\mathsf B \wedge \mathsf N = \mathsf F$ and $\mathsf B \vee \mathsf N = \mathsf T$, whereas the code gives $\mathsf N$ and $\mathsf B$. On that pair the code agrees with the knowledge-order meet and join (consensus and gullibility).
3. The structure is a distributive lattice, but $\neg$ is not antitone on it: $\mathsf N < \mathsf B$, yet $\neg\mathsf N = \mathsf N < \mathsf B = \neg\mathsf B$. It is therefore not a De Morgan algebra, and De Morgan's law fails exactly at $(\mathsf B,\mathsf N)$: $\neg(\mathsf B \wedge \mathsf N) = \mathsf N \ne \mathsf B = \neg\mathsf B \vee \neg\mathsf N$.

`tests/host/test_lpres.c` asserts $\mathsf B \wedge \mathsf N = \mathsf N$, so the deviation is fixed by a test rather than accidental. It is deliberate: no combination of $\mathsf B$ and $\mathsf N$ can yield $\mathsf T$, the only authorising value (owner decision 2026-10-12; [MULTI_VALUED_LOGIC.md](../supplemental/MULTI_VALUED_LOGIC.md)). `lpx_and` and `lpx_or` in `lpres_hex.h` reuse these tables for the base of the 64-state tag $V \times P \times \Phi$, where $P$ is four polarities and $\Phi$ is a four-phase Gray cycle. The tag therefore inherits the deviation. The repository also contains other value sets: `trit_t` in `m5_types.h` has six codes, and the T14 coil states in `zt.h` have six. These are not one algebra. Non-explosion is enforced operationally, by an action table, not by a consequence relation; see [PARACONSISTENT_LOGIC.md](PARACONSISTENT_LOGIC.md).

## 9 Reed–Solomon erasure coding over $\mathrm{GF}(2^8)$

**Definition 9.1 (`kernel/src/freight/freight.h`).** The field is $\mathbb{F}_{256} = \mathbb{F}_2[x]/(x^8+x^4+x^3+x^2+1)$ (polynomial `0x11d`, generator 2). For $1 \le k \le 168$, the $168\times k$ generator matrix $G$ has entry $G_{rj} = [r=j]$ for $r<k$ and $G_{rj} = (r \oplus j)^{-1}$ for $k \le r < 168$, where $\oplus$ is addition in $\mathbb{F}_{256}$ on byte labels (`freight_gen_coef`). Each of the 20 byte columns of a freight is encoded independently.

**Proposition 9.2 (MDS).** Every $k\times k$ submatrix of $G$ is invertible, so any $k$ distinct rows determine the data. *Proof sketch:* the parity block is a Cauchy matrix $C_{rj} = 1/(x_r + y_j)$ with $x_r = r \in \{k,\dots,167\}$ and $y_j = j \in \{0,\dots,k-1\}$, distinct and disjoint inside $\mathbb{F}_{256}$. Every square submatrix of a Cauchy matrix is nonsingular. For a selection of $k$ rows containing $i$ identity rows, Laplace expansion along those rows leaves a $(k-i)$-square Cauchy minor (MacWilliams & Sloane 1977, ch. 11). $\square$

Decoding is Gauss–Jordan elimination over $\mathbb{F}_{256}$. The code corrects erasures only. Corruption is *detected*, not located, by a SHA-256 of the payload in the header. `test_freight.c` decodes every $k$-subset of the 168 rows for $k \le 3$, every erasure pattern for $k \ge 166$, and 300 random patterns for each of $k \in \{1,21,84,167,168\}$.

## 10 Hash-chain constructions

**Definition 10.1.** Given a hash function $H$ and an injective encoding $\mathrm{enc}$ of records, set $h_0 = c$ and $h_i = H(\mathrm{enc}(r_i, h_{i-1}))$. Instances in the code:

- `lpx_qlog` (`lpres_hex.c`): SHA-256 over a fixed-length, domain-separated encoding (`"zxv/lpx-qlog/v1"`, seq, tag, reason, submitter, a 32-byte subject, prev), with $h_0 = 0^{256}$.
- `chronicle` (`chronicle.h`): $h_k = \mathrm{SHA256}(h_{k-1}\,\|\,\mathrm{seq}\,\|\,\mathrm{verdict}\,\|\,\mathrm{reason}\,\|\,\mathrm{digest})$.
- The pay-ledger provenance chain (`pay_ledger.h`): SHA3-256.
- The paradox-engine operator log (`paradox.h`).

**Property.** If an adversary changes a record $r_j$ and the verifier still recomputes the same final head $h_n$ from a trusted $h_0$, then some step yields a collision or second preimage for $H$. Tamper evidence therefore reduces to the collision resistance of SHA-256 or SHA3-256, provided the encoding is injective. This holds by construction for fixed-length fields; for the other chains we have not verified it field by field. The construction is the linked time-stamping scheme of Haber & Stornetta (1991). It gives *evidence*, not *prevention*: the code signs no heads (`lpres_hex.h`: "Signing the log head is the caller's job"). `pay_ledger_verify_chain` checks only the journal window and takes the `prev` of the window's first entry on trust.

## 11 Status of claims

| # | Claim | Status | Evidence |
|---|---|---|---|
| 1 | `rational_normalize` keeps value and gives positive denominator; checked rmag division is sound | Tested (Lean model, CI job `proofs`) | `proofs/rational_bounds/RationalBounds.lean`; `kernel/src/rmag/test_rmag_div.c` |
| 2 | `rmag_add_quotas` overflows int64 on some inputs (unfixed finding) | Tested (Lean witness) | `RationalBounds.lean` `addQ_overflow_witness` |
| 3 | `rat_t` results are exact or flagged invalid on 64-bit targets | Tested (host) | `kernel/src/rational/test_rational.c`, `test_rational_regress.c` |
| 4 | `rat_t` gives the same results and refusals without `__int128` (32-bit targets); fixed 2026-10-12, previously unchecked | Tested (host, `-DRAT_NO_WIDE`; not on 32-bit hardware) | `kernel/src/rational/test_rational_narrow.c`; `tests/harness/tiers.py` |
| 5 | Q8_0/Q4_K/Q6_K kernels identical across C, AVX2, AVX-512, NEON; within 0.499 Q16 unit of exact dequantisation | Measured; Tested | REPORT.md §1.5; `kernel/src/tensor/test_zt_simd.c` |
| 6 | Native K-quant logits: mean KL 0.000008 vs float64 | Measured | `bench/results/2026-10-10-qwen2.5-1.5b/REPORT.md` §1.5 |
| 7 | RoPE sin/cos within 2 Q30 units; rotation within 1 Q16 unit up to position 4,000,000 | Tested (host, long double reference) | `kernel/src/tensor/test_zt_rope.c` |
| 8 | Icosian lattice is even unimodular (= $E_8$); 240 roots; $2I$ closed | Tested (self-check) | `kernel/src/e8/test_e8.c`, `e8_selfcheck` |
| 9 | Icosian basis maps isometrically onto doubled $E_8$ coordinates | Tested | `kernel/src/tensor/test_zt_e8.c` |
| 10 | $E_8$ codebook shells 1/240/2160/6720/17520; nearest point agrees with brute force | Tested | `kernel/src/tensor/test_zt_lattice.c` |
| 11 | $E_8$ 9.89 dB vs 9.59 dB scalar on Gaussian data | Measured (in test) | `zt_lattice.h`, `test_zt_lattice.c` |
| 12 | $E_8$ beats 2-bit scalar on `ffn_down` (13.97 vs 20.52 ppl) | Measured | REPORT.md §2 |
| 13 | Codebook pull-in finds the nearest codebook point | Design intent (heuristic, not proved) | `zt_lattice.h` "HONEST LIMITS" |
| 14 | Leech decoder exact; 196560 minimal vectors; Golay weights | Tested | `test_zt_lattice.c` |
| 15 | Phi scales help quantisation | Measured: **no** (small harm) | REPORT.md §2 |
| 16 | Holographic coding compresses weights | Measured: **no** (8.12 vs 7.31 bits) | REPORT.md §2; `zt.h` T15 |
| 17 | Coil strides are bijections | Tested | `kernel/src/tensor/test_zt.c` |
| 18 | Fibonacci tiling/hashing/fractal placement speed benefit | Implemented, not evaluated | REPORT.md §2 "Not tested here" |
| 19 | Swarm split sums exactly to $T$; `used ≤ allotted` | Tested; Lean model | `test_swarm_budget.c`; `HeadroomConvergence.lean` |
| 20 | `zt_isf_step` product fits int64 | Implemented, not evaluated (bound not enforced) | `HeadroomConvergence.lean` `isf_mul_fits` |
| 21 | `lpres` ∧/∨ form the chain F<N<B<T; De Morgan fails at (B,N) | Tested (tables asserted) | `kernel/src/lpres/lpres.c`; `tests/host/test_lpres.c` |
| 22 | Freight code is MDS (any $k$ of 168 rows) | Tested (exhaustive for small/large $k$) | `kernel/src/freight/test_freight.c` |
| 23 | Hash chains detect tampering | Tested (verify functions); reduction to SHA security by argument | `lpres_hex.c` `lpx_qlog_verify`; `pay_ledger.c` `pay_ledger_verify_chain` |
| 24 | Ledger conservation invariants | Tested (TLC, small bounds) | `proofs/ledger_conservation.tla`; `proofs/README.md` |
| 25 | `e8.h`'s offline verification record exists | Not found | `PROVENANCE/GEOMETRY_VERIFIED.md` absent |

## 12 Limitations and threats to validity

- **Model–code gap.** All Lean and TLA+ results concern hand-written models. A change to the C code silently invalidates them, and nothing links models to code or binary in the way seL4 does (Klein et al. 2009).
- **Bounded model checking.** TLC results hold only for the small configurations listed in `proofs/README.md`.
- **Test-based claims** are only as strong as their oracles. Several use `long double` or float64 numpy references, which are themselves approximate. Random tests sample; they do not exhaust.
- **One model.** The neural-network measurements cover a single checkpoint (Qwen2.5-1.5B Q4_K_M), one text of 512 tokens and two machines. The arm64 runs were emulated.
- **Saturation.** Q16 activations saturate at $\pm 32768$ (`zt_model.h`). A model with larger residual streams would clip, and this was not checked.
- **Several rational types** coexist with different overflow semantics (§2). A reader must check which one a module uses.
- **Unenforced bounds** found by the proofs (`isf_mul_fits`, `addQ_overflow_witness`) are not fixed in C.

## 13 Relation to prior work

- Exact rational arithmetic follows the standard gcd-normalised representation (Knuth 1997). The motivation from floating-point non-associativity is classical (Goldberg 1991).
- Integer-only inference relates to Jacob et al. (2018). ZXV's distinguishing choice is 16-bit activations with a single rounding per row, aimed at cross-platform bit identity rather than speed.
- Lattice codebooks for LLM weights, $E_8$ in particular, were introduced before this work by QuIP# (Tseng et al. 2024). The decoders are Conway & Sloane's (1982).
- $E_8$'s optimal packing is Viazovska (2017). Its icosian construction is in Conway & Sloane (1999, ch. 8).
- Rotary encodings are from Su et al. (2024).
- Largest-remainder apportionment and its paradoxes (for example the Alabama paradox, to which any Hamilton split such as R3 is in principle subject when $T$ changes; not tested here) are analysed in Balinski & Young (1982).
- The four-valued logic is Belnap–Dunn's, with the deviation in §8. For bilattices see Ginsberg (1988) and Fitting (1991); for LP see Priest (1979).
- Erasure coding is Reed–Solomon (1960) with a Cauchy generator.
- Hash chaining is Haber & Stornetta (1991).
- Proof tooling: Lean 4 (de Moura & Ullrich 2021) and TLA+ (Lamport 2002).

## References

- Balinski, M. L., & Young, H. P. (1982). *Fair Representation: Meeting the Ideal of One Man, One Vote*. Yale University Press.
- Belnap, N. D. (1977). A useful four-valued logic. In J. M. Dunn & G. Epstein (Eds.), *Modern Uses of Multiple-Valued Logic*, pp. 5–37. Reidel.
- Conway, J. H., & Sloane, N. J. A. (1982). Fast quantizing and decoding algorithms for lattice quantizers and codes. *IEEE Transactions on Information Theory*, 28(2), 227–232.
- Conway, J. H., & Sloane, N. J. A. (1999). *Sphere Packings, Lattices and Groups* (3rd ed.). Springer.
- de Moura, L., & Ullrich, S. (2021). The Lean 4 theorem prover and programming language. *CADE-28*, LNCS 12699, Springer.
- Dunn, J. M. (1976). Intuitive semantics for first-degree entailments and "coupled trees". *Philosophical Studies*, 29, 149–168.
- Fitting, M. (1991). Bilattices and the semantics of logic programming. *Journal of Logic Programming*, 11(2), 91–116.
- Ginsberg, M. L. (1988). Multivalued logics: a uniform approach to reasoning in artificial intelligence. *Computational Intelligence*, 4(3), 265–316.
- Goldberg, D. (1991). What every computer scientist should know about floating-point arithmetic. *ACM Computing Surveys*, 23(1), 5–48.
- Haber, S., & Stornetta, W. S. (1991). How to time-stamp a digital document. *Journal of Cryptology*, 3(2), 99–111.
- Jacob, B., et al. (2018). Quantization and training of neural networks for efficient integer-arithmetic-only inference. *CVPR 2018*.
- Klein, G., et al. (2009). seL4: formal verification of an OS kernel. *SOSP 2009*.
- Knuth, D. E. (1997). *The Art of Computer Programming, Vol. 2: Seminumerical Algorithms* (3rd ed.). Addison-Wesley.
- Knuth, D. E. (1998). *The Art of Computer Programming, Vol. 3: Sorting and Searching* (2nd ed.). Addison-Wesley.
- Lamport, L. (2002). *Specifying Systems: The TLA+ Language and Tools for Hardware and Software Engineers*. Addison-Wesley.
- MacWilliams, F. J., & Sloane, N. J. A. (1977). *The Theory of Error-Correcting Codes*. North-Holland.
- Priest, G. (1979). The logic of paradox. *Journal of Philosophical Logic*, 8, 219–241.
- Reed, I. S., & Solomon, G. (1960). Polynomial codes over certain finite fields. *Journal of the SIAM*, 8(2), 300–304.
- Sós, V. T. (1958). On the distribution mod 1 of the sequence $n\alpha$. *Ann. Univ. Sci. Budapest. Eötvös Sect. Math.*, 1, 127–134.
- Su, J., Ahmed, M., Lu, Y., Pan, S., Bo, W., & Liu, Y. (2024). RoFormer: Enhanced transformer with rotary position embedding. *Neurocomputing*, 568, 127063.
- Tseng, A., Chee, J., Sun, Q., Kuleshov, V., & De Sa, C. (2024). QuIP#: Even better LLM quantization with Hadamard incoherence and lattice codebooks. *ICML 2024*.
- Viazovska, M. S. (2017). The sphere packing problem in dimension 8. *Annals of Mathematics*, 185(3), 991–1015.
