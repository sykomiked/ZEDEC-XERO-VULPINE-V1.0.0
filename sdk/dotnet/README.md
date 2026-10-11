<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# ZXV .NET SDK

The .NET 8 SDK for the ZXV platform core. It wraps `libzxv`, the one C
library every ZXV SDK shares (`sdk/libzxv/include/zxv_api.h`), and covers
every group of ABI 1.4:

- **finance**: exact-minor-unit ledger, ISO 4217, ISO 20022 (pacs.008,
  camt.053), payments, statements, netting, cards, conformance;
- **exactness and logic**: `Rational`, four-valued `Truth`, attestations, the
  64-state `Hexagram` tag and its `QuarantineLog`;
- **least privilege**: `Capability` on every gated call, fail-closed contexts;
- **economy**: rational budgets with async waiting, phase ticks, the swarm
  token economy;
- **security**: SHA3, SHAKE256, Keccak, Ed25519, ML-DSA-87, ML-KEM-1024 with
  off-heap secret keys;
- **engine**: deterministic integer GGUF inference, sync, async and streamed;
- **wire and chain**: UBH-168 frames in pure managed code, EVM state channels;
- **hardware**: ZHP-1 machine profiles, the emulation matrix with the
  standard engines, and signed hardware updates for reconfigurable regions.

The finance groups (`ZxvPlatform` and its services) are described in depth in
[`docs/DOTNET_SDK.md`](../../docs/DOTNET_SDK.md). This README is the map of
the whole SDK.

## Install

Requirements: the .NET 8 SDK and `libzxv` built for your platform.

```sh
make -C sdk/libzxv                      # once: builds sdk/libzxv/build/libzxv.so
cd sdk/dotnet
dotnet build Zxv.sln -c Release -p:ZxvSkipNativeBuild=true
dotnet test  Zxv.sln -c Release --no-build
dotnet run --project samples/Zxv.Samples.Agent
```

Reference `src/Zxv.Sdk/Zxv.Sdk.csproj` from your project (the NuGet package
`Zxv.Sdk` is produced by `dotnet pack`; it carries the native library under
`runtimes/<rid>/native/`). `Directory.Build.props` points `ZxvNativeDir` at
`../libzxv/`; the build copies `libzxv` next to every output.

The library is found in this order: `ZxvLibrary.UseLibraryPath(path)`, the
`ZXV_LIBRARY_PATH` environment variable (or the older `ZXV_NATIVE_LIBRARY`),
the copy next to the application, then the system loader path
(`LD_LIBRARY_PATH`, `DYLD_LIBRARY_PATH`, `PATH`). A library whose ABI is not
1.4 or a later 1.x is refused on first use (the SDK calls the `zxv_hw_*`
functions added in 1.4, the `zxv_hex_*` functions added in 1.3 and the
`_cap` functions added in 1.2).

## Quick start

```csharp
using Zxv;
using Zxv.Engine;

using ZxvPlatform zxv = ZxvPlatform.Create();
using Capability root = zxv.CreateRootCapability();
using Capability ai = root.Attenuate(CapabilityRights.AiInference | CapabilityRights.Budget);
Console.WriteLine(Rational.Parse("0.1") + Rational.Parse("0.2"));          // 0.3, exactly
using Model model = Model.Open(ai, "tiny-qwen2.gguf", "tiny-tokenizer.gguf");
using Session chat = model.NewSession();
await foreach (GeneratedToken t in chat.StreamAsync("Hello", new GenerateOptions { MaxTokens = 16 }))
    Console.Write(t.Text);
```

## Concepts, in .NET terms

**One platform, one context.** `ZxvPlatform` owns one native context (its
ledger and configuration). It is thread-safe and `IDisposable`; register it as
a singleton with `services.AddZxv(...)`. Every ABI 1.1 object is created from
a platform and keeps the native context alive until it is itself released,
so disposal order never causes a use-after-free.

**Capabilities on every call.** `platform.CreateRootCapability()` returns a
`Capability`; `Attenuate(rights)` returns a narrower child, never a wider
one. Objects that make gated calls (`Budgets`, `PhaseClock`, `Swarm`,
`Attestations`, `Model`, `Ed25519`, `MlDsa87`, `MlKem1024`, `StateChannel`,
`LedgerService.WithCapability(cap)`) hold one and present it on every call;
the C library checks it each time (live, not revoked, right present,
attestation `True`). There is no ambient "current capability". With DI,
`services.AddZxvCapability(rights)` registers one narrow singleton.

Since ABI 1.2 the ledger and currency surface take a capability too:

| Call | Needs |
|---|---|
| `Currencies.Enable(cap, code[, minorUnits])`, `Currencies.RegisterPrivateUnit(cap, code, minorUnits)` | `LedgerPost` |
| `Ledger.WithCapability(cap).OpenAccount(...)`, `.Post`, `.PostMany` | `LedgerPost` |
| `Ledger.WithCapability(cap).GetAccount`, `.ListAccounts`, `.GetEntryCount`, `.GetEntries`, `.GetBalances`, `.GetAvailable`, `.Verify` | `LedgerRead` |
| `Messages.BuildPacs008(cap, msg)`, `Messages.BuildCamt053(cap, msg)` (re-checked on every field) | `LedgerRead` |
| `CreateBudgets`, `CreatePhaseClock`, `CreateAttestations`, `CreateSwarm` | `Budget`, `Phase`, `Attest`, `Swarm` |
| `CreateHwMatrix` and every `HwMatrix` call; `HwMatrix.TrustKey`, `ApplyContract`, `CreateHwRegion` and every `HwRegion` call; `HwPackageSigner` | `HwMatrix`; `HwReconf`; `CryptoSign` |

A capability of another platform is refused with `ArgumentException`. The
1.0 forms without a capability (`Currencies.Enable(code)`,
`Ledger.OpenAccount`, `Ledger.GetAccount`, `Ledger.ListAccounts`,
`Ledger.GetEntryCount`, `Ledger.GetEntries`, `Messages.BuildPacs008(msg)`,
`Messages.BuildCamt053(msg)`) keep working and are `[Obsolete]`.

The members that take no capability (those obsolete forms, `ZxvOptions`
currency configuration, `Ledger.Post`, `Ledger.GetBalances`,
`Ledger.Verify`, `Payments.SubmitAsync(request)`,
`Statements.BuildStatement(account)`, `Messages.IsSupported`) present the
platform's own capability (`LedgerRead | LedgerPost`, created with the
platform and never exposed), so the SDK calls only the ABI 1.2 `*_cap`
forms; a test scans the sources for the deprecated ones. Code that should
hold less passes its own:

| Call | Needs |
|---|---|
| `Payments.SubmitAsync(cap, request)` | `LedgerRead` and `LedgerPost` |
| `Statements.BuildStatement(cap, account[, request])` | `LedgerRead` |

**Disposal.** `ZxvPlatform.Dispose()` makes the platform disposed at once:
its members, its services and `CapabilityLedger` views throw
`ObjectDisposedException`. Capabilities, budgets, clocks and logs created
from it are children with their own lifetime: they keep working until you
dispose them, and the native context is destroyed after the last of them
(ABI.md R6).
The same holds one level down. A disposed `Capability`, `Model` or
`Budgets` throws at once, even while something still holds it, and so does
anything that acts with its authority (a `CapabilityLedger` view, a `Budget`,
a model opened with that capability). Its own handles keep working: an
attenuated child capability, and a session or LoRA adapter of a disposed
model. The native object is freed after the last of them.

**No floats at the boundary.** No public method takes or returns `double` or
`float`. `Rational` is a `readonly record struct` built with `Rational.Of(3, 4)`,
`Rational.Parse("12.75")` or `Rational.FromDecimal(12.75m)` (exact). A
`double` enters only through `Rational.FromDoubleExact(x)`, which returns the
value the double really holds (`0.1` becomes
`3602879701896397/36028797018963968`); NaN and infinity raise
`FloatRejectedException`. There is an implicit conversion from `long`, none
from floating point. Engine sampling settings are `Rational`s sent as Q16.16.

**Four-valued logic.** `Truth` is an enum with the kernel's numbering
(`Neither = 0`, `True = 1`, `False = 2`, `Both = 3`) and no conversion to
`bool`. Decide with an explicit policy: `IsCertainlyTrue()`,
`ToBoolean(onBoth, onNeither)` or `RequireTrue()`. `And`, `Or`, `Not` follow
the kernel tables. `Both` is a held contradiction, not an error.

**Errors by class.** Every native failure becomes an exception chosen by
`zxv_status_class`:

| Class | .NET type | You should |
|---|---|---|
| USAGE | `ZxvUsageException` (`FloatRejectedException`) | fix the call |
| REFUSED | `ZxvRefusedException`: `InsufficientFundsException`, `CapabilityDeniedException`, `BudgetExhaustedException`, `VerificationFailedException`, `ZxvFeatureNotAvailableException` | handle it; retry after the situation changes |
| SYSTEM | `ZxvSystemException` | check the environment |
| FATAL | `ZxvFatalException`, which does **not** derive from `ZxvException` | stop using the platform; never retry |

All of them carry `Status`, `StatusName` and `Detail` (diagnostic text from
`zxv_last_error`). Arguments caught in managed code raise the standard
`ArgumentException` family; `Verify` methods return `false` instead of
throwing `VerificationFailedException`.

**Fail closed.** When an invariant breaks, or an operator calls
`platform.Halt(reason)`, the C context is poisoned: every later call fails
with `ZXV_E_POISONED`, whatever the host does. The SDK raises
`ZxvFatalException`, marks the context dead so later calls fail before
crossing into C, and, if you set `ZxvLibrary.FatalPolicy =
FatalPolicy.FailFast`, ends the process with `Environment.FailFast` (the only
uncatchable form in .NET).

**Secrets off the heap.** `SecretKey` holds key material in memory from
`NativeMemory.AllocZeroed`, wiped with `zxv_secure_zero` on `Dispose` and by a
critical finalizer as a backstop. Keys the SDK generates never exist as a
managed array. `ExportBytesUnsafe()` is the one, explicitly named, way out.

**Async where something waits.** `Budget.ConsumeAsync(amount, ct)` waits
while the budget is exhausted and retries when a `Release` or larger limit
replenishes it (it fails at once on a malformed amount). `PhaseClock` is
driven manually (`Advance`) or in real time (`StartRealtime`, a
`PeriodicTimer` loop); `DeferredEvent.WaitClearedAsync()` completes when the
clock passes its tick. `Session.GenerateAsync` runs on the thread pool and
`Session.StreamAsync` is an `IAsyncEnumerable<GeneratedToken>` fed by the
native token callback (an `UnmanagedCallersOnly` function pointer plus a
`GCHandle`); breaking out of the loop stops generation.

## The hexagram tag (ABI 1.3)

A `Hexagram` tags one unit of work with a four-valued base, the polarity
of its evidence (entropic, neutral, forbidden, negentropic) and its phase
(potential -> emerging -> manifest -> dissolving), packed into one byte
0..63. Its action is proceed, wait, refuse, escalate or rollback. Every rule
comes from libzxv and is tested against `vectors.json` key `hex`.
Forbidden is absorbing, a phase moves one step at a time, and the base is
**not** numbered like `Truth` (TRUE is base 3 but Truth 1): convert only
with the explicit `ToTruth()` / `Hexagram.FromTruth(...)`. Combining BOTH with NEITHER follows
the `Truth` tables (AND gives NEITHER, OR gives BOTH); those two results
are pending an owner decision. A `QuarantineLog` (needs the ATTEST
capability) records forbidden work in a SHA-256 hash chain; when it is
full, append is refused with `ZXV_E_CAPACITY` and you must halt the
context. See [ABI.md, Changes in 1.3](../docs/ABI.md#changes-in-13).

```csharp
var h = new Hexagram(HexBase.True, HexPolarity.Negentropic, HexPhase.Potential);
// h.Action == HexAction.Proceed
h = h.Advance();                          // Emerging
h.Step(HexPhase.Dissolving);              // ZxvRefusedException (ZXV_E_STATE): a jump
var bad = h.Forbid();                     // Rollback, forever
using var log = platform.CreateQuarantineLog(cap, 1024);   // needs Attest
ulong seq = log.Append(bad, reason: 7, submitter: 42, digest32);
log.Verify();
```

`Hexagram` is a `readonly struct` (`Zxv.Sdk.Exact`) with `&`, `|` and `~`;
`default(Hexagram)` is tag 0. `QuarantineLog` is in `Zxv.Sdk.Security`.

## Hardware emulation and hardware updates (ABI 1.4)

Namespace `Zxv.Hardware` wraps the 55 `zxv_hw_*` functions
([HW_EMULATION.md](../docs/HW_EMULATION.md), [HW_PROFILE.md](../docs/HW_PROFILE.md)).
Everything is integer.

- **Profiles** (`HwProfile`, plain data, no capability): `ProbeHost()`, the
  reference machine R1 (`Reference()`, 16 nodes), `Present(host, r1)`,
  `Match`/`Pair`, `Encode`/`Decode`/`Digest`, nodes, capabilities
  (`HwCapKeys`, `HwClasses`) and the opaque per-node extension.
- **Matrix** (`platform.CreateHwMatrix(cap)`, needs the new
  `CapabilityRights.HwMatrix`): every matrix starts with the standard
  engines (`HwFunctions.Sha256`, `Keccak`, `Mac`, `Gcd`, `Logic64`,
  `Tensor`), each served by a proven SOFT implementation. Discovery
  (`GetProviders`, `GetFunctionInfo`, `Bound`), placement (`SetPolicy`,
  `SetTraits`; key-bearing work never goes REMOTE), `Call`/`Shadow`/`Memo`,
  versioned contracts, and your own implementations as `HwImplementation`
  delegates (`AddProvider`, proven by `Attest`).
- **Signed packages** (`HwPackageSigner`, needs `CryptoSign`): release keys
  (`GenerateKey()` returns an `HwReleaseKey` whose two secret keys are
  off-heap `SecretKey`s, wiped on dispose) and images or contract packages
  signed with ML-DSA-87 **and** SLH-DSA-SHAKE-256s. SLH-DSA signing takes
  about two seconds: it is a release step.
- **Regions** (`platform.CreateHwRegion(...)`, needs the new
  `CapabilityRights.HwReconf`): `Stage`, `Trial`, `Commit`, `Rollback`,
  `Tick`, `GetInfo`; every step raises `StepRecorded` and appends a record
  to a hash-chained audit log (`GetLog`, `GetLogEntry`, also on the matrix
  for contract changes). The device is an `IHwRegionDevice` (load, read
  back, activate).

```csharp
using Capability cap = platform.CreateRootCapability(CapabilityRights.AllAbi14);
using HwMatrix mx = platform.CreateHwMatrix(cap);
byte[] d = mx.Call(HwFunctions.Sha256, 0, "abc"u8);             // ba7816bf...
uint fabric = mx.AddProvider(fnId, resId, HwKind.Fabric, 1, (op, input, output) => device.Run(input, output));
var signer = new HwPackageSigner(cap);
using HwReleaseKey key = signer.GenerateKey();
using HwRegion rg = platform.CreateHwRegion(cap, mx, fabric, 1, 0xD0, default, device, 64);
rg.TrustKey(key.PublicKey.Span);
rg.StepRecorded += (_, e) => Console.WriteLine($"{e.Kind} v{e.Version}");
rg.Stage(signer.BuildImage(HwImageKind.PartialBitstream, 0xD0, default, fnId, 1, 1, bitstream, key));
rg.Trial(5);                                                    // proven against the golden vectors
rg.Commit();                                                    // permanent; floor raised
```

Callbacks are `UnmanagedCallersOnly` function pointers whose `user`
argument is a `GCHandle` owned by the matrix or region handle and freed only
after the native object is destroyed. They run on the calling thread inside
the native call. An exception never unwinds into native code: a
`ZxvException` thrown by an implementation or device becomes its status (so
a device can report `ZxvStatus.IOError`), anything else is rethrown from the
call that ran it, and a `StepRecorded` handler's exception is traced and
dropped. New statuses (`NoProvider` -25 through `PolicyRefused` -31) map by
class like every other: `WrongTarget` is USAGE, the rest REFUSED.
`CapabilityRights.All` keeps its ABI 1.1 value (as `ZXV_CAP_ALL` does), so
`CreateRootCapability()` without arguments holds no hardware right; pass
`CapabilityRights.AllAbi14`.

## API overview

| Area | Types | Key members |
|---|---|---|
| Library | `ZxvLibrary` | `AbiVersion`, `Features`, `Supports`, `ClassOf`, `GetStatusName`, `FatalPolicy`, `UseLibraryPath` |
| Platform | `ZxvPlatform` | `Create`, `CreateRootCapability`, `CreateAttestations`, `CreateBudgets`, `CreatePhaseClock`, `CreateSwarm`, `CreateHwMatrix`, `CreateHwRegion`, `Halt`, `IsHalted` |
| Finance | `LedgerService`, `CurrencyService`, `MessageService`, `PaymentService`, `StatementService`, `NettingService`, `CardService`, `ConformanceService`, `Money` | see `docs/DOTNET_SDK.md` |
| Gated ledger | `CapabilityLedger` | `Post`, `GetBalances`, `Verify` |
| Capabilities | `Capability`, `CapabilityRights`, `CapabilityInfo` | `Attenuate`, `Revoke`, `SetAttestation`, `Allows`, `Demand` |
| Exact numbers | `Rational` | `Of`, `Parse`, `TryParse`, `FromDecimal`, `FromDoubleExact`, `ToQ16`, operators |
| Logic | `Truth`, `TruthExtensions` | `And`, `Or`, `Not`, `FromEvidence`, `ToBoolean`, `RequireTrue` |
| Attestations | `Attestations`, `AttestationSubject`, `AttestationState` | `Attest`, `AddEvidence`, `Find`, `GetState`, `IsGateClear` |
| Budgets | `Budgets`, `Budget`, `BudgetStatus`, `BudgetResource` | `Create`, `Consume`, `TryConsume`, `ConsumeAsync`, `Release`, `SetLimit`, `GetStatus`, events `Exhausted`/`Replenished`/`InvalidAmount` |
| Phase | `PhaseClock`, `DeferredEvent`, `PhaseAdmission` | `Now`, `Advance`, `Admit`, `SetHealth`, `Defer`, `NextDue`, `StartRealtime`, `WaitClearedAsync`, `Cancel` |
| Swarm | `Swarm`, `SwarmAllotment`, `SwarmGrant`, `EmotionProfile` | `Register`, `SetMood`, `SetFeeling`, `BeginCycle`, `Consume`, `GetAllotment`, `EndCycle`, `LevelCapacity` |
| Wire | `Zxv.Wire.Ubh168Frame`, `WireTruth` | `MakeTag`, `Pack`, `WriteTo`, `Unpack`, `Idle`, `Resolve` |
| Hashes | `Hashes`, `SecureMemory` | `Sha3256`, `Shake256`, `Keccak256`, `FillRandom`, `Zero` |
| Crypto | `Ed25519`, `MlDsa87`, `MlKem1024`, `SecretKey`, `KeyPair`, `KemEncapsulation` | `GenerateKey`, `Sign`, `SignDeterministic`, `Verify`, `Encapsulate`, `Decapsulate` |
| Engine | `Zxv.Engine.Model`, `Session`, `GenerateOptions`, `GenerationResult`, `GeneratedToken` | `Open`, `OpenMemory`, `Tokenize`, `Detokenize`, `NewSession`, `Generate`, `GenerateAsync`, `StreamAsync`, `BindBudget`, `LoadLora` |
| EVM | `Zxv.Evm.StateChannel`, `EvmState` | `EncodeState`, `LedgerState` |
| Hardware | `Zxv.Hardware.HwProfile`, `HwMatrix`, `HwRegion`, `HwPackageSigner`, `HwReleaseKey`, `HwVector`, `HwFunctions`, `HwClasses`, `HwCapKeys` | `ProbeHost`, `Reference`, `Present`, `Match`, `Call`, `Shadow`, `Memo`, `AddProvider`, `Attest`, `GetProviders`, `SetPolicy`, `ApplyContract`, `Stage`, `Trial`, `Commit`, `Rollback`, `GetLog`, event `StepRecorded` |
| DI | `ZxvServiceCollectionExtensions` | `AddZxv`, `AddZxvCapability` |
| Native | `Zxv.Interop` | `ZxvStatus`, `ZxvStatusClass`, `ZxvFeatures`, SafeHandles (internal P/Invoke) |

## Examples

Budget a component and wait for replenishment:

```csharp
using Budgets budgets = zxv.CreateBudgets(root.Attenuate(CapabilityRights.Budget));
Budget gpu = budgets.Create("gpu-seconds", BudgetResource.ComputeCycles, Rational.Parse("2.5"));
budgets.Exhausted += (_, e) => logger.LogWarning("{Budget} exhausted", e.Budget.Name);
gpu.Consume(Rational.Of(5, 2));
Task next = gpu.ConsumeAsync(1, TimeSpan.FromSeconds(30), ct);   // waits...
gpu.Release(1);                                                  // ...until this
await next;
```

Defer work to the next harmonic fundamental:

```csharp
using PhaseClock clock = zxv.CreatePhaseClock(root.Attenuate(CapabilityRights.Phase));
DeferredEvent ev = clock.Defer(clock.Now + PhaseClock.TicksPerCycle);
await using (clock.StartRealtime(ticksPerSecond: PhaseClock.TicksPerCycle))
    await ev.WaitClearedAsync(ct);                               // about one second
```

Sign an agent message with ML-DSA-87 (the secret key never touches the GC heap):

```csharp
var dsa = new MlDsa87(root.Attenuate(CapabilityRights.CryptoSign));
using KeyPair keys = dsa.GenerateKey();
byte[] sig = dsa.Sign(keys.SecretKey, message, context: "zxv-agent"u8);
bool ok = dsa.Verify(keys.PublicKey, message, sig, "zxv-agent"u8);
```

Deterministic, budgeted generation:

```csharp
using Session s = model.NewSession(contextLength: 2048);
s.BindBudget(budgets.Create("tokens", BudgetResource.Tokens, 512));
GenerationResult r = s.Generate(prompt, new GenerateOptions
{
    MaxTokens = 128, Temperature = Rational.Parse("0.7"), TopK = 40,
    TopP = Rational.Parse("0.95"), RepeatPenalty = Rational.Parse("1.1"), RepeatLastN = 64, Seed = 42,
});
Console.WriteLine($"{r.Text} ({r.StopReason})");                // same tokens on every machine
```

Frame without the native library, then check it:

```csharp
var frame = new Ubh168Frame(Ubh168Frame.MakeTag(trunk: 3, shell: 2, sync: true), 0x01020304, 0x05060708, 0x090A0B0C, 0x11121314, 0x15161718);
byte[] octets = frame.Pack();                                    // 7b0403020111121314...
WireTruth t = Ubh168Frame.Unpack(octets).Resolve();              // native W5 resolver
```

Runnable programs: `samples/Zxv.Samples.Agent` (every ABI 1.1 group in one
agent turn; `--fatal-failfast` demonstrates `FatalPolicy.FailFast`),
`samples/Zxv.Samples.Console` (the finance groups) and `samples/Zxv.Gateway`
(an operator REST gateway).

## Tests

`tests/Zxv.Sdk.Tests` runs against the real library and checks
`sdk/conformance/vectors.json`: status names and classes, rational
arithmetic, parsing and formatting, the IEEE-754 boundary, Q16 rounding, the
four-valued tables, UBH-168 frames byte for byte (managed and native) and
idle pilots, SHA3/SHAKE/Keccak digests, Ed25519, ML-DSA-87 and ML-KEM-1024
digests, swarm allotments, greedy and seeded generation tokens, and the EVM
encoding and digest; plus capability denial and revocation, the fatal path
through `Halt` (and `FailFast` in a child process), budget exhaustion and
async waiting, and phase deferral with both drivers. `Abi14/HwTests` covers
ABI 1.4: R1, the standard engines (SHA-256 of "abc", HMAC RFC 4231),
key-bearing MAC refusing a REMOTE implementation, power and latency
placement, managed implementations with failover, nested calls, shadow
disputes and the memo, and a signed update staged, tried and committed on a
simulated two-slot device with its events, audit log and a contract package
(three SLH-DSA signatures, about five seconds).
async waiting, phase deferral with both drivers, platform disposal while
children are alive, and the platform services on the `*_cap` forms.

## Unity

Unity's scripting runtime targets .NET Standard 2.1 / .NET Framework 4.x
(Mono and IL2CPP), not .NET 8, so these assemblies do not load there as
built. A Unity package would need:

- a `netstandard2.1` build of the managed code: replace `LibraryImport`
  (source-generated, .NET 7+) with `DllImport` and blittable signatures;
  `UnmanagedCallersOnly` callbacks with `[MonoPInvokeCallback]` static
  delegates (required by IL2CPP); `NativeMemory` with `Marshal.AllocHGlobal`;
  `ObjectDisposedException.ThrowIf`, `ArgumentOutOfRangeException.ThrowIf*`,
  `PeriodicTimer`, `Task.WaitAsync`, `Rune` and `Convert.ToHexString` with
  small polyfills; no module initializer (set the resolver from a
  `[RuntimeInitializeOnLoadMethod]` instead, or rely on Unity's plug-in
  loader);
- `libzxv` as a native plug-in per platform (`Assets/Plugins/x86_64/libzxv.so`,
  `libzxv.dylib`, `zxv.dll`; iOS needs the static `libzxv.a` and
  `__Internal` as the library name);
- `PhaseClock` driven from `Update()` (manual `Advance`) rather than a
  thread-pool timer, and generation run off the main thread.

None of this is built yet.

## Honest limits

- **Least privilege, not a sandbox.** Capabilities are checked on every call,
  but code in the same process can create its own root capability. They
  become enforcement only at a process boundary.
- **Secrets.** `SecretKey` cannot protect bytes you imported from a managed
  array (the GC may already have copied them; wipe your copy), memory paged
  to swap, or a core dump. `ExportBytesUnsafe()` defeats the wiping.
- **Not FIPS 140-3 validated.** ML-DSA-87, ML-KEM-1024 and Ed25519 follow FIPS
  204, FIPS 203 and RFC 8032 and pass the vectors here, but no module is
  validated. ZXV has no secp256k1: EVM parties sign digests in their own wallets.
- **Fatal is catchable.** .NET cannot make an exception uncatchable;
  `ZxvFatalException` is only kept out of the `ZxvException` family. Use
  `FatalPolicy.FailFast` when the process must stop.
- **Finalizer timing.** Undisposed keys and handles are released by
  finalizers at the garbage collector's convenience. Dispose them.
- **LoRA is not built.** `Model.LoadLora` always raises
  `ZxvFeatureNotAvailableException` while `ZxvFeatures.Lora` is off.
- **Pending finance modules.** Netting, card lifecycle, VSS checks and the
  pacs.002/004/009, camt.056/029 messages raise
  `ZxvFeatureNotAvailableException` until the kernel modules land.
- **In-memory, no network.** The ledger lives in process memory; there is no
  persistence and no PAPSS/SWIFT/RTGS connectivity.
- **Platforms.** Built and tested on Linux x64 with .NET 8. macOS should work
  with a `libzxv.dylib`; Windows needs a `zxv.dll` build that does not exist
  yet. Unity is not supported (see above).
