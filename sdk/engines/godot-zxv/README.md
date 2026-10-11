<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# godot-zxv: deterministic lockstep for Godot 4

A GDExtension that brings exact rational game state, 64-state hexagram
rule tags and [emu-state](../emu-state/)'s lockstep simulation to Godot
4.3 and later. Three classes:

| Class | What it is |
|---|---|
| `ZxvRational` | An exact rational. Built from two ints or exact text; a float is refused. Arithmetic never rounds; overflow is an error, never a wrap. |
| `ZxvHexagram` | The 64-state hexagram tag (four-valued base, polarity, lifecycle phase), every rule from libzxv. |
| `ZxvLockstep` | One lockstep sim: rational and hexagram slots, per-player inputs per tick, a SHA3-256 hash per tick, rollback, desync checks, per-tick budgets, a fixed-step clock and state transfer. |

## Build

You need CMake 3.20+, a C++17 compiler, Python 3 (godot-cpp generates its
bindings with it), git and a built libzxv.

```sh
make -C sdk/libzxv
cmake -S sdk/engines/godot-zxv -B sdk/engines/godot-zxv/build/ext -DCMAKE_BUILD_TYPE=Release
cmake --build sdk/engines/godot-zxv/build/ext -j
```

CMake fetches godot-cpp at configure time (it is not vendored): tag
`godot-4.3-stable`, checked against commit
`fbbf9ec4efd8f1055d00edb8d926eef8ba4c2cce`, so a moved tag fails the
build. Change both with `-DZXV_GODOT_CPP_TAG=... -DZXV_GODOT_CPP_COMMIT=...`
for another Godot version. The first build compiles all of godot-cpp
(about 10 minutes on 4 cores).

The build writes `demo/bin/libzxv_godot.so` (`.dylib` on macOS) and copies
`libzxv` next to it; `demo/zxv.gdextension` points Godot at both. To use
the extension in your own project, copy `demo/bin/` and
`demo/zxv.gdextension` into it.

Without network access, or without Godot at all, build and test only the
C++ layer the classes wrap:

```sh
cmake -S sdk/engines/godot-zxv -B build-core -DZXV_GODOT_EXTENSION=OFF
cmake --build build-core -j && ctest --test-dir build-core --output-on-failure
```

## Quickstart (GDScript)

```gdscript
extends Node

var sim := ZxvLockstep.new()

func _ready() -> void:
	sim.setup(2, 2, 2, 2, 32, false)            # players, rational slots, hexagram slots,
	                                            # input bytes, history, predict
	sim.set_rational(0, ZxvRational.from_ints(0, 1))
	sim.start()                                 # tick 0
	sim.submit_input(0, 1, PackedByteArray([1, 0]))
	sim.submit_input(1, 1, PackedByteArray([0, 0]))
	sim.advance(step)                           # tick 1
	print(sim.get_rational(0))                  # 1/3
	var h := sim.state_hash(1)                  # send to the other peers

func step(_tick: int) -> int:                   # the game rules for one tick
	if sim.input(0)[0] == 1:
		return sim.add_rational(0, ZxvRational.from_ints(1, 3))  # x += 1/3 exactly
	return ZxvLockstep.OK
```

## The demo

`demo/` is a Godot project. Two peers run in one window: peer A predicts
its opponent's inputs, which reach it three ticks late, and rolls back when
a guess was wrong; peer B runs strict lockstep. Arrow keys move player 0,
space asserts its claim; player 1 is a scripted bot. The label shows the
confirmed tick, the number of rollbacks and whether the peers' final
hashes agree.

```sh
godot --path sdk/engines/godot-zxv/demo --import    # once, registers the extension
godot --path sdk/engines/godot-zxv/demo             # run it
```

`demo/game.gd` holds the rules: a pure function of the state and the
inputs. `demo/main.gd` feeds the clock integer microseconds from
`Time.get_ticks_usec()` (never the float `delta`) and converts positions to
floats only in `_draw`.

## Concepts in GDScript terms

| Concept | GDScript form |
|---|---|
| Exact numbers | `ZxvRational.from_ints(3, 4)`, `ZxvRational.parse("12.75")`. `from_ints(0.5, 1)`, `parse(0.5)` and `from_float(x)` push an error and return `null`: Godot would otherwise truncate a float passed to an int parameter. `str(r)` and `r.text()` are exact ("0.3", "1/3"); `r.fraction()` is always "n/d". |
| Drawing | `r.to_float_lossy()` is the nearest float, for a position on screen. It is lossy; never store it in game state or send it to a peer. |
| Failures | A method that cannot give its value pushes an error and returns `null`, or returns a status int (`ZxvLockstep.OK` is 0, `E_RANGE`, `E_BUDGET`, `E_STATE`, ...). `ZxvLockstep.status_name(rc)` and `sim.last_error()` explain it. |
| Ticks | `advance(step)` runs `step(tick) -> int` between begin and commit; a non-zero return aborts the tick. `begin()`, `commit()` and `abort()` are there if you prefer to drive it yourself. |
| Inputs | `submit_input(player, tick, bytes)`; inside a step, `input(player)` and `input_predicted(player)`. |
| Rollback | `setup(..., predict = true)`; when `rollback_pending_tick() >= 0`, call `rollback_replay(step)`. |
| Desync | `compare(tick, peer_hash)` returns `TRUTH_TRUE`, `TRUTH_FALSE` (recorded, see `desync_tick()`) or `TRUTH_NEITHER` (no verdict yet). |
| Budget | `set_tick_budget(ZxvRational.from_ints(100, 1))`, then `charge(amount)` inside a step returns `E_BUDGET` past the limit. The sim owns a private libzxv context and a capability narrowed to BUDGET only. |
| Four-valued logic | `ZxvHexagram.from_truth(ZxvHexagram.TRUTH_BOTH, polarity, phase)`, `h.to_truth()`; no conversion to `bool`. Base TRUE is 3, Truth TRUE is 1: always convert, never cast. |

## Tests

| What | Where | Result here |
|---|---|---|
| The C++ layer (`src/core`): rationals, every hexagram tag, two lockstep peers with late inputs agreeing hash for hash, budgets, state transfer, the clock, moves; built with `-fno-exceptions` like Godot builds extensions | `ctest` (`test_core`, plus the no-float check on `src/core`) | 1,010 checks pass with gcc 13 and clang 18; also under ASan + UBSan (gcc) |
| The GDExtension inside Godot: refusing floats, exact text, hexagram rules, two peers with rollbacks, desync, a late joiner, the budget | `godot --headless --path demo --script res://tests/test_zxv.gd` (exits with the number of failures) | 542 checks pass in Godot 4.3-stable headless on Linux x86_64 (the refusals print `ERROR:` lines on purpose) |
| The demo scene | `godot --headless --path demo --quit-after 300` | runs without errors; the peers report "in sync" |

CI ([`.github/workflows/sdk-engines.yml`](../../../.github/workflows/sdk-engines.yml))
builds the core tests on Linux and macOS, and builds the extension and runs
the headless Godot tests on Linux (Godot 4.3-stable, checked against its
published SHA-512).

## Honest limits

- Tested with Godot 4.3-stable on Linux x86_64. macOS builds of the
  extension are compiled by CI but not run inside Godot; Windows, Android,
  iOS and Web exports are not built or tested (libzxv has no Windows build
  in this repository yet).
- The step you write in GDScript must itself be deterministic. GDScript
  floats, `randf()`, `Time` inside the step, physics results and
  `Dictionary` iteration order you did not control will all break
  lockstep, and these classes cannot see them.
- Every peer must run the same build of the game and the extension and
  get the same inputs. Hash comparison detects a divergence; it is not
  anti-cheat and nothing here makes a game "unhackable" (see
  [the engines overview](../README.md#honest-limits)).
- `ZxvLockstep` is not thread-safe; use it from one thread.
