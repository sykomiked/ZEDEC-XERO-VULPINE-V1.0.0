# Building the kernel like silicon — the methodology, not the metaphor

_Owner directive: build it the way a cutting-edge microprocessor is designed.
"Hardware in code, code in hardware, hardware emulating hardware because of
code."_

_Companion to LAYERED_BRINGUP.md, which defines the layers this applies to._

---

## 0. Why this is a real correspondence

Two of the checks already in this build are, exactly and not loosely, the two
canonical silicon sign-off checks. That is the reason to take the whole
methodology rather than borrow the vocabulary.

| silicon check | what it does | what we already have |
|---|---|---|
| **LVS** — layout versus schematic | extracts the netlist from the actual layout and compares it to the schematic netlist. Catches "the drawing says this connection exists; the silicon does not." | **`verify_banners.sh`** — extracts symbols from the actual ELF with `nm` and compares them to what the boot banners claim. Caught TLS, Chiglet and `oseq` being announced while absent. |
| **ERC** — electrical rule check | finds floating nodes and unconnected nets: structures present in the layout that are wired to nothing. | **`measure_reachability.sh`** — 1,673 of 2,301 symbols present in the objects and connected to nothing. |

We arrived at both by fixing defects, not by imitating silicon. That convergence
is the argument for adopting the rest deliberately.

## 1. Stage mapping

| silicon stage | kernel equivalent | status |
|---|---|---|
| architecture / spec | the layer table (L0–L7) | designed |
| RTL | the modules in `kernel/src` | exists |
| synthesis | compilation | exists |
| **floorplan** | layer assignment — what may sit where | designed, unenforced |
| **place & route** | initcall ordering within and across layers | designed |
| **DRC** — design rule check | **`verify_layers.sh`**: no module includes a header from a higher layer | **to build** |
| **LVS** | `verify_banners.sh` | **built, gating the link rule** |
| **ERC** | `measure_reachability.sh` | built, not yet gating |
| **STA** — static timing analysis | per-layer boot-time budget | not started |
| **DFT** — design for test | every initcall returns a status | partly — the pattern exists |
| **tape-out gate** | release: every check clean, no exceptions | not started |

## 2. The disciplines worth importing, in order of value

**DFT — testability is designed in, never bolted on.** A chip gets scan chains at
RTL because you cannot add observability to fabricated silicon. Our equivalent:
*every initcall returns a status, and the status is checked.* This is already the
house pattern — the ML-KEM KAT, the X25519 RFC-7748 vector, the HKDF→AEAD
seal/open with a **forged-tag rejection**. That last one is the model: a
round-trip test alone passes on an implementation that ignores the tag entirely.
Scan insertion is exactly that instinct, applied everywhere by default.

**DRC — rules are checked mechanically, not reviewed by eye.** No one eyeballs
metal spacing on a modern die. The layer invariant — *a module may not include a
header from a higher layer* — is statically checkable from the include graph, so
it must be a build failure and never a convention. Conventions in this tree have
a measured survival time of hours: `ROADMAP_TO_COMPLETE.md` contradicted
`SERVER_HANDOVER.md` within a day of being written.

**The tape-out gate — you cannot patch after fabrication.** This is the discipline
that changes behaviour most. Because a respin costs months and millions, every
check runs to completion *before* commit, and "we will fix it in software" is not
available. Our analogue is a release gate: `verify_banners.sh` **and**
`verify_layers.sh` **and** `measure_reachability.sh` all clean, no
`VERIFY_BANNERS=0`, or there is no release. The week's evidence says this is the
needed shape — a fifth of the tree shipped as claims that were never in the
binary.

**Standard cell library — characterised primitives, reused everywhere.** L0 is
exactly this: `zphi` (exact ℤ[φ]), `rat` (overflow-detecting rationals), `e8`,
`mixmat`. Each has known behaviour, a selfcheck, and no dependencies. Nothing
above L0 should re-implement arithmetic, in the same way no block re-invents an
inverter.

**Hard macros / third-party IP — integrated, never absorbed.** A licensed PHY
keeps its own provenance and is dropped in as a block. Same rule for foreign
code — emulator cores, id Tech and Godot lineages: keep their licence and
identity, run them behind our interfaces, do not rewrite them into house style to
pretend they were always ours. This is already the standing position; silicon
practice is the precedent for it.

**Clock and power domains — the multikernel, in hardware terms.** Per-core kernel
instances with explicit message passing are separate clock domains with a defined
crossing discipline, not one synchronous block. Every domain crossing needs a
synchroniser; every cross-core call needs an explicit message and a causal order.
`oseq` at L1 is that synchroniser, which is why it sits below everything but
arithmetic.

**Reset sequencing — power-up order is engineered.** A chip brings rails and
domains up in a fixed order with interlocks. Our initcall levels are that
sequence, and the failure rule matches: a rail that does not come up **holds off**
everything downstream. Today a banner prints `[INITIALIZED]` regardless — that is
a board with no interlocks.

## 3. What silicon practice would say about the current state

Read as a tape-out review, the honest summary:

- **LVS was failing and is now clean** for the 13 checked subsystems.
- **ERC is failing badly**: 73% of nets floating. In silicon this blocks tape-out
  outright — not because floating nodes always break, but because they mean the
  netlist and the intent disagree, and nobody knows which is right.
- **DRC does not exist yet.** The layer rule is unenforced, so the floorplan is a
  drawing.
- **STA has never run.** No boot-time budget exists per layer.
- **Redundant cells are present**: `oseq`/`oseq_core`, `iphase`/`iphase_core`,
  `choice`/`choice_core`, `phase_coord`/`phase_coordinator` — four cases of two
  implementations of one function, the thin one connected and the full one
  floating. A physical designer deletes one, and that both shrinks the die and
  raises the connected percentage.

## 4. "Hardware emulating hardware because of code"

The emulator cores make this literal rather than figurative, and the console
audit found the design flaw a hardware engineer would flag immediately: **every
core silently no-ops unknown opcodes as documented policy** — "never a crash,
never a skip" — and the 6502's `jammed` flag, documented as the safety exit, is
never set anywhere in the tree.

In silicon terms that is a decoder with no illegal-instruction trap and a
tied-off error output. It cannot fail, so it cannot be measured, and a 140,000-ROM
campaign against it would return uniform passes for ROMs that never really
executed. Before any campaign is evidence, the decoders need a real trap behind a
campaign flag — the equivalent of enabling assertions for a validation run.

## 5. Order of work

1. **DRC first** — `verify_layers.sh`, wired into the link rule beside LVS.
   Cheapest, and it makes the floorplan real.
2. **Prove the initcall mechanism on five modules**, one per layer, and measure
   the ERC delta. If `KEEP()` plus one ordered table does not move it, the
   floorplan is wrong and we stop.
3. **Delete the redundant cells** — pure subtraction, improves both metrics.
4. **Convert layer by layer, lowest first**, measuring after each.
5. **STA** — a boot-time budget per layer, so bring-up cost is designed rather
   than discovered.
6. **Tape-out gate** — all checks clean or no release.

The single rule that carries the rest: **nothing is signed off on inspection.**
Every claim this project has lost was lost to something that looked right and was
never mechanically checked.
