# ZXV — Logic States, Number Types, Geometry: the Rules of Interaction

_2026-08-09. Five parallel surveys of the tree. Everything here is what the code
literally does, with file:line. Where a header claims more than the code
delivers, that is stated._

---

## 0. The one-paragraph answer

The system has **two multi-valued logics** (5 distinct trit states, 13 Sephirot
phases), **both prime and both Fibonacci** — so both admit finite fields, which
is what makes a nested matrix algebra possible at all. It has a **13-dimensional
exact group ring** already built (`cyc13_t`). It has **real geometry**
(Rodrigues rotation, phyllotaxis, R⁸ interaction). And it has **four places
where a logical attribute is already mapped to a geometric position**.

What it does **not** have: a single associative operator over the logic states,
a matrix multiply of any kind, or any geometric embedding of the truth values
themselves. Those three absences are the whole build.

---

## 1. The logic states

| type | file:line | states | status |
|---|---|---|---|
| `trit_t` | `include/m5_types.h:13` | **6 enumerators, 5 distinct** | used, 124 refs / 59 files |
| `l13_phase_t` | `src/sephirot/sephirot.h:57` | **13** (10 Sephirot + 3 Veils) | used, 68 refs / 15 files |
| `lpres_state_t` | `src/lpres/lpres.h:38` | 4 (Belnap) | **the only complete algebra** |
| `rur_logic_state_t` | `src/rur/rur.h:80` | 4 | declared, no operators |

**`TRIT_GLUT = 2` is a legacy alias for `TRIT_GLUT_NEUTRAL = 5`.** Every accessor
treats them as equal, so the type is extensionally **5-valued**. The repo
contradicts itself about this: the tests say "5VL", `dharma.h:18` says
"six-valued", `emu/congruence.c:82` treats it as a 5-level lattice. **Two names
for one element breaks any algebra built on it** — `a ⊕ b` can return `2` on one
path and `5` on another, and equality then reports them different.

**The order is already proven.** `emu/congruence.c:85` asserts
`FALSE < GLUT_MINUS < GLUT_NEUTRAL < GLUT_PLUS < TRUE` is strictly monotone
under `trit_to_ell` (1.0 / 0.75 / 0.5 / 0.25 / 0.0). A chain is a bounded
lattice, so **meet/join are associative, commutative, idempotent, distributive,
and have both identities — for free.** Neither `trit_meet` nor `trit_join`
exists anywhere.

**Structure**: 2 classical (TRUE/FALSE) + 3 charged gluts (PLUS/MINUS/NEUTRAL).
`trit_charge()` already returns +1/−1/0 — **the glut triad IS tri-space**, at the
bit level.

### The only binary operator over a multi-valued logic, and it is not associative

`dvaitadvaita_resolve(g, a, b)` — `src/dharana/dharana.c:95`. `g` is
`(void)`-discarded, so it is a pure binary op. Commutative and idempotent, but:

```
resolve(resolve(TRUE,TRUE), FALSE) = NEUTRAL      resolve(G+, resolve(G−,G−)) = NEUTRAL
resolve(TRUE, resolve(TRUE,FALSE)) = G_PLUS       resolve(resolve(G+,G−), G−) = G_MINUS
```

**Not associative, and no identity element exists** (`resolve(TRUE, FALSE)`
= NEUTRAL, not TRUE). It is a commutative idempotent *magma*, not a monoid. It
is instantiated **112 times** as gate arrays in `dharana_array_t`. Nested
matrices over it would bracket-depend: right at depth 1, wrong at depth 3.

---

## 2. The number types — closure screening

Only three types can carry a semiring today:

| type | closed | mechanism |
|---|---|---|
| **`poly_t`** `src/mlkem/mlkem_ntt.h:33` | **YES** | every write via `mlkem_mod_reduce`, intermediates widened. *The only genuine verified closed ring in the kernel.* Use as the **reference** to validate any law-checker. |
| **`hdcm_vector_t`** `src/hdcm/hdcm.h:106` | **YES** | fixed-width XOR group, explicit identity |
| **`rat_t`** `src/rational/rational.h:39` | **YES** over `rat ∪ {invalid}` | 128-bit intermediate, overflow **detected → invalid**, invalid absorbing. Missing `rat_one()`. |

**The trap: there are two unrelated rational types.**

| | overflow | division by zero |
|---|---|---|
| `rat_t` | detected → `invalid` | → `invalid` |
| `rational_t` `include/m5_types.h:12` | raw `int64_t`, **UB, silent** | `rational_normalize` **rewrites `n/0` as `n/1`** |

**`cyc13_t` uses `rational_t`.** `cyc13_mul` is a 13×13 convolution — 169 rational
products summed, denominators multiplying every step. It will overflow silently
and turn `x/0` into `x/1`. **This must be migrated to `rat_t` before any matrix
work, or the engine inherits confidently-wrong arithmetic.**

**Not usable as a matrix element**: `surplus_real_t` (Q32.32) is **not closed** —
raw `int64` add/sub (UB on overflow), narrowing wrap in multiply, `÷0 → 0`. It
names both identities (`SR_ZERO`/`SR_ONE`), which makes it *look* algebraic;
saturation lives in callers, not the type.

Other real structure worth keeping: `gematria_reduce` is the digital root — a
genuine **semiring homomorphism onto Z/9** for both + and ×. `gmul`
(`aes256_gcm.c:51`) is a real GF(2⁸) multiply, a second independent reference.

---

## 3. The 13-level algebra already exists

`cyc13_t` — `src/sephirot/sephirot.h:101` — is `rational_t c[13]`: the group ring
**Q[Z/13Z]**, with `cyc13_add`, `cyc13_mul` (cyclic convolution mod 13),
`cyc13_galois_apply` (k → e·k mod 13), and `l13_project` (Galois trace fold over
a subgroup). A commutative ring, i.e. a semiring with negation.

Its subgroup table `H_DIM{1,2,3,4,6,12}` (`sephirot.c:105`) is exactly the
subgroup lattice of GF(13)\* (cyclic of order 12; subgroup orders = divisors of
12). `l13_dim_t` in the header is the same set. **The field structure is already
encoded**, and `l13_project` is the closest thing in the tree to a closure
operator.

`uvn_t` (`src/dharma/uvn.h:30`) wraps it so a scalar and a full 13-vector are the
same type — nesting-by-degeneracy. It has `uvn_add` but **no `uvn_mul`**.

---

## 4. Geometry — what is real

**Real and usable:**
- `helion.c` — `h_vec3` over Q32.32 with `h_dot`, `h_cross`, `h_norm`,
  **`h_rot` (full Rodrigues rotation)**, Gram-Schmidt. The genuine 3-D core.
- `chiglet.c:43` `chg_interaction` — scale-invariant `u = 1 − (a·b)²/(|a|²|b|²)`
  over R⁸. **This is the existing interaction rule between two vectors.**
- `shimmer.c:17` — exact quarter-wave sine LUT. `refinery.c:132` — 96-step
  milli-unit sine with exact quadrant symmetry.
- `freestanding.h:82-173` — `fs_sqrt/exp/log/pow/cos/sin/cabs/cexp/atan2` are all
  **real implementations**, not stubs (though `fs_log` lacks range reduction).
- `lattice_dimensions.c:70` — **real phyllotaxis**: `ang = (i+1) × GOLDEN_TURN`
  where `GOLDEN_TURN = 25028 = 360/φ²` as a 16-bit turn, `r ∝ √(i+1)`. A closed
  cyclic group Z/2¹⁶ that is φ-derived — a beautiful shape that is also stable.
- `epu_device.c:386` — phyllotaxis again, `θ = i × golden_angle`, `r = √(θ/ga)`.

**Broken or overclaimed:**
- `hypercube_scene.c:194` `hc_dim_to_x/y/z` — base-256 **digit packing, not a
  linear projection**; a `span` bug overshoots up to 256×; `hc_dim_to_y` returns
  0 when `num_dims ≤ 2` after reading `coords[2..3]`. The header claims a
  "hypercube transform"; there is no rotation.
- `rom_dimensions.c:158` `rom_lift` — the header says "at spin step `i`, by
  Helion's spiral". It performs **no rotation and never calls helion**; `i` is an
  object id. It is a 6-layer pop-up-book depth stagger — precisely the
  "extrusion" the header says it is not.
- `dl_phi_permille()` (`dimensional_ladder.c:54`) returns the **constant 1618**;
  it is not computed.
- `fx_sin`/`fx_cos` — **four byte-identical copies** (cinder, quill, radial_map,
  lattice_dimensions), parabolic approximation with **~5.6% peak amplitude
  error** and no refinement pass.
- `SR_SIN`/`SR_COS` exist **only on the host**; undefined on the freestanding
  target.

---

## 5. Logic → geometry: the four existing bridges

**This is the most important section for the design.** A logical attribute is
already mapped to a geometric position in four places:

1. **`cards/zca.c:79`** — the strongest instance. A card's *discipline* selects a
   **basis axis in R⁸**; gematria and root select tilt axes; `zca_exec` writes
   Q32.32 magnitudes into `evidence[CHG_DIM]`, and `chg_effective_experts` then
   merges near-collinear vectors by angle. The file says it outright: *"The
   'collect broadly, not deeply' incentive is this geometry — not a rule written
   down somewhere."* **A policy enforced purely by vector angle.**
   *Bug*: the mapping is `% 8` on raw enum values, so disciplines 8 apart collide
   onto the same axis.
2. **`epu/epu_device.c:795`** — runtime coupling state → `θ ∈ [0, π/2]` → a real
   **Givens rotation** on the (joy, awe) and (love, gratitude) planes.
3. **`desktop/zxv_shell.c:211-262`** — the 13 OS spaces → fixed Metatron vertices;
   arrow-key navigation **maximises cos²θ** between edge and key direction, in
   exact integer cross-multiplied form.
4. **`hypercube_scene.c:194`** — logical attributes → screen rect, but via the
   broken transform above.

**And the gap that defines the work:** *nowhere in the tree is a truth value —
`trit_t`, tri-space polarity, `lpres_state_t` — mapped to an angle or a vertex.*
The polarities are stored as plain enum bytes with **no geometric embedding at
all**. The machinery to do it exists and is used for other things.

Note also: the 13 spaces have **two disagreeing layouts** — the Metatron figure
in `zxv_shell.c` and the golden-angle phyllotaxis in `lattice_dimensions.c`.

---

## 6. What does not exist anywhere

- **No matrix × matrix multiply, for any element type.** No transpose, no
  transitive closure, no Kleene star, no tensor/Kronecker product.
- **No matrix-of-matrices.** Nesting exists only as tree recursion
  (`count_house` — recursive fold over `children[16]`), address-prefix hierarchy
  (`zorder` — `zo_parent`/`zo_contains`/`zo_common_level`, a meet-semilattice),
  scene-graph parent/child (`hypercube`), and subgroup projection (`l13_project`).
- **No `trit_meet`/`trit_join`**, despite the chain being proven.
- `axiom_matrix_t` (`m5_types.h:29`) **owns the name** and sparse hash storage but
  has **zero algebra** — no add, no multiply, no compose. `axiom_matrix_project_tick`
  ignores its `tick` argument entirely.
- `phase_t` (`m5_types.h:21`) — a complex struct with **no operations defined at
  all**. Same for the polar `complex_exposure_t` / `complex_corr_t` pairs: built,
  never multiplied.

---

## 7. The build order this implies

1. **`rat_one()`** — `rat_t` cannot be a semiring element without a multiplicative
   identity. Trivial.
2. **Canonicalise `TRIT_GLUT`** — one element, one name, or the algebra is unsound
   before it starts.
3. **`trit_meet`/`trit_join`** from the already-proven chain — the honest ⊕/⊗ for
   L5, associative by construction.
4. **Exhaustive law-checker**, validated first against `poly_t` and `gmul` as
   known-good rings. 125 triples for L5, 2,197 for L13. If it cannot confirm a
   known ring, the checker is wrong and nothing it says counts.
5. **Repair `dvaitadvaita_resolve`** — 112 live gates depend on its semantics.
6. **Migrate `cyc13_t` to `rat_t`** — invasive (sephirot, dharana, chakra,
   dharma), and unavoidable before matrices.
7. **Embed the logic states geometrically** — the pentagon for L5 (5-fold
   symmetry *is* φ: diagonal/side = φ, φ = 2cos36°), the 13-gon for L13, reusing
   `GOLDEN_TURN` phyllotaxis and `h_rot`. This is the missing bridge from §5.
8. **The matrix engine over `cyc13_t`/`rat_t`**, with nesting from the semiring
   closure property (matrices over a semiring are a semiring — so nesting is a
   theorem, not plumbing), and star-closure for the loops.

**Standing rule: beauty proposes, the law disposes.** Geometry generates the
candidate tables; the exhaustive check decides. A φ-spaced table that fails
associativity is wrong however good it looks, and the failing law names which
relation to fix.
