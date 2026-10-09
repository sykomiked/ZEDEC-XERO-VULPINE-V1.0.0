<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

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
- **Holographic render primitive** (`emu/holo.{c,h}`): the depth-on-a-flat-screen
  shader extracted from `zxv_present` — colour/anti-colour phase-tick shimmer +
  chromostereopsis (near warm / far cool). Verified optics, reusable by every
  layer. Boot: "holo shader sound — near=warm, far=cool, grey-stable".
- **Story unification** (`emu/story_mechanics.{c,h}`): story-bearing consoles as
  8-dim story-mechanics signatures; the ISF holds their diversity and unifies
  them into one connected universe. Boot: "6 consoles → 5 distinct archetypes,
  coherence 63 permille". (Honest: era-capability priors, not parsed plots.)
- **Netplay bridge** (`emu/netplay.{c,h}`): the FAR SIDE of the bridge — modern
  networked play (PS2-online forward) adapted into our P2P DISTRIBUTED model on
  the real `mesh_net` + Porter House stack. A game universe = a P2P session peers
  join directly; each peer is a user + **Chiglet** operating ALONGSIDE them (the
  play-with-you companion; complementary per ISF, not a mirror); the session
  relates peers across the mesh = the distributed multiverse, a productivity-
  capable workspace. Boot: "2 peers joined, user↔Chiglet + cross-peer link hold
  (189 permille)". Next: two-physical-node transport via `mn_send_data`.
- **Console-innovation trajectory** (`emu/console_trajectory.{c,h}`): every
  generation innovates a NEW signature mechanic (outer/spatial, inner-space,
  hybrid-form, network) yet builds one continuous lineage. Our own generation
  names (Hearth/Loom/Pillar/Aura/Mirror/Chimera/Relay/MeshHD/Forge/Prism/Aether);
  real consoles are `/* ref: */` dev references only. Boot: "7 distinct
  innovations, one lineage; sub-lineage continuity 120 permille".
- **Break-potency detector** (`emu/break_potency.{c,h}`): every game/magic system
  has a way to become brazenly OVER-POTENT — synergy stacks (ratio>1.5) and
  feedback loops (gain≥1). The SAME detector is the **sandbox guardrail** for
  mechanic-mods (`broken` = the cap/contain signal). Boot: "safe/synergy/feedback
  separated (synergy build 1899 permille → broken, contain it)".
- **Mechanic-mod admission gate** (`emu/mechanic_mod.{c,h}`): the real policy that
  lets a mod change mechanics WITHOUT taking over the system. Runs the mod through
  `break_potency`; bounded → ADMITTED at full strength; over-potent → CONTAINED
  with its potency capped back to the additive base (the emergent synergy denied,
  the parts kept). Produces the verdict + cap the EL0/W^X sandbox enforces. Boot:
  "modest mod admitted, god-stack CONTAINED (capped at 2699 permille of raw)".

## Cross-cutting requirements (apply to the whole OS + every MegaROM layer)

- **Holographic, nonlinear rendering — toward beyond-PS6.** Classic games are
  the grounding, but the engine must scale to beyond-PlayStation-6-level
  graphics. The holographic effect is achieved on ordinary 2D screens via
  `holo_shade` (colour/anti-colour + chromostereopsis), driven by the nonlinear
  phase-tick field — no glasses, no special panel. Path: per-object depth →
  per-primitive depth → a full nonlinear rendering engine the MegaROM composes.
- **Device-adaptive, auto-configuring.** One OS that detects the device and
  configures itself: **phone, tablet, laptop, desktop, and server.** Detect the
  framebuffer resolution (ramfb/GOP already reports it) and the input class
  (virtio touch/tablet vs mouse+keyboard vs headless) at boot, and pick the
  layout, input model, and (for server) a headless/no-GUI profile. Server config
  also ties into the Linux cross-compile handover (`SERVER_HANDOVER.md`).

## The phased path

**Phase A — Mechanic corpus (STARTED — tool built, running).**
`tools/megarom_extract.c` runs real ROMs through the emulator machines,
extracts each game's 8-dim mechanics (`game_universe_vector`), and produces
TWO corpora via ONLINE dedup (each game vs the growing palette, so it scales
to 144k): (1) the deduplicated MECHANIC PALETTE (the MegaROM's mechanic set),
and (2) the SOCIAL/STORY training set for Chiglet — story-capable games only
(`gu_story_capable`), in Sutra rationals. First run: 637 real NES games ->
10 DISTINCT mechanic classes + 512 story contributors
(`megarom_corpus/megarom_palette_nes.sutra`, `chiglet_social_nes.sutra`).
NEXT: extend to SMS/Z80 + the zipped corpus (needs unzip), tune the merge
threshold for richer palettes, and feed the social set to Chiglet (Phase B).

**Phase A-orig — Mechanic corpus (superseded by the above).**
Run the Game Master over the corpus; for each game capture its mechanics vector
(`game_universe_vector`) alongside the fault result. Output: a deduplicated set
of DISTINCT mechanic-universes across all runnable games (Chiglet's k_distinct).

**Phase B — Chiglet MVP training.**
Synthesis foundation built (`emu/megarom_synth.{c,h}`): the MegaROM's MECHANICS
come from ALL runnable games (deduped to the distinct best via Chiglet's ISF)
and its STORIES from the story-capable NES/SNES-era+ subset (`gu_story_capable`:
substantial content + structured display/interrupt flow) — classics are
mechanics-only. Boot: "synthesis: N distinct mechanics + M story contributor(s),
presented beyond-PS6". Presentation is decoupled from source generation and
rendered through `holo_shade` (beyond-PS6 holographic graphics). NEXT: 
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

**Phase E — UI layers as MegaROMs (console model).** [foundation built]
The interface resembles a **game console**; MegaROMs ARE the bootable UI — each
a UI layer you boot like a cartridge. `emu/megarom.{c,h}` is the console's
cartridge slot: a registry + boot selector (`megarom_register/boot/current`).
The 13-space lattice desktop is **MegaROM #0**. Next: a console front-end screen
that lists registered MegaROMs and boots between them; then package the desktop
(and each new UI layer / game) as a real bootable MegaROM with its own
render+input hooks driven through `holo_shade`.

## Resume checklist (start here after the break)

1. `cd 05_KERNEL && git log --oneline -15` — see the latest emu commits
   (`bcc225f` trajectory+break-potency, `95d413d` netplay).
2. Check the campaign: newest `gamemaster_logs_33k_v*/runs.jsonl` + `summary.txt`.
   As of this save point: **v9 at 2,920 runs, 0 faults** — both kernel faults
   (`proc_enter_el0` IRQ-atomicity `d5f7e95`; `kernel_main` NEON-not-saved-across-
   IRQ, `-fno-tree-vectorize`) FIXED and holding. The preserved `kernel_arm64.elf`
   in each logdir maps any future fault to source.
3. **Both original kernel faults are CLOSED** (tasks #39, #41). Regression gate
   green: `cd kernel && make verify-all` → "ALL STAGE-1 CHECKS PASSED"; a clean
   boot reaches `BOOT_OK` (QEMU flags: `-M virt,gic-version=3 -cpu cortex-a53` —
   gic-version=3 is REQUIRED or GICv3 init data-aborts early).
4. **Next builds (clean resume points), in priority order:**
   a. ~~mechanic-mod admission gate~~ **DONE** (`emu/mechanic_mod.{c,h}`) — the
      admission POLICY + potency cap are built and boot-verified. Remaining:
      have the EL0/process + W^X machinery actually ENFORCE the returned cap on a
      loaded mod (physical sandbox), and add a `megarom`/`game_runner` hook that
      calls `mechanic_mod_admit` before activating a loaded mechanic-mod.
   b. **Phase A wiring:** have `game_master.py` (or an in-kernel pass) emit each
      game's `game_universe_vector` into a corpus file, feed batches to
      `chg_effective_experts` for the distinct-mechanic set, and run each vector
      through `break_potency` to catalog its break-vectors.
   c. **65816 core → SNES machine** (task #43, 3,466 story games) — next faithful
      per-console machine; unlocks SNES story extraction for Chiglet.
   d. **Two-node netplay:** `mn_send_data` transport between two QEMU instances so
      the distributed multiverse runs over a real wire, not just in one process.
5. Mirror every kernel change to `ZEDEC_pqOS_CLEAN`; commit in `05_KERNEL`.
6. **Naming rule:** game/console/franchise names are DEV REFERENCES ONLY (`ref:`
   in comments); shipped features use our own names. See memory `zxv-proprietary-naming`.
7. 32-bit builds remain blocked pending the cross-compiler recompile (Linux
   handover track) — stay on ARM64 for now.
