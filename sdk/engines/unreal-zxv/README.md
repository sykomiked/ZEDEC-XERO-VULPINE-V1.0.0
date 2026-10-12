<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# unreal-zxv: deterministic lockstep for Unreal Engine 5

An Unreal Engine 5 plugin, **ZxvUnreal**, that brings exact rational game
state, 64-state hexagram rule tags and [emu-state](../emu-state/)'s
lockstep simulation to Blueprint and C++. It is the same model as
[godot-zxv](../godot-zxv/) and [unity-zxv](../unity-zxv/).

> **Not yet compiled inside Unreal.** The plugin wrapper (the `.uplugin`,
> `ZxvUnreal.Build.cs` and the `UCLASS` / `USTRUCT` files) is written for
> UE 5.4 but this repository's CI has no Unreal Engine, so it has never
> been built by UnrealBuildTool or run in an editor. What *is* built and
> tested, on Linux and macOS, is the engine-neutral C++ layer every
> wrapper call goes through (`Source/ZxvUnreal/*/ZxvCore/`), against the
> real libzxv. A text-level check (`tools/check_ue_layout.py`) catches
> structural mistakes in the wrapper, but it is not a compile. Expect to
> fix small things the first time you build it in Unreal.

| Type | What it is |
|---|---|
| `FZxvRational` | An exact rational, `Num` / `Den` as two `int64`. Built from integers or exact text; there is no float constructor. Arithmetic never rounds; overflow is `OutOfRange`, never a wrap. |
| `FZxvHexagram` | The 64-state hexagram tag (four-valued base, polarity, lifecycle phase); every rule comes from libzxv. |
| `UZxvLibrary` | Blueprint functions for both: make, parse, add, compare, text, floor, `ToFloatLossy` (drawing only), every hexagram rule. |
| `UZxvLockstep` | One lockstep sim: rational and hexagram slots, per-player inputs per tick, a SHA3-256 hash per tick, rollback, desync checks, per-tick budgets, a fixed-step clock fed integer microseconds, and state transfer. |
| `UZxvSubsystem` | A `UGameInstanceSubsystem` that owns named sims and, for sims with `bAutoPace`, reads the monotonic clock in integer microseconds every frame and broadcasts `OnTicksDue`. |
| `zxvue::` (C++) | The engine-neutral layer: `Rational`, `Hexagram`, `Lockstep`, `Pacer`. No Unreal header, no exceptions, no floats. |

## Layout

```
ZxvUnreal.uplugin
Source/ZxvUnreal/ZxvUnreal.Build.cs
Source/ZxvUnreal/Public/ZxvCore/zxv_ue_core.hpp     engine-neutral layer (tested with CMake)
Source/ZxvUnreal/Private/ZxvCore/zxv_ue_core.cpp
Source/ZxvUnreal/Public/{ZxvTypes,ZxvLibrary,ZxvLockstep,ZxvSubsystem,ZxvUnrealModule}.h
Source/ZxvUnreal/Private/*.cpp                      the UE wrappers (not compiled in CI)
Source/ThirdParty/Zxv/                              staged libzxv + emu-state (git-ignored)
test/test_core.cpp, test/test_cross_cpp.cpp         the layer's tests
tools/stage_thirdparty.sh, tools/check_ue_layout.py
```

The layer uses only the public libzxv ABI (`sdk/libzxv/include/zxv_api.h`)
and emu-state's public header (`zxv_emu.h`); nothing under `kernel/`.

## Build the tested part (no Unreal needed)

CMake 3.20+, a C++20 compiler, Python 3 and a built libzxv:

```sh
make -C sdk/libzxv all
cmake -S sdk/engines/unreal-zxv -B build-ue-core
cmake --build build-ue-core -j && ctest --test-dir build-ue-core --output-on-failure

# the same under AddressSanitizer + UndefinedBehaviorSanitizer
cmake -S sdk/engines/unreal-zxv -B build-ue-san -DZXV_SANITIZE=ON
cmake --build build-ue-san -j && ctest --test-dir build-ue-san --output-on-failure
```

The layer is compiled with `-Wall -Wextra -Werror -Wshadow -fno-exceptions`
(Unreal builds modules without exceptions by default) as C++20 (UE 5.4's
default).

## Use it in an Unreal project (untested)

1. `sdk/engines/unreal-zxv/tools/stage_thirdparty.sh` builds libzxv and
   emu-state and copies the headers and libraries into
   `Source/ThirdParty/Zxv/` (Linux and macOS only: libzxv has no Windows
   build in this repository yet, and `ZxvUnreal.Build.cs` refuses other
   platforms rather than link something untested).
2. Copy (or symlink) `sdk/engines/unreal-zxv` into your project's
   `Plugins/ZxvUnreal/` and regenerate project files.
3. Add `"ZxvUnreal"` to your module's `PublicDependencyModuleNames` to use
   the C++ API.

`libzxv.so` / `libzxv.dylib` is linked as a shared library and listed as a
runtime dependency next to the module binary; emu-state is linked
statically.

## Quickstart (C++)

```cpp
#include "ZxvSubsystem.h"

void AMyGameMode::BeginPlay()
{
	Super::BeginPlay();
	UZxvSubsystem* Zxv = GetGameInstance()->GetSubsystem<UZxvSubsystem>();
	FZxvLockstepConfig Cfg;          // 2 players, 1 rational slot, 1 input byte
	EZxvResult Result;
	UZxvLockstep* Sim = Zxv->CreateLockstep(TEXT("Match"), Cfg, Result);
	FZxvRational Zero, Step;
	UZxvLibrary::MakeRational(0, 1, Zero);
	Sim->SetRational(0, Zero);
	Sim->Start();                                        // tick 0
	UZxvLibrary::MakeRational(1, 60, Step);
	Sim->SetStep(Step, 4);                               // 60 Hz, catch up at most 4
	Sim->bAutoPace = true;                               // the subsystem paces it
	Zxv->OnTicksDue.AddDynamic(this, &AMyGameMode::OnTicksDue);
}

// The game rules for one tick: a pure function of state and inputs.
static int32_t StepRules(zxvue::Lockstep& S, uint64_t /*Tick*/)
{
	std::vector<uint8_t> In;
	if (int32_t Rc = S.input(0, &In, nullptr); Rc != ZXV_OK) return Rc;
	zxvue::Rational Third;
	zxvue::Rational::from_ints(1, 3, &Third);
	return In[0] == 1 ? S.add_rational(0, Third) : ZXV_OK;   // x += 1/3 exactly
}

void AMyGameMode::OnTicksDue(FName Name, UZxvLockstep* Sim, int32 Due, int64 Dropped)
{
	for (int32 I = 0; I < Due; ++I)
	{
		// submit every player's input for Sim->CurrentTick() + 1 first
		if (Sim->Core().advance(StepRules) != ZXV_OK) break;
	}
}
```

In Blueprint, the same calls are under **ZXV**: *Create Lockstep*, *Make
Rational*, *Advance* (bind a step event that returns *Ok* to commit), *State
Hash*, *Compare Hash*, and *To Float Lossy* for the actor's location.

## Concepts in Unreal terms

| Concept | Unreal form |
|---|---|
| Exact numbers | `MakeRational(3, 4)`, `ParseRational("12.75")`. There is no node that takes a float; `FZxvRational(0.5)` does not compile. A value typed into the editor (`Num`, `Den`) is checked when used: `1/0` is `InvalidArgument`, `2/4` reduces to `1/2`. |
| Drawing | `ToFloatLossy(R)` is the nearest double, for a transform or a widget. It is lossy; never store it in game state or send it to a peer. `RationalToScaledInt(R, 100)` gives exact integer units (cm from m) when you can draw from integers. |
| Failures | Functions that can fail return `EZxvResult` (`Ok`, `OutOfRange`, `WrongState`, `OverBudget`, ...) and write outputs only on `Ok`; `UZxvLibrary::LastError()` explains it. Nothing throws. |
| Time | `FeedMicroseconds(int64)` or `PumpClock()` (monotonic, integer microseconds). The subsystem ignores the float `DeltaTime` on purpose. `ClockAlpha` gives the exact fraction of a tick for interpolation. |
| Ticks | `Advance(Step)` runs the step between begin and commit; a result other than `Ok` aborts the tick and restores the state. `BeginTick` / `CommitTick` / `AbortTick` are there if you drive it yourself. |
| Inputs | `SubmitInput(Player, Tick, Bytes)`; inside a step, `GetInput(Player, Bytes, bPredicted)`. |
| Rollback | `FZxvLockstepConfig.bPredict = true`; when `HasPendingRollback`, call `RollbackReplay(Step)`. |
| Desync | `CompareHash(Tick, PeerHash, Verdict)` gives `EZxvTruth::True`, `False` (recorded, see `HasDesync`) or `Neither` (no verdict yet). |
| Budget | `SetTickBudget(Limit)`, then `Charge(Amount)` inside a step returns `OverBudget` past the limit. The sim owns a private libzxv context and a capability narrowed to BUDGET only. |
| Four-valued logic | `HexagramFromTruth(EZxvTruth::Both, Polarity, Phase)`, `HexagramToTruth`; there is no conversion to `bool`. Base TRUE is 3, Truth TRUE is 1: always convert, never cast. |

## Building on this plugin

ZxvUnreal is the general engine plugin. Other plugins layer on top of it
rather than fork it; the planned **Vino Embed** checkout plugin is one.
For that:

- `ZxvUnreal` is a `Runtime` module whose public headers (including
  `ZxvCore/zxv_ue_core.hpp` and the libzxv / emu-state C headers) are
  exported; a dependent plugin adds `"ZxvUnreal"` to
  `PublicDependencyModuleNames` and lists it under `"Plugins"` in its own
  `.uplugin`.
- `UZxvSubsystem` is not final. Its hooks (`GetLockstepClass`,
  `HandleSimCreated`, `HandleSimDestroyed`, `PaceSim`) are virtual, and
  `OnSimCreated` / `OnSimDestroyed` are C++ multicast delegates. The usual
  pattern is a separate subsystem in the dependent plugin that gets this
  one with `GetGameInstance()->GetSubsystem<UZxvSubsystem>()`.
- `UZxvLockstep::Core()` gives the engine-neutral `zxvue::Lockstep` for C++
  steps.
- This kit has **no payment, checkout or voucher features** and will not
  get them: those belong in the layer above, which keeps this plugin
  usable by any game.

## Tests

| What | Where | Result here |
|---|---|---|
| The C++ layer: parse/format tables, arithmetic identities, overflow and `INT64_MIN` refused with the output untouched, floor/ceil/scaling/Q16, all 64 hexagram tags (and/or against all 64 partners, from libzxv), every call on an empty sim refused, the emu-state shape limits, **two peers** (one predicting late inputs and rolling back, one strict) plus a third receiving inputs out of order agreeing hash for hash, replay determinism, **overflow inside a tick** refused with state and hash unchanged, budgets, desync, manual rollback, a late joiner by state transfer, moves, the clock and the pacer (600 jittery 60 Hz frames are exactly 600 ticks; time going backwards is refused); built with `-fno-exceptions` | `test_core` | 3,653 checks pass with gcc 13.3 and clang 18.1; also under ASan + UBSan (gcc 13.3) |
| The layer against the header-only C++ SDK (`sdk/cpp/include/zxv.hpp`): 400 generated pairs (fixed-seed xorshift) including values near the int64 edge (add, sub, mul, div, compare, text; the same refusals), parsing, Q16, all 64 hexagram tags and their and/or tables | `test_cross_cpp` | 3,385 checks pass with gcc 13.3 and clang 18.1; also under ASan + UBSan (gcc 13.3) |
| No `float` / `double` in the layer or its tests | `check_nofloat_core` (`sdk/kits/z-wasm/tools/check_nofloat.py`) | 4 files, no float types |
| Structure of the Unreal wrapper | `check_ue_layout` | 18 files, 74 `UFUNCTION`s, each with a definition; not a compile |
| The plugin inside Unreal | none | **not compiled or run**: no Unreal Engine in CI |

CI ([`.github/workflows/sdk-engines.yml`](../../../.github/workflows/sdk-engines.yml),
job `unreal-core`) builds and runs all of the above on Ubuntu 24.04 with
gcc and on macOS 14 with clang, plus the ASan + UBSan build on Linux, and
checks that `tools/stage_thirdparty.sh` stages the native libraries.

## Honest limits

- **Not yet compiled inside Unreal** (see the top of this file). The
  wrapper is plausible UE 5.4 code and passes a structural check, nothing
  more. Windows, consoles, Android and iOS are not supported:
  libzxv has no build for them in this repository yet.
- Lockstep only works when **every peer runs the same build** (the same
  step code, the same plugin and the same libzxv) and gets **the same
  inputs**. Hash comparison detects a divergence; it cannot repair one
  (resync with `ExportState` / `ImportState`).
- **No anti-cheat.** Hashes show that two peers disagree, not which one is
  honest. Every peer has the full game state, so a modified client can read
  it and can send any input it likes. Nothing here makes a game
  "unhackable".
- **The hash is not a signature.** It is SHA3-256 for integrity between
  cooperating peers; sign the hashes yourself if peers must not be able to
  deny them.
- **Floats only for rendering.** `ToFloatLossy` exists for drawing. The
  step you write must itself be deterministic: Unreal's float math on game
  state, `FMath::Rand`, world time inside the step, physics results and
  `TMap` iteration order you did not control all break lockstep, and the
  plugin cannot see them.
- Exact rationals cost more than floats and denominators grow
  (`1/2 + 1/3 + 1/5 + ... + 1/47` fits, adding `1/53` is `OutOfRange`;
  the tests show it). Keep quantities on a common grid and expect
  `OutOfRange` rather than a silent error.
- `UZxvLockstep` is not thread-safe; use it from the game thread.
