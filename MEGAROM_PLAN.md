<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 -->

# The MegaROM — plan & resume point

> **Purpose of this file:** a durable place to pick up the MegaROM work after a
> break. It captures the vision (from the driving-session notes), what is
> already built, the honest scope, and the phased path. Full speed continues;
> this is the map, not a stop.

## The vision (as stated)

- Every video game ROM is **its own state / universe** — its own rules,
  mechanics, and (NES/SNES era and beyond) its own **story of character
  relationships**. Older Atari/arcade games have no story but carry **mechanics**
  worth keeping — treat each as a **simpler engine**.
- The kernel/OS is **non-linear and phase-tick oriented**, so it can **hold many
  of these states at once — even contradictory ones** (the M⁵ GLUT states). That
  is a **multiverse**, not a single machine.
- The **Game Master** serves multiple functions: (1) fault-stress QC [in
  progress], then (2) the games become **deterministic training data** for the
  **Chiglet** AI, expressed in the **Sutra** language, to reach a Chiglet MVP.
  Character interactions inside games are relationship datasets.
- **The MegaROM:** build *our own* game ROM that **runs the OS inside the kernel**
  and **combines the best mechanics of all 144k+ games across the 84 consoles**,
  natively. People can **load their own ROMs** into the OS — a ROM may **change
  mechanics without taking over the system** (games as pluggable mods).
- **Universal compatibility, forward and backward:** the MegaROM should span
  every generation — ROM-era (the 84 consoles) → RAM/disc-model consoles → up to
  the PlayStation-6 generation → modern game engines and Steam-type PC games.
- **The UI is itself a video-game-style interface.** The UI layers on top of the
  kernel are eventually a **series of MegaROMs you boot on top of the kernel**
  (the functional 13-space lattice desktop is the seed of this).

## Honest scope

Emulating everything from Atari 2600 to PS6 + modern engines + Steam is a
decades-scale effort. **We do not build that now.** The near-term, achievable
deliverable is the **extensible architecture** + the first mechanic primitives,
so that compatibility grows generation-by-generation without re-architecting.
"Prepare for it" = lay the abstractions and the roadmap; the corpus campaign and
Chiglet training proceed in parallel.

## What is already built (the foundation)

- **CPU cores:** MOS 6502 (complete), Zilog Z80 (complete: base+CB+ED+DD/FD+port
  I/O+interrupts). Host-tested. `kernel/src/emu/cpu6502.*`, `cpu_z80.*`.
- **Faithful per-console machines** (the "run it, verify it's alive" pattern):
  NES mapper-0 (`nes.*`, 36/40 real ROMs RUNNING), Sega Master System / Game Gear
  (`sms.*`, 29/30 + 14/15, 0 false positives). Behavioural running verdict.
- **Game runner** (`game_runner.*`): executes an attached ROM on-target, routes
  iNES → NES machine; reports facts (no false "running" claim for bare CPU).
- **M5-RELATE** (`emu_relate.*`): measures the *relationship* between two
  non-compatible cores along the perpendicular M⁵ axes (ω/r/ℓ/φ/χ).
- **Structural congruence gate** (`congruence.*`): a boot QC axiom — a state is
  valid only if geometrically sound (Fibonacci sets, complex plane, M5-axis
  perpendicularity, >3-valued logic).
- **Game universe → Chiglet multiverse** (`game_universe.*`): a game's mechanics
  → 8-dim evidence vector; Chiglet's ISF holds many at once and counts the
  DISTINCT ones, collapsing duplicates. **This is the MegaROM's first stone:
  mechanic extraction + dedup.** Boot: "multiverse holds 3 distinct game
  universes; a duplicate collapsed".
- **Game Master harness** (`build_system/game_master.py`): stratified, both
  polarities (S+/S−), auto-logs, preserves the exact binary so every fault maps
  to source. Corpus at `/Users/36n9/Downloads/GAMEMASTER` (140,372 files, 81
  ecosystem dirs).
- **Chiglet** (`chiglet.*`): ISF mixture-of-experts; `chg_infer`,
  `chg_effective_experts` (distinct count), `chg_interaction` (relationship).
  **Sutra** (`kernel/src/sutra/`) incl. `sutra_chiglet.c` bridge; Sutra speaks
  exact rationals — the training representation.
- **The 13-space lattice desktop** (`kernel/src/desktop/zxv_shell.c`): a
  phase-tick, stacked-screens, video-game-style UI; each space calls a real
  subsystem. The seed of "UI layers as bootable MegaROMs".

## The phased path

**Phase A — Mechanic corpus (in progress).**
Run the Game Master over the corpus; for each game capture its mechanics vector
(`game_universe_vector`) alongside the fault result. Output: a deduplicated set
of DISTINCT mechanic-universes across all runnable games (Chiglet's k_distinct).

**Phase B — Chiglet MVP training.**
Label the distinct mechanic-universes (genre/mechanic class), build Chiglet
prototypes, and train toward a DECIDED classifier over game universes. Extend to
character-relationship datasets for story-era games (harder; needs game-content
extraction, not just CPU traces) via Sutra.

**Phase C — Mechanic-primitive abstraction (the MegaROM core).**
Define a console-agnostic **mechanic primitive** interface (input model, display
model, timing/interrupt model, memory model, audio model). Each console's
machine (NES, SMS, …) becomes a *provider* of primitives. The MegaROM composes
the DISTINCT primitives Chiglet kept.

**Phase D — Native ROM loading + mechanic mods.**
A loaded ROM registers/overrides mechanic primitives **without taking over the
system** (sandboxed, capability-gated — reuse the W^X + EL0 + capability model).
Backward: the 84 ROM-era consoles. Forward: RAM/disc-model, then modern engines
via primitive adapters (long tail).

**Phase E — UI layers as MegaROMs.**
Package UI layers (starting with the 13-space lattice desktop) as bootable
MegaROMs on the kernel — the video-game-style OS face.

## Resume checklist (start here after the break)

1. `cd 05_KERNEL && git log --oneline -15` — see the latest emu/debug commits.
2. Check the campaign: newest `gamemaster_logs_33k_v*/summary.txt`; the preserved
   `kernel_arm64.elf` in that dir maps any fault to source.
3. **Open debug:** task #41 — `kernel_main +0x24d0` scan-loop wild-index fault
   (FAR=0x7A0…, `w0≈0xCCCCCCCC`). The fault handler now **dumps x0–x30**; grab a
   `fault_*.log` that shows `kernel_main` and read the register dump to see which
   register is poison. Then fix precisely. (`proc_enter_el0` fault already FIXED
   + verified via IRQ-atomicity — commit `d5f7e95`.)
4. **Next build:** Phase A wiring — have `game_master.py` (or an in-kernel pass)
   emit each game's `game_universe_vector` into a corpus file, then feed batches
   to `chg_effective_experts` to accumulate the distinct-mechanic set.
5. Mirror every kernel change to `ZEDEC_pqOS_CLEAN`; commit in `05_KERNEL`.
6. 32-bit builds remain blocked pending the cross-compiler recompile (Linux
   handover track) — stay on ARM64 for now.
