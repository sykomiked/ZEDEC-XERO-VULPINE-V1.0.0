<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# unity-zxv: deterministic lockstep for Unity

A Unity package, `com.zedec.zxv`, for lockstep games with exact game
state. It calls the native [emu-state](../emu-state/) and
[libzxv](../../libzxv/) libraries through P/Invoke.

| Type | What it is |
|---|---|
| `ZxvLockstep` | One lockstep sim, not a MonoBehaviour: rational and hexagram slots, per-player inputs per tick, a SHA3-256 hash per tick, rollback, desync checks, per-tick budgets, a fixed-step clock and state transfer. Drive it from your own `FixedUpdate`, a server loop or a test. |
| `Rational` | An exact rational, `[Serializable]`, that stores exactly two `long` fields, `num` and `den`. No float field, no float constructor. |
| `Hexagram` | The 64-state hexagram tag, `[Serializable]` as one byte. |
| `Truth` | Four-valued logic (`Neither`, `True`, `False`, `Both`), no conversion to `bool`. |
| Editor | Property drawers: a `Rational` is one text field ("3/4", "12.75"), parsed exactly and stored as `num`/`den`; a `Hexagram` shows its name. |

## What was tested, and what was not

Unity is not installed where this package was written, so:

- **Tested:** the `Runtime/` code, compiled as a plain `netstandard2.1`
  library with C# 9 (Unity 2021.3's language version) and run under .NET 8
  against the real native libraries: 27 xunit tests in
  [`dotnet/`](dotnet/), including the shared conformance vectors
  (`sdk/conformance/vectors.json`: rational arithmetic and parsing, all 64
  hexagram tags, the 64 x 64 `and`/`or` tables, `step` and `set`) and two
  peers, one predicting the other's late inputs, ending hash for hash
  identical. CI runs them on Linux and macOS.
- **Not tested:** anything inside Unity: the Editor drawers (they use
  `UnityEditor` and are not compiled here), Mono and IL2CPP players,
  serialization of `Rational` in real scenes and prefabs, and every
  player platform. The code avoids what usually breaks there (no
  callbacks from native code, so IL2CPP needs no reverse P/Invoke; only
  blittable signatures; `__Internal` on iOS), but that is design, not a
  test result.

## Install into a Unity project

1. Build the native libraries for each platform you target:

   ```sh
   make -C sdk/libzxv
   make -C sdk/engines/emu-state        # build/libzxv_emu.so (.dylib on macOS)
   ```

2. Copy `com.zedec.zxv/` into your project's `Packages/` folder (or use
   *Package Manager > Add package from disk* and pick its `package.json`).
   Unity writes the `.meta` files on import; the folder in this repository
   has none, so install it as a local or embedded package, not from a git
   URL.

3. Put the native libraries in `Assets/Plugins/` (or the package's
   `Plugins/` folder) and set each one's platform and CPU in its Inspector:

   | Platform | Files |
   |---|---|
   | Linux | `libzxv.so`, `libzxv_emu.so` |
   | macOS (Editor and player) | `libzxv.dylib`, `libzxv_emu.dylib` |
   | Windows | `zxv.dll`, `zxv_emu.dll` (not built by this repository yet) |
   | iOS | static `libzxv.a`, `libzxv_emu.a` (bound as `__Internal`) |

   `libzxv_emu` loads `libzxv` from its own folder, so keep the two
   together.

## Quickstart

```csharp
using Zedec.Zxv;

using var sim = new ZxvLockstep(players: 2, ratSlots: 2, hexSlots: 0,
                                inputBytes: 1, history: 32, predict: false);
sim.SetRational(0, Rational.Parse("2.5"));      // setup, before Start
sim.Start();                                     // tick 0

ZxvStatus Step(ZxvLockstep s, ulong tick) =>     // the rules for one tick
    s.Input(0)[0] == 1 ? s.AddRational(0, Rational.Of(1, 3)) : ZxvStatus.Ok;

sim.SubmitInput(0, 1, new byte[] { 1 });
sim.SubmitInput(1, 1, new byte[] { 0 });
sim.Advance(Step);                               // tick 1
Debug.Log(sim.GetRational(0));                   // 17/6, exactly
byte[] hash = sim.Hash(1);                       // send to the other peers
```

In a `MonoBehaviour`, feed the clock integer microseconds from a
`System.Diagnostics.Stopwatch` (not `Time.deltaTime`, which is a float) and
run the ticks it returns:

```csharp
long now = stopwatch.Elapsed.Ticks / 10;         // microseconds
int due = sim.ClockFeedMicroseconds(now - last); last = now;
for (int i = 0; i < due; i++) { SubmitLocalInput(); sim.RollbackReplay(Step); sim.Advance(Step); }
// Drawing only: the exact value becomes a float here and never goes back.
transform.position = new Vector3(sim.GetRational(0).ToFloatLossy(), 0, 0);
```

## API overview

| Area | Members |
|---|---|
| Lifetime | `new ZxvLockstep(players, ratSlots, hexSlots, inputBytes, history, predict)`, `Start()`, `Dispose()` (a `SafeHandle` releases the sim, then its private context, if you forget) |
| Inputs | `SubmitInput(player, tick, bytes)` returns a `ZxvStatus` (`Ok`, `Range`, `Duplicate`, ...); `Input(player)`, `Input(player, out predicted)`, `Ready` |
| Ticks | `Advance(Step)` (begin, your step, commit or abort; returns the status), `Tick` |
| State | `GetRational`, `SetRational`, `AddRational`, `MulRational`, `GetHexagram`, `SetHexagram` |
| Budget | `SetTickBudget(limit)`, then `Charge(amount)` inside a step returns `Budget` past the limit. The sim owns a private libzxv context and a capability narrowed to BUDGET only. |
| Desync | `Hash(tick)`, `Hash(tick, out isFinal)`, `ConfirmedTick`, `Compare(tick, peerHash)` returns `Truth.True`, `Truth.False` (recorded in `DesyncTick`) or `Truth.Neither` (no verdict yet) |
| Rollback | `RollbackPendingTick`, `RollbackReplay(Step)`, `Rollback(toTick)` |
| Clock | `ClockSetStep(Rational.Of(1, 60), maxCatchup)`, `ClockFeedMicroseconds(usec)`, `ClockAlpha()` |
| Transfer | `ExportState()`, `ImportState(bytes)` |
| Errors | Misuse throws `ZxvException` (`Status`, `Code`, `Detail`); outcomes a game must handle (`Budget`, `Range`, `Duplicate`, inputs missing) are returned as `ZxvStatus` |

## Build and test without Unity

```sh
make -C sdk/libzxv && make -C sdk/engines/emu-state
cd sdk/engines/unity-zxv/dotnet
dotnet build Zxv.Unity.sln -c Release
dotnet test Zxv.Unity.sln -c Release --no-build
```

`Zxv.Unity.Runtime.csproj` compiles `../com.zedec.zxv/Runtime/**/*.cs`
(the files Unity compiles through `Zedec.Zxv.asmdef`); Unity never sees
the `.csproj`. The test project copies both native libraries next to the
test assembly, the way a Plugins folder holds them.

## Honest limits

- Not run inside Unity yet (see above).
- Your step must itself be deterministic: no `float` maths, no
  `UnityEngine.Random`, no `Time`, no physics results, no `Dictionary`
  iteration order you did not control. The package cannot see what your
  step does.
- Every peer must run the same build and get the same inputs. Hash
  comparison detects a divergence; it is not anti-cheat and nothing here
  makes a game "unhackable" (see [the engines overview](../README.md#honest-limits)).
- A hand-edited asset can hold a non-reduced or zero-denominator
  `Rational`; `IsCanonical` reports it and arithmetic on a zero
  denominator throws rather than guessing.
- `ZxvLockstep` is not thread-safe; use it from one thread.
