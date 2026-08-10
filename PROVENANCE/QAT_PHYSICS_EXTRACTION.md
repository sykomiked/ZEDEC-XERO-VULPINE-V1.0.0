# QAT → ZXV: the extracted physics, and what may be built on it

_Source: "Quantum Atom Theory", Nick Harvey — 834 videos, ~180,000 words of
transcript, plus slide extraction. Owner directive: this is the physics engine
for the system._

_Every claim below is marked **VERIFIED** (I re-derived or measured it),
**TRANSCRIBED** (read off a slide, not checked), or **REFUTED** (checked and it
does not hold). Nothing is carried on the author's authority._

---

## 1. The model, stated as a mechanism

Time is not a coordinate. It is the **spontaneous absorption and emission of
photons by atomic electrons**, and each photon–electron coupling *is* one quantum
of elapsed time — "the moment of now". Atoms persist as standing waves in time.

The generator is **Huygens' Principle (1670)**: every point on a wavefront is the
source of a new spherical 4πr² wave. The wave is continuous; the *interactions*
are quantised. Each surface point is a potential coupling, so each is a potential
next moment.

Four consequences the theory draws, all of which have a computational reading:

| claim | computational reading |
|---|---|
| each object sits at the centre of its own reference frame | **no global clock; only local causal order** |
| Ψ² *is* the forward passage of time in that frame | state advance is the collapse/reform cycle, not a tick |
| the sphere's 2D surface is the boundary condition for charge | the boundary is where the work happens, not residue |
| absorption converts potential energy into kinetic — "what is actually happening" | **potential → actual is the commit** |

The last two are the load-bearing ones for us. See §4.

## 2. Geometry — VERIFIED EXACT

Two constructions from the slides check out exactly. These are the parts worth
building on.

**(a) Square inscribed in a semicircle gives φ.**
Base on the diameter, top corners on the arc. With radius R the side is
s = 2R/√5; taking A as the square's left base corner and G as the far end of the
diameter, AG = R + s/2 and

> **AG / AB = (√5 + 1)/2 = φ, exactly.** [VERIFIED — symbolic derivation, error
> 0.000e+00. Independently confirmed by pixel measurement of his own slide
> (`6nE1Isqf-3Q/f019`): diameter x=240→645, A at centre−s/2, ratio **1.619**.
> The figure is correctly proportioned, not decorative.]

**(b) A circumference cut in golden ratio subtends the golden angle.**
Divide a line in golden ratio, bend it into a circle, draw radii to the join
points:

> arc a = 360/φ = **222.492236°**, arc b = 360/φ² = **137.507764°**, sum exactly
> 360°. [VERIFIED]

**(c) Already present in ZXV.** `GOLDEN_TURN = 25028` in `emu/radial_map.c` and
`emu/lattice_dimensions.c` is 360/φ² as a 16-bit turn — agreeing with 137.5078°
to **0.025°**. His central constant has been a kernel constant all along.

Note **360 / golden angle = φ² exactly** — the same φ² that is the ratio between
E8's two root shells (PROVENANCE/GEOMETRY_VERIFIED.md §3). One constant, two
unrelated routes.

## 3. Equations read off the slides

**TRANSCRIBED and standard** (correct as written):
`ΔE = hf` · `Eₖ = ½mv²` · `4πr²` · `Δx·Δpₓ ≥ h/4π` · `K = 1/4πε₀` ·
`α = e²/4πε₀ℏc` · `e^(iπ) + 1 = 0` · Schrödinger `d²ψ/dx² + (8π²m/h²)(E−V)ψ = 0`

**TRANSCRIBED and correct, with a stated reason** — the strongest single slide:
`E² = (MC²)² + (pC)²` drawn as a **3-4-5 right triangle** inscribed in a light
sphere, legs pC and MC², hypotenuse E, with the right angle *justified* (E ⊥ B)
rather than assumed. That is the relativistic energy–momentum relation, and the
3-4-5 triple makes it checkable.

Also with a stated reason: the **½** in Eₖ=½mv² is derived from "the radius being
half the diameter of the sphere", and the inverse-square law is drawn as a
solid-angle fan with 1, 1/4, 1/9 placed at r, 2r, 3r.

## 4. What this licenses ZXV to build

The four mechanisms below are the extraction. Each already has a home.

1. **Process as the unit of advance.** The system advances by phase ticks and
   events, never wall clock. QAT makes the *interaction* the quantum of time;
   `clock/event_clock.c` is that, and the reality layer should be driven by
   coupling events rather than a loop with physics attached.

2. **Local reference frames — no global clock.** "Each object at the centre of
   its own frame" is the distributed-systems statement exactly. The organ is
   already built and sitting unlinked in the reserve: **`src/oseq/oseq.c`** —
   causal DAG, happens-before queries, replay detection, per-node incarnation.
   The linked `oseq_core.c` sibling exposes four functions; the reserve version
   is the whole ganglion. **This is the next module to differentiate.**

3. **Concave / convex / surface = S− / S+ / S0.** His manifold assigns negative
   charge to inner curvature, positive to outer, and puts every event *on the
   boundary between them*. That is tri-space with the boundary as the active
   site — the same claim `zxvfs_tri` makes by storing the triad as the unit
   rather than a positive face with two attachments.

4. **Absorption/emission = potential → actual = commit.** The collapse-and-reform
   cycle is copy-on-write: allocate the new state invisibly, then atomically
   make it the actual one. `zxvfs` already implements exactly this.

And one structural rule from §2(a): **absorption and emission are the a:b of the
golden section** — a two-part split whose ratio is φ. That is a real pairing rule
for S+/S−, not a metaphor, and it is constructible.

## 5. REFUTED — do not encode these

Stated plainly so nothing downstream inherits them.

- **golden angle ≠ fine structure constant.** 137.507764 vs 137.035999177 —
  **0.344% apart**, different kinds of quantity (an angle in degrees vs a
  dimensionless coupling). Across 59 slides read in detail, **no equation relates
  φ to α numerically**; the chain is: analogy → *one real construction* →
  analogy → analogy. The construction is sound; the identification is asserted.
  The narration goes further than the slides, stating the result "will always be
  137" when the construction gives 137.5078. [REFUTED]

- **ΔE·Δt ≥ h/2π is not the canonical bound.** Both uncertainty relations are
  ℏ/2 = h/4π (Robertson for x,p; Mandelstam–Tamm for E,t). His h/2π = ℏ is a
  factor of 2 high. This matters because the **entire "time is 2-dimensional,
  space is 3-dimensional" argument rests on 2π and 4π differing between the two
  relations** — and canonically they do not. Related: slide agents found **ℏ
  typeset as h on six frames**, including a self-contradictory `h = h/2π`. A
  missing bar is exactly the error that turns h/4π into h/2π. [REFUTED]

- **1/α described as "an irrational number that never ends."** 1/α is a
  *measured* dimensionless quantity, 137.035999177(21). Its irrationality is not
  established; this is a category error, not a rounding convention. [REFUTED]

- **`F = G m₁m₂/(4πr²)` and `F = K q₁q₂/(4πr²)`** appear alongside
  `K = 1/4πε₀`, double-counting the 4π; the same slides elsewhere print the
  standard `F = G m₁m₂/r²`. Unresolved contradiction — do not encode either
  until it is. [REFUTED as written]

## 6. The pattern, and the working rule

Across every claim examined: **the geometry is sound and the physical
identifications are where it breaks.** Both failures so far are an equation
borrowed at the wrong value — α at 137.5, and ℏ where ℏ/2 belongs.

So the rule for the physics engine, which is the same rule that governed the
retracted Fibonacci-dimension claim in GEOMETRY_VERIFIED.md §4:

> **Take the constructions. Leave the constants.** A construction that is exact
> can be implemented and tested. A constant that is 0.344% wrong is a bug that
> propagates silently through everything downstream and is nearly impossible to
> find later.

Nothing in §4 depends on any refuted item. The mechanism survives the corrections
intact — which is the strongest thing that can be said for it.

## 7. Corpus status

834 videos, ~180,050 words after dedup. Cluster coverage: photon/electron 200
videos (23%), sphere/geometry 160 (19%), squares 80 (9%), uncertainty 51,
Huygens 47 (1670 cited 74×), golden ratio/Fibonacci 28 (3%) — but **38 video
titles** name φ/Fibonacci/137/spiral, so that material is *concentrated*, not
diffuse. Slide extraction confirms the corpus is ~70% redundant: 59 frames
resolved to 20 distinct slides.

Four spiral-periodic-table videos remain unexamined and are the most direct
collision with the owner's own 2022 Sonic Chemistry work, which indexes the
periodic table by φ.
