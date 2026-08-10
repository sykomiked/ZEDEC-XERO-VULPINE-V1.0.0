# Event-space bring-up — the correction to layered bring-up

_Owner correction: the thirteen phases do not depend on a synchronised clock.
The layer waves in LAYERED_BRINGUP.md and FABRICATION_ORDER.md were linear
thinking. This supersedes their scheduling model; the layer assignments and the
tri-space cross stand._

---

## 0. What was wrong

I designed bring-up as **waves separated by barriers** — L0 completes, barrier,
L1 completes, barrier. That is a global clock wearing different clothes. It
forces a total order on a structure that only has a **partial** one, and it
contradicts three things this system already established:

- **the multikernel principle** — per-core kernels, no shared global state, so
  there is no place for a global bring-up counter to live;
- **QAT** — "each object sits at the centre of its own temporal reference frame";
- **`oseq`**, which is already in the binary (9 symbols) and exists precisely to
  express ordering *without* a clock.

A barrier says "everyone waits for the slowest." Causality says "each proceeds
when its own preconditions hold." Those are different systems, and only the
second one is what we built.

## 1. The correction: readiness is an event, not a tick

A module does not declare a **level**. It declares its **preconditions**, and it
fires when they are satisfied — on whichever core, at whatever time.

```c
ZXV_PROVIDES(zxvfs_ready)
ZXV_REQUIRES(mm_ready, oseq_ready)
ZXV_BRINGUP(zxvfs_bringup)
```

No number. No wave. The dependency graph *is* the schedule, and any valid
traversal of it is a valid boot. Two cores may bring the same system up in
different orders and both be correct, provided happens-before is respected —
which is exactly what `oseq` decides.

**The linear path still exists — it is just derived, not designed.** A topological
sort of the DAG gives one; each core computes its own; they need not match. I had
been hand-authoring one particular sort and mistaking it for the structure.

## 2. Tri-space is the readiness state, and it was already sitting there

The three faces are not decoration on this. They are the states:

| face | meaning during bring-up |
|---|---|
| **S+** `.zxvc` | the capability is **available** — preconditions met, it fired |
| **S−** `.cedez` | the capability can be **withdrawn** — revocation, teardown, rollback |
| **S0** `.cedec` | preconditions **not yet satisfied** — HELD, not failed, not skipped |

S0 is the answer to a question my wave model could not express. A subsystem whose
requirements are unmet is not an error and not a success. It is *unresolved*, and
it should stay unresolved until it isn't — which is precisely what S0 means and
why the file type exists.

This also repairs the empty S− column found in THIRTEEN_LAYERS.md §3. Under a
wave model, teardown had nowhere to live: barriers only run forwards. Under an
event model, withdrawal is symmetric with provision — retract the fact, and
everything that required it returns to S0 by the same mechanism that brought it
up. **Revocation stops being a feature to add and becomes the reverse edge of one
already there.**

## 3. Specialise, then synthesise

The kernel carries several number systems — `zphi` (exact ℤ[φ]), `rat` (exact
rationals, overflow-detecting), `surplus_real_t` (Q32.32), `poly_t` (𝔽_q ring),
`cyc13_t` (exact 13-dimensional cyclotomic) — and multiple logic arities: 5
distinct trit states, 13 phase states.

**Nothing should use all of them at once, and nothing was ever meant to.** A
module declares the representation it *thinks in*, works natively there, and
converts only at a boundary. That is specialisation; synthesis happens at the
edges.

And the mechanism is already built: `modbind`'s `MB_FORM_*` are exactly these
declarations — `MB_FORM_POLY` (𝔽_q), `MB_FORM_TRISPACE`, `MB_FORM_TRIT`,
`MB_FORM_BINARY`, `MB_FORM_PHASE` — with `module_transform_t` as the conversion
that lives *with the module*, not in shared glue. I built that yesterday to catch
orphans and did not notice it was also the specialisation registry.

So the rule is: **native inside, canonical at the boundary.** `e8` thinks in
ℤ[φ]. `mixmat` thinks in exact rationals. `mlkem` thinks in 𝔽_q. None of them
converts unless it is talking to something else, and when it does, the conversion
is declared and checkable.

## 4. Why this needs the higher-dimensional scaffold

A total order is one-dimensional — it is a line, and forcing thirteen
interdependent layers onto a line is what produced the barriers.

A **partial order is a lattice**, and a lattice has dimension. That is where E8
earns its place, and it is a narrower claim than I made in FABRICATION_ORDER.md:
the root poset is **graded by height**, so it supplies a partial order with a
known width — how many elements are mutually incomparable, and therefore how much
genuinely proceeds at once. The Cartan matrix says which pairs commute. Neither
requires a clock; both are structure.

A linear boot is then a **projection** of that lattice onto a line. Many
projections exist. Any of them boots. Choosing one and calling it the design was
the error.

## 5. What changes concretely

- `ZXV_INITCALL(Ln, fn)` becomes `ZXV_REQUIRES`/`ZXV_PROVIDES`/`ZXV_BRINGUP`.
  Layer numbers survive as **documentation and as the DRC rule** — a module may
  still not include from a higher layer — but they stop being the runtime
  sequence.
- `KEEP()` on the bring-up sections is unchanged; that was always the retention
  lever, independent of ordering.
- `verify_layers.sh` gains a third check: **the requires-graph must be acyclic**,
  and every declared requirement must be provided by something. A cycle or an
  unprovided requirement is a build failure — the honest form of "it hung at
  boot and nobody knew why".
- The bring-up log becomes `oseq` events rather than printed lines, so causal
  order is recorded rather than inferred from the order text happened to appear.

## 6. What stands from the earlier documents

The thirteen layers, the tri-space cross into 39 cells, the DRC layer rule, the
silicon sign-off discipline (LVS/ERC/DFT/tape-out gate), and E8 as the source of
a parallelism structure — all stand.

What is retracted is the **barrier-wave schedule** in LAYERED_BRINGUP.md §6 and
FABRICATION_ORDER.md §4. Those described a clocked assembly line. This system is
an event space, and its own `oseq` was already the right instrument.
