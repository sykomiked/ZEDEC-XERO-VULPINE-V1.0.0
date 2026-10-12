<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Why there are game ROM engines in the kernel

Readers of `kernel/src/emu/` often ask why an operating system for a local AI
swarm carries NES, Master System, Game Boy and Genesis machines. The short
answer: **they are a debugging instrument.** Old cartridge games were used to
stress test the architecture during its early development.

## The reasoning

Games from the cartridge era are **ROM, not RAM**. The program is burned into
read-only memory and never changes. That makes each one a useful test fixture:

- **Fixed input.** The same bytes go in on every run, so a run is repeatable
  and a fault can be traced to an exact byte offset in an exact image.
- **Known-good behaviour.** These programs ran correctly on real hardware for
  decades. When one misbehaves on ZXV, the fault is in our code (the storage
  path, the VFS, the event pipeline or a CPU core), not in the game.
- **Real, varied machine code.** Thousands of programs written by different
  teams for different CPUs (6502, 65816, Z80, LR35902, 68000, HuC6280, ARM7)
  exercise far more instruction mixes, timing assumptions and odd edge cases
  than hand-written tests would.
- **Hostile-ish data.** Raw images are arbitrary binary blobs to the block
  layer and parsers, which makes them good fuzz input for a young kernel.
- **Determinism.** Determinism is a core design goal of ZXV (lockstep
  multiplayer, bit-identical inference across CPUs). A deterministic program
  on a deterministic machine must give the same result every time, so any
  drift shows up immediately.

## What actually exists

| Piece | Where | What it does |
|---|---|---|
| CPU cores | `kernel/src/emu/cpu6502.*`, `cpu65816.*`, `cpu_z80.*`, `cpu_lr35902.*`, `cpu_m68k.*`, `cpu_huc6280.*`, `cpu_arm7.*` | Interpreters for the CPUs of that era. The 6502, Z80 and Master System machine have host tests in `make verify-all`. |
| Console machines | `kernel/src/emu/nes.*`, `snes.*`, `sms.*`, `gb.*`, `gba.*`, `genesis.*`, `pce.*` | Enough of each machine (memory map, mapper basics, video/IRQ registers) to decide whether a ROM is actually running. Each has a `*_selfcheck()`. |
| Game runner | `kernel/src/emu/game_runner.*` | Reads an image attached as a virtio-blk device, detects its format, and runs it on the matching machine. Reports facts (instructions retired, frames, IRQs) and coverage markers. |
| Boot self-checks | `kernel/arch/arm64/kernel_main_arm64.c` | The arm64 boot runs the emulator self-checks and the 6502-to-Z80 relationship probe (`emu_relate.*`), and calls `game_runner_run` when an image is attached. |
| Game Master harness | `build_system/game_master.py` | Boots the kernel in QEMU with each ROM attached, drives an input storm, and logs every kernel `[FAULT]` with the faulting function. Also runs each image byte-inverted ("negative space") to exercise the opposite polarity of the stack. |
| Corpus extractor | `tools/megarom_extract.c` | Runs ROMs through the machines and records an 8-number mechanics vector per game. |

The harness itself notes that it is a stress tool first. The emulator machines
were added so the kernel could tell the difference between "the image loaded
without crashing" and "the program is really executing".

## Where it went next

Once the engines worked as a test bench, they became the seed of later ideas,
described in [`MEGAROM_PLAN.md`](../../MEGAROM_PLAN.md): extracting game mechanics
as deterministic training data for the Chiglet model, a mod admission gate,
and a game-style desktop. Those are follow-on uses. The original reason the
engines are in the kernel is the one above: fixed, known-good ROM programs
make a strong stress test for a new architecture.

## Copyright: no commercial game ROMs ship in this repository

- **No ROM images are committed.** The repository contains no `.nes`, `.sfc`,
  `.smc`, `.gb`, `.gbc`, `.gba`, `.sms`, `.gg`, `.gen` or `.pce` files, and
  none have ever been added to its history.
- **Self-checks use our own test programs.** Each `*_selfcheck()` builds a
  tiny hand-written program in memory (for example, `nes_selfcheck` in
  `kernel/src/emu/nes.c` assembles a 16 KB image with a few 6502 instructions).
- **The test corpus stays on the developer's machine.** The Game Master and
  extractor take a path to a local folder; nothing from that folder is copied
  into git.
- **`megarom_corpus/` holds derived numbers, not game data.** Each line is an
  8-number mechanics vector with the source file name as a label. No game code,
  graphics or audio is included.

Anyone reproducing these tests should use ROMs they have the right to use:
homebrew, public-domain test ROMs (such as the widely used CPU test suites),
or dumps of cartridges they own where local law allows it.
