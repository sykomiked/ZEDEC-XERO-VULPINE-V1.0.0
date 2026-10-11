<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Game-engine kits

These kits are for games that run **deterministic lockstep**: every peer
runs the same simulation from the same inputs and sends only the inputs
over the network. Lockstep breaks when two machines compute even one
different bit, and floating point is the usual cause: the same expression
can round differently on another CPU, compiler or optimisation level.

The kits keep game state **exact**. Positions, speeds, health, money and
timers are rationals (an int64 numerator over an int64 denominator) and
the arithmetic runs in [`libzxv`](../libzxv/), which never rounds and
reports overflow instead of wrapping. Game rules can use the 64-state
hexagram tag (four-valued logic with polarity and a lifecycle phase), and
each tick can have an exact work budget.

| Kit | What it is | Tested here |
|---|---|---|
| [emu-state](emu-state/) | The engine-neutral C11 library: fixed-step tick driver, rational and hexagram state slots, an input queue per player per tick, a SHA3-256 state hash per tick, rollback to a kept tick, desync detection, per-tick budgets and state transfer. | 1,138 unit checks, a fuzz target (2,000 programs in `make test`, 100,000 under ASan + UBSan), gcc and clang |
| [godot-zxv](godot-zxv/) | A Godot 4.3 GDExtension: `ZxvRational`, `ZxvHexagram`, `ZxvLockstep`, and a demo project. | 1,010 checks of the C++ layer without Godot; the extension built against godot-cpp `godot-4.3-stable` and 542 checks run inside Godot 4.3 headless on Linux |
| [unity-zxv](unity-zxv/) | A Unity package, `com.zedec.zxv`: a `ZxvLockstep` core (no MonoBehaviour), a serializable exact `Rational` that stores `num` and `den`, a `Hexagram`, and inspector drawers. | 27 xunit tests of the Runtime code built as netstandard2.1 with .NET 8; not run inside Unity |

Start with [emu-state](emu-state/) to see the model, then open the kit for
your engine.

## The rules every kit follows

- **No floats in game state or at the boundary.** A float or double
  cannot be turned into a rational by these kits (Godot refuses it at run
  time, C++ and C# at compile time). Values come from integers or exact
  text ("12.75", "3/4").
- **Floats only at the very edge, for drawing.** `to_float_lossy()`
  (Godot) and `ToFloatLossy()` (Unity) give the nearest float for a
  Transform or a label. That value is lossy and must never go back into
  the simulation or over the network.
- **Time is an integer.** Feed the fixed-step clock integer microseconds
  (Godot `Time.get_ticks_usec()`, .NET `Stopwatch`), never the engine's
  float `delta`.
- **The step is a pure function** of the state and the inputs: no wall
  clock, no host random numbers, no reading of engine physics.

## Honest limits

- Lockstep only works when **every peer runs the same build** (the same
  step code and the same libzxv) and gets **the same inputs**. The kits
  detect a divergence by comparing state hashes; they cannot repair one
  (resync by state transfer: `export`/`import`).
- **No anti-cheat.** Hashes show that two peers disagree, not which one
  is honest. Every peer has the full game state, so a modified client can
  read it (map hacks) and can send any input it likes. Nothing here makes
  a game "unhackable".
- The state hash is SHA3-256 for integrity between cooperating peers. It
  is not a signature; sign the hashes yourself if peers must not be able
  to deny them.
- Exact rationals cost more than floats, and denominators can grow:
  `1/3 + 1/7 + 1/11 ...` gets bigger with every distinct prime. Keep
  quantities on a common grid (thirds, sixtieths) and expect
  `ZXV_E_RANGE` when a value no longer fits, rather than a silent error.
