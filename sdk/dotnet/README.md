<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# ZXV .NET SDK

The .NET 8 SDK for the ZXV platform core. It wraps `libzxv`, the one C
library every ZXV SDK shares (`sdk/libzxv/include/zxv_api.h`), and covers
every group of ABI 1.5:

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
  standard engines, and signed hardware updates for reconfigurable regions;
- **ABI 1.5**: treaties, CIDs and AIPI nodes, coins and token wallets, the
  assurance fee, the V-Bill hub, freight, Web4 agents and paradox handling.

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
1.5 or a later 1.x is refused on first use (the SDK calls the ABI 1.5
groups, the `zxv_hw_*` functions added in 1.4, the `zxv_hex_*` functions
added in 1.3 and the `_cap` functions added in 1.2).

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

## ABI 1.5 groups

ABI 1.5 adds eight groups, each in its own namespace, reported by
`ZxvLibrary.Features2` (`zxv_features2`). New statuses `Denomination` (-32)
and `TreatyInvalid` (-33) are USAGE; -34 through -40 (`CidMismatch`,
`ConsentRequired`, `ErasureTooFewRows`, `PegInvalid`, `LimitExceeded`,
`ParadoxStepLimit`, `ParadoxOwnerApproval`) are REFUSED; each maps to an exception by class as before.
Variable-length results use the C two-call pattern; the SDK hides it (it
asks for the size, allocates, and asks again if the result grew). Fixed-size
records (`Cid`, `PegMessage`, `FreightHeader`, `ConsentToken`, `RepairProof`,
`ParadoxDraft`) are value types. Handles keep their context alive (R6) and
handles whose native object is not thread-safe are serialized by the SDK.
New rights: `CapabilityRights.Aipi`, `Mint`, `Paradox`; pass
`CapabilityRights.AllAbi15` (`All` keeps its ABI 1.1 value). Integers only.

### Treaties (`Zxv.Treaties`)

`Treaty` parses, validates, emits and runs a sutra or a 2400-byte record of
parties and obligations; `Run()` returns a `TreatyResult` (parties, legs,
fees). No capability. Limits: a batch description, not a contract in law;
one asset per treaty; dividend shares are reported, nothing is paid.

```csharp
using Treaty t = Treaty.ParseSutra(File.ReadAllText("batch.sutra"));
if (t.Validate() != TreatyFault.None) return;
using TreatyResult r = t.Run();
foreach (TreatyLeg leg in r.Legs) Console.WriteLine(leg);
```

### CIDs and AIPI (`Zxv.Aipi`)

`Cid` (value type): `Compute`, `ForFile`, `Parse`/`TryParse`, `ToV1`,
`ToString(CidBase)`, `ProviderKey`. `AipiNode`
(`platform.CreateAipiNode(cap, storeBytes, maxBlocks, nodeKey)`, needs
`Aipi`): content-addressed `Put`/`Get`, private blocks sealed with the node
key (wiped on dispose), records, and `Plan` for a function call's route.
Limits: no network, no IPFS peers, no EXECUTE (Z-Wasm is not in libzxv); a
CID names bytes, it does not make them available or prove outputs right.

```csharp
using AipiNode node = platform.CreateAipiNode(cap, 1 << 20, 64);
Cid id = node.Put("hello"u8);
Console.WriteLine($"{id} -> {Encoding.UTF8.GetString(node.Get(id))}");
```

### Coins and tokens (`Zxv.Coins`)

`Coin` (Fibonacci rungs, `Split`, `SplitCount`), `Token` (64 tiers,
`Canonical`, `Measure`, `Fuse`, `Split`, octane levels in `Octane`),
`FibonacciCode`, and `TokenWallet`
(`platform.CreateTokenWallet(cap, modelId, octane)`, needs `Mint`): `Mint`,
`Spend`, `TransferTo`, `SetOctane`, `GetCounts`. Limits: VFV coin minting is
not in this ABI; wallet tokens exist only by `Mint` and live in memory.

```csharp
using TokenWallet a = platform.CreateTokenWallet(cap, modelId: 1, Octane.Racing);
using TokenWallet b = platform.CreateTokenWallet(cap, modelId: 1);
a.Mint(1_000_000);
a.TransferTo(b, 1000);
Console.WriteLine($"{a.Value} {b.Value}");                       // 999000 1000
```

### Assurance fee (`Zxv.Fees`)

`AssuranceFee.Compute`, `ComputeWithCarry` (exact remainder carried between
payments), `Split` into the four pools, and `DefaultPolicy`. Pure integer
arithmetic; no capability. Limits: arithmetic only; tax, securities and
consumer-law treatment is a question for counsel.

```csharp
FeeCharge c = AssuranceFee.ComputeWithCarry(0, 12_345);
FeeSplit s = AssuranceFee.Split(c.Fee);                          // s.Total == c.Fee
```

### V-Bill hub (`Zxv.VHub`)

`VBillHub` (`platform.CreateVBillHub(cap, seed)`, needs `LedgerPost`):
test currencies with an ML-DSA-87 issuer key, signed rational pegs
(`PegMessage`), accounts, issuance, limits, `Quote` and `Swap`, and `Check`.
Limits: a demonstration with test codes only (ISO 4217 codes are refused),
in-memory, no settlement rail or issuer; V-Bills are likely securities in
many places; an exact peg removes rounding, not exchange-rate risk.

```csharp
using VBillHub hub = platform.CreateVBillHub(cap);
hub.AddCurrency(1, "XTA", 2, issuer.PublicKey, pool, backing);
hub.SetPeg(1, 1, 1, epoch: 1, dsa.Sign(issuer.SecretKey, PegMessage.Create(1, 1, 1, 1).ToArray()));
VHubQuote q = hub.Quote(1, 2, want: 500);
```

### Freight (`Zxv.Freight`)

`FreightHeader` (value type), `FreightCodec` (`Encode`/`Decode` an
erasure-coded freight, `Split` large data into members, `FreightJoin` to
reassemble) and `FreightOps` (`Encode`, `Expand`, `Peek`, `RunGenerator`,
`FreightOpsWriter` for LITERAL/COPY/CIDREF/SEED ops). The CIDREF resolver
is a `CidRefResolver` delegate called during `Expand` on the calling thread;
its exception is rethrown from `Expand` and never crosses into C. No
capability. Limits: lost rows are recovered, corrupted rows only detected;
headers are not authenticated; ops give no size guarantee.

```csharp
byte[] ops = FreightOps.Encode(data);
byte[] back = FreightOps.Expand(ops, FreightOps.MaxBytes, (cid, output) => store.TryCopy(cid, output));
```

### Web4 agents (`Zxv.Web4`)

`Web4Agent` (`platform.CreateWeb4Agent(cap, name, nowMs, seed, node)`,
needs `CryptoSign`; consent needs `Attest`): agent cards, `ToolManifest`
signing, `IssueConsent`, `Seal` and `Reply` envelopes. `AgentCard.Verify`,
`AgentDirectory`, `ConsentLog` (single use, revoke, prune) and `Envelope`
(`Decode`, `Verify`). The agent's secret keys stay inside the native handle,
are wiped when it is released, and never appear in `ToString` or logs. There
is no clock: pass Unix milliseconds. Limits: signed, not encrypted; no
replay windows, rate limits or Web 2/3 bridges; ZXV-to-ZXV only; a consent
token proves a key signed, not that the right human understood it.

```csharp
using Web4Agent alice = platform.CreateWeb4Agent(cap, "alice", now);
using Web4Agent bob = platform.CreateWeb4Agent(cap, "bob", now);
byte[] req = alice.Seal(EnvelopeKind.Request, bob.AgentId, 1, 0, "pay", EnvelopeMarks.None, "{}"u8, now);
bool ok = Envelope.Verify(req, alice.PublicKey);
```

### Paradox (`Zxv.Paradox`)

`ParadoxEngine` (pure classification), `ParadoxBook`
(`platform.CreateParadoxBook(cap, logCap, regCap)`, needs `Paradox`):
registry, hash-chained logs, `Constructive` (policy change inside published
bounds), `Destructive` (containment), `Neutral` (hold with branches),
`Special` (frontier graph), `Draft`/`Review`/`Pass` and `RepairProof`;
`ParadoxPolicy`, `ParadoxHold` and `ParadoxGraph` come from the platform.
Limits: contradiction-tolerant bookkeeping, not a judge of truth; anything
touching money or devices goes to the owner ("owner approved" is the
caller's assertion); destructive work is contained, never answered; nothing
signs. Advisory only.

```csharp
using ParadoxBook book = platform.CreateParadoxBook(cap, 64, 16);
ParadoxClassification c = book.Classify(tag);
Console.WriteLine(ParadoxEngine.KindName(c.Kind));
```

## Vino Embed (ABI 1.6)

`Zxv.Vino` is what an app or game needs to take part in Vino without ever
holding anyone's value or keys (`ZxvFeatures2.Vino`). There are two lanes,
and they never meet:

- **Play lane.** `PlaySandbox` (`platform.CreatePlaySandbox(cap)`, needs
  `CapabilityRights.Vino`) is a sandbox inside the app with its own play
  units, issuers, holders and Fibonacci-face vouchers. Play units are not
  money: they come from a faucet (`PostBacking`), are worth nothing, and are
  lost when the sandbox is disposed. No call takes a sandbox and anything
  live, so play units can never become live value.
- **Live lane.** Real Vino stays in the user's own ZXV wallet. The app builds
  a `PaymentRequest` and encodes it (160 bytes, `PaymentRequestBytes`, id =
  SHA-256 of the bytes); the wallet approves it and signs a receipt with its
  ML-DSA-87 key (`Receipt.Sign`, the wallet's call); the app checks the
  receipt offline (`Receipt.Verify`). The app never sees a balance, a key or
  a custody account.

Rules worth knowing:

- A voucher is active on one rail at a time, `VinoRail.Bearer` (the note in
  hand) or `VinoRail.Ghost` (its ledger twin). A transfer must name that rail
  (else `ZxvStatus.WrongRail`), so a voucher can never be spent twice.
- An issuer may issue only while 10 x backing >= 18 x outstanding (1.8x
  coverage, exact integers); otherwise `ZxvStatus.InsufficientCoverage`.
- `Redeem` returns a voucher to its issuer and retires it. It pays nothing
  out. There is no interest, yield or return of any kind.
- A LIVE request may not sell a paid chance outcome (loot boxes, paid
  draws): `PaymentRequestFlags.PaidChance` is refused with
  `ZxvStatus.PaidChance`. The raffle is a separate module.
- `SpendMeter` (`platform.CreateSpendMeter(cap, dailyCap)`) counts what one
  device approved per day and refuses what would pass the cap. It is a
  counter, not a legal check.

New statuses: `WrongRail` (-46), `InsufficientCoverage` (-47), `Expired`
(-49) and `PaidChance` (-50) are REFUSED (`ZxvRefusedException`);
`WrongLane` (-48) is USAGE (`ZxvUsageException`). New rights:
`CapabilityRights.Vino` and `CapabilityRights.AllAbi16` (`AllAbi15` keeps its
value). Requests, receipt messages and receipt checks are pure and need no
capability; signing needs `CryptoSign`. Amounts, ids and times are `ulong`.

A play-lane shop: the shop backs itself, sells a voucher to a player, the
player spends it at a friend, and the friend redeems it.

```csharp
using ZxvPlatform platform = ZxvPlatform.Create();
using Capability cap = platform.CreateRootCapability(CapabilityRights.Vino);
using PlaySandbox play = platform.CreatePlaySandbox(cap);

uint shop = play.OpenIssuer(owner: 1);
uint player = play.OpenHolder(owner: 100);
uint friend = play.OpenHolder(owner: 101);

play.PostBacking(shop, 18);                                  // play units: not money
ulong voucher = play.Issue(shop, player, face: 8, VinoRail.Bearer);
play.Transfer(voucher, player, friend, VinoRail.Bearer);     // must name the active rail
play.Redeem(voucher, friend);                                // retired; nothing is paid out

Console.WriteLine(play.GetVoucher(voucher).State);           // Redeemed
Console.WriteLine(play.GetAccount(shop).Faces);              // 0 outstanding
play.Check();
```

A live payment request, the wallet's receipt, and the app's offline check.
In a real deployment the two halves run in different places: the wallet
key never reaches the app.

```csharp
using ZxvPlatform platform = ZxvPlatform.Create();
ulong now = (ulong)DateTimeOffset.UtcNow.ToUnixTimeSeconds();

// App: build and encode the request, send the 160 bytes to the wallet.
byte[] merchant = SHA256.HashData("my-shop-public-key"u8);
byte[] nonce = RandomNumberGenerator.GetBytes(16);
var request = new PaymentRequest(VinoLane.Live, merchant, nonce, amount: 1_234_567,
    createdAt: now, expiresAt: now + 600, memo: "Sword of Dawn");
PaymentRequestBytes bytes = request.Encode();

// Wallet: the user approves; the wallet signs with its own key.
using Capability walletCap = platform.CreateRootCapability(CapabilityRights.CryptoSign);
using KeyPair wallet = new MlDsa87(walletCap).GenerateKey();
ulong approvedAt = now + 5;
byte[] receipt = Receipt.Sign(walletCap, bytes, wallet.SecretKey, wallet.PublicKey, approvedAt);

// App: check the receipt offline (no capability, no network).
ReceiptVerdict verdict = Receipt.Verify(bytes, wallet.PublicKey, approvedAt, receipt, now + 10);
Console.WriteLine(verdict);                                  // Valid
```

Limits: ML-DSA-87 here is the reference implementation, not FIPS 140-3
validated. The live flow is the app's half only: approving a request and
posting the payment happen in the user's wallet (zxvd). Whether a given
deployment of Vino is store credit, a prepaid access product or something
else is a legal question pending counsel; nothing in this SDK decides it.

## API overview

| Area | Types | Key members |
|---|---|---|
| Library | `ZxvLibrary` | `AbiVersion`, `Features`, `Features2`, `Supports`, `ClassOf`, `GetStatusName`, `FatalPolicy`, `UseLibraryPath` |
| Platform | `ZxvPlatform` | `Create`, `CreateRootCapability`, `CreateAttestations`, `CreateBudgets`, `CreatePhaseClock`, `CreateSwarm`, `CreateHwMatrix`, `CreateHwRegion`, `CreateAipiNode`, `CreateTokenWallet`, `CreateVBillHub`, `CreateWeb4Agent`, `CreateParadoxBook`/`Policy`/`Hold`/`Graph`, `CreatePlaySandbox`, `CreateSpendMeter`, `Halt`, `IsHalted` |
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
| Treaties | `Zxv.Treaties.Treaty`, `TreatyResult` | `ParseSutra`, `ParseRecord`, `Validate`, `ToSutra`, `ToRecord`, `Run` |
| AIPI | `Zxv.Aipi.Cid`, `AipiNode` | `Compute`, `Parse`, `ToV1`, `Put`, `Get`, `Plan` |
| Coins | `Zxv.Coins.Coin`, `Token`, `FibonacciCode`, `TokenWallet` | `Split`, `Canonical`, `Fuse`, `Mint`, `Spend`, `TransferTo` |
| Fees | `Zxv.Fees.AssuranceFee`, `FeeSplit`, `FeePolicy` | `Compute`, `ComputeWithCarry`, `Split`, `DefaultPolicy` |
| V-Bill hub | `Zxv.VHub.VBillHub`, `PegMessage` | `AddCurrency`, `SetPeg`, `OpenAccount`, `Issue`, `Quote`, `Swap`, `Check` |
| Freight | `Zxv.Freight.FreightHeader`, `FreightCodec`, `FreightJoin`, `FreightOps`, `FreightOpsWriter` | `Encode`, `Decode`, `Split`, `Add`, `Expand`, `Peek`, `RunGenerator` |
| Web4 | `Zxv.Web4.Web4Agent`, `ToolManifest`, `ConsentToken`, `ConsentLog`, `Envelope`, `AgentCard`, `AgentDirectory` | `Sign`, `IssueConsent`, `Seal`, `Reply`, `Verify`, `Check`, `Decode` |
| Paradox | `Zxv.Paradox.ParadoxEngine`, `ParadoxBook`, `ParadoxPolicy`, `ParadoxHold`, `ParadoxGraph`, `RepairProof` | `Classify`, `Constructive`, `Destructive`, `Neutral`, `Special`, `Draft`, `Review`, `Pass`, `VerifyLog` |
| Vino | `Zxv.Vino.PlaySandbox`, `PaymentRequest`, `PaymentRequestBytes`, `PaymentRequestId`, `Receipt`, `ReceiptMessage`, `ReceiptVerdict`, `SpendMeter`, `VoucherInfo`, `VinoAccount` | `OpenIssuer`, `OpenHolder`, `PostBacking`, `WithdrawBacking`, `Issue`, `Transfer`, `SetRail`, `Redeem`, `GetVouchers`, `Check`, `Encode`, `Decode`, `Sign`, `Verify`, `Admit`, `TryAdmit` |
| DI | `ZxvServiceCollectionExtensions` | `AddZxv`, `AddZxvCapability` |
| Native | `Zxv.Interop` | `ZxvStatus`, `ZxvStatusClass`, `ZxvFeatures`, `ZxvFeatures2`, SafeHandles (internal P/Invoke) |

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
(three SLH-DSA signatures, about five seconds). `Abi15/*` covers ABI 1.5
against the vectors status_abi15, treaty, cid, aipi, coin, fee, vhub,
freight, web4 and paradox, refusals included, plus each new status's
exception, `Features2`, the capability bits and refusals, the buffer-growth
path, handles outliving their platform, and a poisoned (halted) context.
`Abi16/*` covers ABI 1.6 against status_abi16 and vino: every request's
bytes, id and refusal, decode refusals by changed byte, the fixed-seed
receipt (message, deterministic signature and every check), the ordered
play scenario with its final accounts and vouchers, and the meter scenario;
plus each new status's exception, capability bits and refusals, the voucher
list path, use after dispose, and a halted context.

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
