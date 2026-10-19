<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# emu-state: deterministic lockstep over libzxv

A small C11 library for games and emulators that must compute the same
result on every machine. It holds the simulation state as exact rationals
and hexagram tags, moves it forward one tick at a time, hashes every tick,
and supports rollback and desync detection. It contains no floating point
at all (checked by `make check-nofloat` and compiled with
`-mgeneral-regs-only` where the compiler has it).

The Godot and Unity kits are thin layers over this library; the API is in
[`include/zxv_emu.h`](include/zxv_emu.h).

## Build and test

```sh
make -C sdk/libzxv                  # the library this one calls
make -C sdk/engines/emu-state test  # unit tests, fuzz smoke run, demo, no-float check
make -C sdk/engines/emu-state demo
make -C sdk/engines/emu-state SANITIZE=1 OUT=build/san test   # ASan + UBSan
make -C sdk/engines/emu-state fuzz FUZZ_ITERS=100000           # longer fuzz run
```

`make` builds `build/libzxv_emu.a` and `build/libzxv_emu.so` (`.dylib`
on macOS); the shared library finds `libzxv` next to itself, which is how
the Unity package ships them. `make dist` copies the headers and both
libraries to `build/dist/`.

## Quickstart

```c
#include "zxv_emu.h"

static int32_t step(zxv_emu *sim, uint64_t tick, void *user) {
    uint8_t in[1];
    size_t n;
    zxv_emu_input_get(sim, 0, in, sizeof in, &n, NULL);   /* player 0's input */
    if (in[0]) return zxv_emu_rat_add(sim, 0, 1, 3);      /* x += 1/3, exactly */
    return ZXV_OK;
}

zxv_emu *sim;
zxv_emu_create(/*players*/ 1, /*rat slots*/ 1, /*hex slots*/ 0, /*input bytes*/ 1,
               /*history*/ 64, /*flags*/ 0, &sim);
zxv_emu_start(sim);                                       /* tick 0 = initial state */
uint8_t move = 1;
zxv_emu_input_submit(sim, 0, 1, &move, 1);                /* input for tick 1 */
zxv_emu_advance(sim, step, NULL);                         /* tick 1 */
uint8_t hash[32];
zxv_emu_hash(sim, 1, hash, NULL);                         /* send this to your peers */
zxv_emu_destroy(sim);
```

[`examples/lockstep_demo.c`](examples/lockstep_demo.c) runs two peers in
one process: peer A predicts peer B's inputs, which arrive three ticks
late, and rolls back when a guess was wrong; peer B runs strict lockstep.
Both end with the same hash:

```
tick 30, 24 rollbacks on peer A
player 0 position 10/3 (exact)
peer A 99912a4a...c94609
peer B 99912a4a...c94609
in sync
```

## The model

| Part | What it does |
|---|---|
| State | `rat_slots` exact rationals (start at 0/1) and `hex_slots` hexagram tags (start at tag 0). Written during setup, then only inside a tick. |
| Tick | Tick 0 is the initial state (`zxv_emu_start`). Tick t is tick t-1 plus every player's input for tick t. `zxv_emu_begin`, your rules, then `zxv_emu_commit` (or `zxv_emu_abort`, which restores tick t-1). `zxv_emu_advance` does all three around a callback; the engine kits drive begin/commit themselves, so no callback crosses into Godot or Unity. |
| Inputs | One fixed-size blob per player per tick, accepted up to `history` ticks ahead. **Strict mode** (`flags` 0): a tick cannot begin until every input for it has arrived. **Predict mode** (`ZXV_EMU_F_PREDICT`): a missing input is predicted as the player's previous one; when the real one arrives and differs, `zxv_emu_rollback_pending` names the tick to redo and `zxv_emu_rollback_replay` re-simulates. |
| Hash | After each commit, SHA3-256 (libzxv's `zxv_sha3_256`) of the canonical encoding below. A tick is **final** when every input up to it was confirmed and no rollback below it is pending; only final hashes are compared. |
| Desync | `zxv_emu_compare(tick, peer_hash)` returns `ZXV_LP_TRUE` (equal), `ZXV_LP_FALSE` (different; recorded, see `zxv_emu_desync`) or `ZXV_LP_NEITHER` (not final here yet, or no longer kept: no verdict). |
| Rollback | `zxv_emu_rollback(to_tick)` restores any of the last `history` snapshots. |
| Budget | `zxv_emu_set_tick_budget(cap, num, den)` (needs `ZXV_CAP_BUDGET`) gives each tick an exact budget, reset at every `begin`; `zxv_emu_charge` returns `ZXV_E_BUDGET` when a tick's work would pass it. |
| Clock | `zxv_emu_clock_set_step(1, 60, max_catchup)` and `zxv_emu_clock_feed(elapsed_num, elapsed_den, &due, &dropped)`: a fixed-step accumulator in exact time; `zxv_emu_clock_alpha` is the fraction of a tick left over, for render interpolation. It is local pacing and is never hashed. |
| Transfer | `zxv_emu_state_export` / `zxv_emu_state_import`: the canonical encoding, for a late joiner or a resync after a desync. Import refuses anything malformed or non-canonical (`ZXV_E_FORMAT`). |

Errors follow the libzxv rules ([ABI.md](../../docs/ABI.md)): every call
returns `ZXV_OK` or a negative `ZXV_E_*`, out parameters are written only
on success, and `zxv_emu_last_error` gives the detail text.

### Canonical encoding (what is hashed)

```
"zxv/emu-state/v1"           16 ASCII bytes
tick                         u64 little endian
rat_slots, hex_slots         u32 little endian each
each rat slot                num i64 LE, den i64 LE (reduced, den > 0)
each hex slot                tag u8 (0..63)
```

`hash(t) = SHA3-256(encoding at tick t)`. Any language can recompute it;
`test_encoding_and_hash` in [`test/test_emu.c`](test/test_emu.c) builds it
byte by byte.

## Tests

| Command | What runs |
|---|---|
| `make test` | `check-nofloat`; `test_emu` (1,138 checks: argument validation, the encoding byte for byte, strict lockstep with inputs in different orders, desync detection, tick abort, exact arithmetic and overflow, budgets and capabilities, predict mode with late and out-of-order inputs, rollback windows, state transfer, the clock, determinism across runs); `fuzz_emu 2000`; the demo |
| `make fuzz` | the fuzz driver: random programs mixing late, early and repeated inputs, rollbacks, replays, hash comparisons, corrupted imports and clock feeds against a strict reference sim. It checks that every status is documented, that statuses are deterministic, that import either refuses with `ZXV_E_FORMAT` or round-trips, and that every final hash equals the reference |
| `make fuzz-libfuzzer` | the same target under libFuzzer, when clang ships it |

CI runs these with gcc and clang on Linux, clang on macOS, and under
ASan + UBSan ([`.github/workflows/sdk-engines.yml`](../../../.github/workflows/sdk-engines.yml)).

## Honest limits

- Determinism holds for **this library's state**. Your step function must
  also be deterministic: no floats, no wall clock, no host randomness, no
  iteration over unordered containers, no reading engine physics.
- Every peer must run the **same build** and get the **same inputs**.
  emu-state detects divergence; it does not prevent it, and it is not
  anti-cheat (see [the engines overview](../README.md#honest-limits)).
- An input that arrives after its rollback point has left the history
  ring is refused (`ZXV_E_RANGE`); that tick can never become final and
  the peers must resync by state transfer. Size `history` for your worst
  latency.
- A `zxv_emu` is not internally locked. Use one from one thread at a time.
- Memory is fixed at create time: `history` snapshots of the whole state
  plus a `2 x history` input ring, capped at 256 MiB.
