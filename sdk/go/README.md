<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# ZXV Go SDK

Package `zxv` is a cgo binding to `libzxv`, the stable C ABI of the ZXV
platform (`sdk/libzxv/include/zxv_api.h`, ABI 1.6).

```
import "github.com/sykomiked/ZEDEC-XERO-VULPINE-V1.0.0/sdk/go/zxv"
```

## Install

Build `libzxv` first (`make -C sdk/libzxv`), then use the module. cgo must be
enabled (`CGO_ENABLED=1`, the default with a C compiler present).

By default the package compiles against `sdk/libzxv/include` and links
`sdk/libzxv/build/libzxv.so` with an rpath to that directory, so tests and
examples in this repository need no setup. To use a library installed
elsewhere, add flags through the standard cgo variables (they are appended
to the package's own):

```sh
export CGO_CFLAGS="-I/opt/zxv/include"
export CGO_LDFLAGS="-L/opt/zxv/lib -Wl,-rpath,/opt/zxv/lib"
# or link statically:
export CGO_LDFLAGS="/opt/zxv/lib/libzxv.a -lpthread -lm"
```

At run time the loader must find `libzxv.so` (the rpath above,
`LD_LIBRARY_PATH`, or a system path).

## Quickstart

```go
ctx, err := zxv.NewContext()                            // one platform instance
if err != nil { log.Fatal(err) }
defer ctx.Close()
root, _ := ctx.RootCapability(1, nil, zxv.CapAll)       // what you may do
defer root.Close()
_ = ctx.EnableCurrencyCap(root, "NGN")
ledger := zxv.NewLedger(root)
bank, _ := ledger.OpenAccount("bank", "NGN", zxv.Issuer)
alice, _ := ledger.OpenAccount("alice", "NGN", zxv.Holder)
_, err = ledger.Post(bank, alice, 12550, "NGN", "E2E-0001") // exact minor units
```

## Concepts in Go terms

**Handles.** `Context`, `Capability`, `Budgets`, `PhaseClock`, `Swarm`,
`Attestations`, `Model`, `Session`, `Message` and `SecretKey` wrap C handles.
Call `Close()` (typically with `defer`); a `runtime.SetFinalizer` backstop
frees anything you forget. The C rule "destroy a child before its parent"
is enforced at run time: closing a parent while a child made from it is
still open returns an error (class REFUSED, `ZXV_E_STATE`) and changes
nothing. Use after `Close` returns an error matching `zxv.ErrClosed`.

**Concurrency.** `Context`, `Capability`, `Model`, `Budgets`, `PhaseClock`,
`Swarm` and `Attestations` are safe for concurrent use. `Session` and
`Message` serialize their calls with a mutex (the C handles are
single-threaded). Every C call and its `zxv_last_error` read happen on one
locked OS thread, so error details are never mixed up between goroutines.

**Errors are values.** Every error from the library unwraps to
`*zxv.Error{Status, Name, Detail, Class}` and is wrapped in its class type
(`*UsageError`, `*RefusedError`, `*FatalError`, `*SystemError`). Branch with
`errors.Is`:

```go
switch {
case errors.Is(err, zxv.ErrBudgetExhausted):   // REFUSED: wait or shed load
case errors.Is(err, zxv.ErrCapabilityDenied):  // REFUSED: wrong capability
case errors.Is(err, zxv.ErrFatal):             // context halted: never retry
case errors.Is(err, zxv.ErrUsage):             // your bug
}
var e *zxv.Error
if errors.As(err, &e) { log.Println(e.Status, e.Name, e.Detail) }
```

**Fail closed.** A FATAL status (a broken ledger invariant, or
`ctx.Halt`) halts the context in C: every later call returns
`ZXV_E_POISONED`. The Go `Context` is also marked poisoned, and every
handle derived from it fails fast with `ErrFatal`.
`zxv.NewContext(zxv.WithFatalPolicy(zxv.FatalExit))` instead prints the
error and calls `os.Exit(70)`.

**Capabilities.** Objects that make gated calls (`Ledger`, `Budgets`,
`PhaseClock`, `Swarm`, `Attestations`, `Ed25519`, `MLDSA87`, `MLKEM1024`,
`StateChannel`, `Session`) hold a `*Capability` and pass it on every call.
Narrow with `cap.Attenuate(zxv.CapLedgerRead)`; `Revoke` revokes all
descendants; an attestation other than `TruthTrue` blocks the capability.

**Exact numbers.** `Rational` is an immutable value (two `int64`, reduced)
with `Add`, `Sub`, `Mul`, `Div` returning `(Rational, error)` (overflow is
`ZXV_E_RANGE`, never a wrap), exact `Cmp`, `ParseRational("12.75")` and an
exact `String()`. No function takes a `float64` except
`FromFloat64Exact`, which returns the float's exact value (`0.1` is
`3602879701896397/36028797018963968`) and rejects NaN and infinity with
`ErrFloatRejected`. Sampling options are `Rational`s converted to Q16.16 by
the library.

**Four-valued logic.** `Truth` is a small struct with four values
(`TruthNeither`, `TruthTrue`, `TruthFalse`, `TruthBoth`, numbered 0..3).
It is not a `bool`: use `IsCertainlyTrue()`, `ToBool(onBoth, onNeither)` or
`RequireTrue()`.

**Secrets.** `SecretKey` keeps secret keys and shared secrets in
`C.malloc` memory, outside the Go heap; `Close` (or the finalizer) wipes it
with `zxv_secure_zero` and frees it. `String()` is redacted;
`ExportBytesUnsafe()` is the only way out.

**Waiting.** `Budget.ConsumeWait(ctx, amount)` consumes now or blocks until
the budget is replenished (a `Release` or a larger `SetLimit` through this
SDK) and retries; it fails at once on an invalid amount, and returns
`ctx.Err()` on cancellation or timeout. `ConsumeAsync` returns a `<-chan
error`. `DeferredEvent.Wait(ctx)` blocks until the phase clock passes the
event's tick; drive the clock with `Advance(n)` or `RunRealtime(ctx,
ticksPerSecond)` in a goroutine.

**Generation.** `Session.Generate(ctx, ...)` (all at once),
`GenerateFunc(ctx, ..., func(Token) bool)` (a token callback through an
exported Go function and `runtime/cgo.Handle`), and `GenerateStream(ctx,
...)` (tokens on a channel; `Close` stops early). Cancelling `ctx` stops
generation at the next token and returns the partial result with
`ctx.Err()`. The same seed and inputs give the same tokens on every
machine.

## Capabilities on every gated call (ABI 1.2)

Every gated operation takes a `*Capability`, and the SDK calls the
capability-gated `zxv_*_cap` form of the C ABI:

| Call | Needs |
|---|---|
| `ctx.EnableCurrencyCap`, `EnableCurrencyWithMinorCap`, `RegisterPrivateCurrencyCap` | `CapLedgerPost` |
| `Ledger.OpenAccount`, `Post` | `CapLedgerPost` |
| `Ledger.Balance`, `Account`, `AccountCount`, `Journal`; `cap.NewMessage` | `CapLedgerRead` |
| `NewBudgets`, `NewPhaseClock`, `NewAttestations`, `NewSwarm` | `CapBudget`, `CapPhase`, `CapAttest`, `CapSwarm` |

A capability of another context is refused with `ErrCapabilityDenied`
(`ZXV_E_CAP`). A message keeps the capability it was made with open, and
the library re-checks it on every call. The 1.0 forms
`ctx.EnableCurrency`, `EnableCurrencyWithMinor`, `RegisterPrivateCurrency`
and `ctx.NewMessage` still work but are marked `// Deprecated:`.

## The hexagram tag (ABI 1.3)

A `Hexagram` tags one unit of work with a four-valued base, the polarity
of its evidence (entropic, neutral, forbidden, negentropic) and its phase
(potential -> emerging -> manifest -> dissolving), packed into one byte
0..63. Its action is proceed, wait, refuse, escalate or rollback. Every rule
comes from libzxv and is tested against `vectors.json` key `hex`.
Forbidden is absorbing, a phase moves one step at a time, and the base is
**not** numbered like `Truth` (TRUE is base 3 but Truth 1): convert only
with the explicit `ToTruth()` / `HexagramFromTruth(...)`. Combining BOTH with NEITHER follows
the `Truth` tables (AND gives NEITHER, OR gives BOTH); those two results
are pending an owner decision. A `QuarantineLog` (needs the ATTEST
capability) records forbidden work in a SHA-256 hash chain; when it is
full, append is refused with `ZXV_E_CAPACITY` and you must halt the
context. See [ABI.md, Changes in 1.3](../docs/ABI.md#changes-in-13).

```go
h, _ := zxv.NewHexagram(zxv.HexBaseTrue, zxv.HexNegentropic, zxv.HexPotential)
_ = h.Action()                           // zxv.HexProceed
h = h.Advance()                          // HexEmerging
_, err := h.Step(zxv.HexDissolving)      // errors.Is(err, zxv.ErrRefused): a jump
bad := h.Forbid()                        // HexRollback, forever
log, err := zxv.NewQuarantineLog(cap, 1024) // needs CapAttest
defer log.Close()
seq, err := log.Append(bad, 7, 42, digest32)
err = log.Verify()
```

`Hexagram` is a comparable value; its zero value is tag 0
(false/entropic/potential).

## Hardware emulation and hardware updates (ABI 1.4)

Profiles are plain data; the matrix needs `CapHWMatrix`; regions and
contract packages need `CapHWReconf`; release keys and packages need
`CapCryptoSign`. `CapAll` keeps its 1.1 value, so use `CapAll14` for every
bit. New sentinels: `ErrNoProvider`, `ErrDispute`, `ErrQuarantined`,
`ErrRollback`, `ErrBlocked`, `ErrTarget` (USAGE), `ErrPolicy`. Everything is
integers. See [HW_EMULATION.md](../docs/HW_EMULATION.md).

* `HwProfile` (ZHP-1): `HwProbeHost`, `HwReference` (R1, 16 nodes),
  `HwPresent`, `Encode` / `DecodeHwProfile`, `Digest`, `AddNode`,
  `SetCap` / `Cap`, `SetExt` / `Ext` (opaque, at most `HwExtMax` bytes),
  `HwMatchProfiles`, `HwPairNode`.
* `HwMatrix`: starts with the standard engines (`HwFnSHA256`, `HwFnKeccak`,
  `HwFnMAC`, `HwFnGCD`, `HwFnLogic64`, `HwFnTensor`), each served by a
  proven SOFT implementation. Discover with `Providers` / `ProviderInfo` /
  `FindProvider` / `FunctionInfo`; constrain with `SetPolicy`
  (`HwPolicyKeyBearing` is sticky and keeps work local); add your own
  implementations (`HwProviderFunc`) with `AddProvider`, prove them with
  `Attest`; `Call`, `CallInto`, `Shadow`, `Memo`, `TrustKey` +
  `ApplyContract`, `Log` / `LogEntry`.
* `HwRelease`: `KeyGen` (an `HwReleaseKey` whose two secret halves are
  `SecretKey`s, wiped on `Close`) and `Build` (header || payload || ML-DSA-87
  and SLH-DSA signatures; SLH-DSA takes seconds).
* `HwRegion`: `NewHwRegion` with an `HwDevice` you implement (`Load`,
  `Readback`, `Activate`), `TrustKey`, `Stage`, `Trial`, `Commit`,
  `Rollback`, `Tick`, `Info`, `OnEvent`, `Log` / `LogEntry` (records carry a
  `Hexagram` tag). A region keeps its matrix open.

```go
r1, _ := zxv.HwReference()
host, _ := zxv.HwProbeHost()
m, _ := zxv.HwMatchProfiles(host, r1) // m.Native, m.Emulated
root, _ := ctx.RootCapability(1, nil, zxv.CapAll14)
mx, _ := zxv.NewHwMatrix(root)
defer mx.Close()
digest, _ := mx.Call(zxv.HwFnSHA256, 0, []byte("abc")) // ba7816bf...
```

Callbacks reach Go through exported functions and `cgo.Handle`s, on the
goroutine of the call that needs them. A panic is recovered at the C
boundary and reported as `ZXV_E_INTERNAL`; an error is reported as its
status. A provider may call the matrix again for the functions it declared
with `AddDependency`.

## ABI 1.5: treaties, content ids, coins, fees, hub, freight, Web 4, paradox

New capability bits: `CapAIPI` (store and publish AIPI blocks), `CapMint`
(token wallets) and `CapParadox` (paradox books, policies, holds, graphs);
`CapAll15` is every bit. `LiveFeatures2` reports which groups were built
(`Feat2Treaty` ... `Feat2Paradox`). New sentinels, each with its class:
`ErrDenom` and `ErrTreaty` (USAGE); `ErrCID`, `ErrConsent`, `ErrErasure`,
`ErrPeg`, `ErrLimit`, `ErrParadoxStep`, `ErrParadoxOwner` (REFUSED).
Fixed-size byte structures are Go value types you can compare with `==`
(`CID`, `FreightHeader`, `W4ConsentToken`, `RepairProof`, `ParadoxDraft`,
the 40-byte peg message). Handles tied to a context (`AipiNode`,
`TokenWallet`, `VHub`, `W4Agent`, `ParadoxBook`, `ParadoxPolicy`,
`ParadoxHold`, `ParadoxGraph`) keep the capability they were made with,
use it on every gated call, fail closed when the context halts, and stop
that capability from closing while they are open. Handles that libzxv does
not make thread-safe (`Treaty`, `FreightJoin`, `FreightOpsWriter`,
`W4Agent`, `W4Directory`, `W4Manifest`, `W4ConsentLog`, policy, hold and
graph) serialize their own calls, so they may still be shared.

### Treaty

COBOL copybook treaties and canonical Sutra text: parse, emit, validate and
run NET or SETL settlement with PIC S9(18) arithmetic. A refusal is a
`*TreatyError` carrying the fault and its byte offset.

```go
tr, err := zxv.ParseTreatySutra("TREATY C USD SETL\nPARTY A 5000 4000 1/2\nPARTY B 3000 3000 1/2\nOWE A B 1200\nEND\n")
var te *zxv.TreatyError
if errors.As(err, &te) { fmt.Println(te.Fault, te.Offset) }
rec, _ := tr.Record()      // the 2542-byte copybook record
res, err := tr.Run()       // ErrInsufficientFunds when a party cannot pay
```

### CID and AIPI

IPFS CIDs (`ComputeCID`, `UnixFSCID`, `ParseCID`, `Text`, `Info`, `ToV1`,
`Verify`, `ProviderKey`) and AIPI function, call and claim records. An
`AipiNode` is an offline in-memory block store (no network).

```go
c, _ := zxv.ComputeCID(zxv.CIDCodecRaw, []byte("abc"))
fmt.Println(c) // bafkrei...
root, _ := ctx.RootCapability(1, nil, zxv.CapAll15)
node, _ := zxv.NewAipiNode(root, zxv.AipiStoreMin, 64, nil)
defer node.Close()
id, _ := node.Put([]byte("zxv module bytes"), false)
data, _ := node.Get(id) // re-hashed before it is returned
```

### Coin and token octane

Fibonacci coin faces and Zeckendorf splits (`CoinFace`, `CoinRung`,
`CoinSplit`, `CoinSplitCount`), swarm token tiers (`TokenFace`,
`TokenTier`; a dense token is worth exactly its face: density carries no
premium, fee or extra allotment), `TokenHolding` (64 tier counts: `Value`, `Compress`, `Fuse`,
`Split`, `CanonicalHolding`), the token prefix code
(`EncodeTokenCodes` / `DecodeTokenCodes`) and a `TokenWallet` (needs
`CapMint`).

```go
rungs, _ := zxv.CoinSplit(100)                       // [10 5 3]: 89 + 8 + 3
h, _ := zxv.CanonicalHolding(100, zxv.OctaneMidgrade) // fewest pieces up to face 89
w, _ := zxv.NewTokenWallet(root, 1, zxv.OctanePlain)
defer w.Close()
_ = w.Mint(100)
```

### Fee

The 0.08889% assurance fee and its 50/25/15/10 split, exact and pure.

```go
fee, _ := zxv.AssuranceFee(1125)                 // 1
fee, rem, _ := zxv.AssuranceFeeCarry(0, 100)     // 0, 888900: store rem per account
parts, _ := zxv.SplitFee(10)                     // [6 2 1 1]
```

### V-Bill hub

A demonstration hub with test currencies (ISO codes are refused), signed
exact pegs, quotes and all-or-nothing swaps on its own in-memory ledger.
Every currency needs a limit rule before it can swap (fail closed).

```go
hub, _ := zxv.NewVHub(root, nil)
defer hub.Close()
_ = hub.AddCurrency(0, "ZTA", 2, issuerPK, 500000, 10000)
msg, _ := zxv.VHubPegMessage(0, 350, 1, 1)
sig, _ := zxv.NewMLDSA87(root).SignDeterministic(issuerSK, msg[:], nil)
_ = hub.SetPeg(0, 350, 1, 1, sig) // ErrPeg when stale, ErrVerificationFailed for a wrong key
```

### Freight

168 erasure-coded 21-byte rows per freight: any k rows rebuild the payload.
Sets split and join larger payloads; op streams expand under a limit you
set. A CIDREF resolver is a Go function called during `FreightOpsExpand`;
a panic in it is recovered and reported as "not resolved".

```go
h, _ := zxv.NewFreightHeader(42, 10, 0, 0, payload) // k = 10, up to 200 bytes
packets, _ := zxv.FreightEncode(h, payload)          // 3528 bytes
out, err := zxv.FreightDecode(h, packets[21*100:])   // rows 100..167 are enough
if errors.Is(err, zxv.ErrErasure) { /* wait for more rows */ }
```

### Web 4

Agents with ML-DSA-65 keys that never leave their `W4Agent` handle (Close
wipes them), signed cards and tool manifests, a directory, human consent
tokens and signed envelopes. Every call that needs the time takes `nowMs`.

```go
alice, _ := zxv.NewW4Agent(root, nil, "alice", nil, now)
defer alice.Close()
m, _ := zxv.NewW4Manifest(1, now, now+86400000)
defer m.Close()
_ = m.AddTool(zxv.W4Tool{Name: "pay", PriceVFV: 150, RatePerMin: 60, Burst: 10, Flags: zxv.W4ToolMoney})
_ = m.Sign(alice)
digest, _ := zxv.W4ActionDigest("pay", from, to, 7, []byte("amount=100"))
tok, _ := zxv.IssueW4Consent(root, human, zxv.W4ConsentRequest{AgentID: aliceID, Action: digest,
	Scope: zxv.W4ConsentMoney, MaxVFV: 1000, IssuedMs: now, ExpiresMs: now + 600000})
```

### Paradox

Classify a `Hexagram` into a kind and run the fixed, logged operator for it.
REFUSED and PROPOSAL_FOR_OWNER are outcomes, not errors; `GraphPass`
returns `ErrParadoxStep` when a graph escalates to its owner.

```go
book, _ := zxv.NewParadoxBook(root, 64, 16)
defer book.Close()
tag, _ := zxv.NewHexagram(zxv.HexBaseBoth, zxv.HexNeutral, zxv.HexManifest)
c, _ := book.Classify(tag, &zxv.ParadoxEvidence{Pos0: 3, Neg0: 1, Pos1: 5, Neg1: 1}, nil)
pol, _ := zxv.NewParadoxPolicy(root, []int64{0}, []int64{100}, []int64{50}, 0)
defer pol.Close()
out, _ := book.Constructive(pol, c.ParadoxClass, []int64{60}, 5) // ParadoxApplied
```

## Vino Embed (ABI 1.6)

`vino.go` lets an app or game take part in Vino without ever holding
anyone's value or keys. Two lanes never meet:

- **PLAY** (`VinoPlay`): a sandbox inside the app with its own play units,
  issuers, holders and Fibonacci-face vouchers, in process memory only. Play
  units are not money, cannot be converted into or moved to live value, and
  are lost on `Close`.
- **LIVE**: real Vino stays in the user's own ZXV wallet. The app builds a
  `VinoRequest`; the wallet approves it and signs a receipt
  (`SignVinoReceipt`, the wallet's call with its own key); the app checks the
  receipt offline (`VerifyVinoReceipt`). The app never sees a balance, a key
  or a custody account.

A voucher is active on exactly one rail, `VinoBearer` or `VinoGhost`, and
can be spent only on that rail (`ErrRail`). An issuer may issue only while
`10*backing >= 18*outstanding` (`ErrCoverage`). Redeeming retires a voucher
and pays nothing out; there is no interest, yield or return of any kind.
`VinoMeter` counts what one device approved per day against a daily cap
(`ErrLimit`); it is a counter, not a legal check.

New: `CapVino` (play lane and meter; `CapAll16` is every bit), `Feat2Vino`,
and the statuses `ErrLane` (USAGE), `ErrRail`, `ErrCoverage`, `ErrExpired`,
`ErrChance` (REFUSED). Requests, ids and receipt messages are value types
(`VinoRequestRecord` 160 bytes, `VinoRequestID` 32, `VinoReceiptMessage`
88). Amounts, ids and times are `uint64`. Encoding, decoding, the receipt
message and verification are pure and need no capability.

Play-lane shop:

```go
root, _ := ctx.RootCapability(1, nil, zxv.CapVino)
play, _ := zxv.NewVinoPlay(root)
defer play.Close()
issuer, _ := play.Open(100, zxv.VinoIssuer)
player, _ := play.Open(200, zxv.VinoHolder)
shop, _ := play.Open(300, zxv.VinoHolder)
_ = play.Back(issuer, 18)                                   // play faucet: room for faces up to 10
coin, _ := play.Issue(issuer, player, 8, zxv.VinoBearer)    // a face of 13 would be ErrCoverage
_ = play.Transfer(coin, player, shop, zxv.VinoBearer)       // the player spends it
_ = play.Redeem(coin, shop)                                 // retired; nothing is paid out
ids, _ := play.Vouchers(shop)                               // active vouchers: none left
```

Live payment request, wallet receipt, offline verify:

```go
req := zxv.VinoRequest{Lane: zxv.VinoLaneLive, Merchant: merchantID, Nonce: nonce,
	Amount: 1234567, CreatedAt: now, ExpiresAt: now + 600, Memo: "Sword of Dawn"}
rec, id, err := req.Encode()           // ErrChance for a paid chance outcome on LIVE
// ... the wallet (not the app) approves:
sig, _ := zxv.SignVinoReceipt(walletCap, rec, walletSK, walletPK, approvedAt)
// ... back in the app, offline:
err = zxv.VerifyVinoReceipt(rec, walletPK, approvedAt, sig, uint64(time.Now().Unix()))
// nil, ErrVerificationFailed, or ErrExpired
```

Both run as `ExampleVinoPlay` and `ExampleVerifyVinoReceipt`. Honest
limits: the LIVE flow here is only the app's half; the wallet posts the
payment on its own ledger. ML-DSA-87 is the reference implementation, not
FIPS 140-3 validated. Whether a deployment of Vino is store credit, a
prepaid access product or something else is a legal question pending
counsel; nothing here decides it. The allotment is a separate module.

## API overview

| Area | Types | Key calls |
|---|---|---|
| Library | `Version`, `LiveFeatures`, `LiveFeatures2` | ABI version, feature bits |
| Context | `Context`, `FatalPolicy` | `NewContext`, `RootCapability`, `Halt`, `EnableCurrencyCap`, `EnableCurrencyWithMinorCap`, `RegisterPrivateCurrencyCap` |
| Capability | `Capability`, `Caps` | `Attenuate`, `Revoke`, `SetAttestation`, `Check`, `Info`, `NewMessage` |
| Exact numbers | `Rational` | `NewRational`, `ParseRational`, `Add`/`Sub`/`Mul`/`Div`, `ToQ16`, `FromFloat64Exact` |
| Logic | `Truth` | `And`, `Or`, `Not`, `TruthFromEvidence`, `ToBool` |
| Ledger | `Ledger`, `AccountID`, `Balance` | `OpenAccount`, `Post`, `Balance`, `Check`, `Account`, `AccountCount`, `Journal` |
| ISO data | `Currency`, `Message` | `CurrencyByAlpha`, `CurrencyByNumeric`, `SetText`, `SetAmount`, `Render` |
| Attestations | `Attestations` | `Attest`, `Evidence`, `State`, `GateClear` |
| Budgets | `Budgets`, `Budget` | `Create`, `Consume`, `ConsumeWait`, `ConsumeAsync`, `Release`, `SetLimit`, `OnExhausted` |
| Phase | `PhaseClock`, `DeferredEvent` | `Advance`, `Admit`, `Defer`/`UntilTick`/`After`, `Wait`, `RunRealtime` |
| Wire | `Ubh168Frame` | `WireTag`, `PackFrame`, `Unpack` (pure Go), `Resolve`, `IdleFrame` |
| Swarm | `Swarm`, `Feeling` | `Register`, `SetMood`, `BeginCycle`, `Consume`, `Allotment` |
| Crypto | `Ed25519`, `MLDSA87`, `MLKEM1024`, `SecretKey` | key generation, sign/verify, encapsulate/decapsulate, `SHA3256`, `Keccak256` |
| Engine | `Model`, `Session`, `GenerateOptions` | `OpenModel`, `Tokenize`, `NewSession`, `Generate`, `GenerateStream`, `BindBudget` |
| EVM | `StateChannel` | `EncodeState`, `LedgerState` |
| Hexagram | `Hexagram`, `QuarantineLog` | `NewHexagram`, `Action`, `Step`, `Forbid`, `Append`, `Verify` |
| Hardware | `HwProfile`, `HwMatrix`, `HwRelease`, `HwRegion` | `HwProbeHost`, `HwReference`, `Call`, `AddProvider`, `Attest`, `Build`, `Stage`, `Trial`, `Commit` |
| Treaty | `Treaty`, `TreatyResult`, `TreatyError` | `ParseTreatySutra`, `ParseTreatyRecord`, `Validate`, `Run`, `PicAdd`/`PicSub`/`PicMul`/`PicDiv` |
| CID / AIPI | `CID`, `AipiFunction`, `AipiClaim`, `AipiNode` | `ComputeCID`, `UnixFSCID`, `ParseCID`, `EncodeAipiCall`, `Put`, `Get`, `PublishFunction`, `Plan` |
| Coin | `TokenHolding`, `TokenWallet`, `Octane` | `CoinSplit`, `CoinFace`, `TokenFace`, `CanonicalHolding`, `Fuse`, `Split`, `EncodeTokenCodes`, `Mint`, `Transfer` |
| Fee | `FeeSplit`, `FeePolicy` | `AssuranceFee`, `AssuranceFeeCarry`, `SplitFee`, `FeePolicyDefault` |
| V-Bill hub | `VHub`, `VHubQuote`, `VHubSwap` | `VHubPegMessage`, `AddCurrency`, `SetPeg`, `AddLimit`, `Quote`, `Swap`, `Check` |
| Freight | `FreightHeader`, `FreightJoin`, `FreightOpsWriter` | `NewFreightHeader`, `FreightEncode`, `FreightDecode`, `FreightSplit`, `FreightOpsExpand`, `FreightGenRun` |
| Web 4 | `W4Agent`, `W4Directory`, `W4Manifest`, `W4ConsentToken`, `W4ConsentLog` | `NewW4Agent`, `VerifyW4Card`, `Sign`, `W4ActionDigest`, `IssueW4Consent`, `Check`, `Seal`, `Reply`, `VerifyW4Envelope` |
| Vino (1.6) | `VinoPlay`, `VinoMeter`, `VinoRequest`, `VinoRequestRecord`, `VinoRail`, `VinoLane` | `NewVinoPlay`, `Open`, `Back`, `Issue`, `Transfer`, `SetRail`, `Redeem`, `Vouchers`, `Encode`, `DecodeVinoRequest`, `SignVinoReceipt`, `VerifyVinoReceipt`, `Admit` |
| Paradox | `ParadoxBook`, `ParadoxPolicy`, `ParadoxHold`, `ParadoxGraph`, `RepairProof` | `Classify`, `Constructive`, `Destructive`, `Neutral`, `Special`, `Draft`, `Review`, `GraphPass`, `LogRecords` |

## Examples

```sh
cd sdk/go/zxv
go run ./examples/quickstart   # capabilities, ledger, rationals, Truth, ML-DSA-87
go run ./examples/budgets      # ConsumeWait, streamed budgeted generation, real-time phase clock
go doc -all .                  # the full reference; Example* functions run as tests
```

## Tests

```sh
cd sdk/go/zxv
go vet ./...
go test ./...          # add -race to check the concurrency claims
```

`hw_test.go` mirrors `test_zxv_abi14.c` (ABI 1.4): R1, profiles, the standard
engines, placement (MAC never goes REMOTE), proof, dependencies, failover,
shadow, memo, contracts, and a full stage / trial / commit / quarantine cycle
with events, the audit log and fail-closed when it fills (three SLH-DSA
signings, ~5 s).

`abi15_test.go` checks the ABI 1.5 keys (`status_abi15`, `treaty`, `cid`,
`aipi`, `coin`, `fee`, `vhub`, `freight`, `web4`, `paradox`) exactly,
refusals included, and covers capability refusals (a cap without AIPI,
MINT, PARADOX, CRYPTO_SIGN, ATTEST or LEDGER_POST), use after `Close`,
buffer growth, a failing and a panicking freight resolver, and fail-closed
handles after `Halt`.

`vino_test.go` replays the ABI 1.6 keys (`status_abi16`, `vino`: requests,
decode refusals, the fixed-seed receipt and its checks, the play scenario
with final accounts and vouchers, the meter) exactly, and covers error
classes, a cap without VINO or CRYPTO_SIGN, use after `Close`, the voucher
list buffer growth and fail-closed handles after `Halt`.

`hexagram_test.go` checks the `hex` key (ABI 1.3): every tag, the logic, step and
set tables, the Truth mapping and the quarantine chain.

`conformance_test.go` checks every other key of `sdk/conformance/vectors.json`
(status names and classes, rationals, the IEEE-754 boundary, Q16, the lpres
tables, UBH-168 frames byte for byte in pure Go and in C, hashes, Ed25519,
ML-DSA-87 and ML-KEM-1024 digests, swarm allotments, greedy and seeded
generation, EVM encoding). `behaviour_test.go` covers the fatal path
(`Halt`, both policies), capability denial and revocation, close ordering,
budget exhaustion and waiting, phase deferral with both drivers, streaming,
cancellation, budget-bound generation, the ledger and ISO data.

## Honest limits

- **ABI 1.5 groups.** No network transport anywhere: an `AipiNode` holds
  only what you put in it, freight packets and Web 4 envelopes are bytes
  you carry. The V-Bill hub is a demonstration with test currencies and an
  in-memory ledger, connected to no settlement rail. Policies that touch
  money or devices need the owner, and `ownerOK` is your assertion. Op
  streams promise no compression. Envelopes are signed, not encrypted. No
  treaty, fee or token result is legal, tax or compliance advice.

- **Least privilege, not a sandbox.** Capabilities stop mistakes, not
  hostile code in the same process: anyone can call `RootCapability`.
  Enforcement needs a process boundary.
- **Secrets.** `SecretKey` wipes its own off-heap buffer, but cannot wipe
  Go-heap bytes you passed to `SecretKeyFromBytes`, copies returned by
  `ExportBytesUnsafe` (or made internally by `SecretKey.Equal`, which
  wipes them after use), swapped memory or core dumps. Memory is not
  locked. The crypto is not FIPS 140-3 validated.
- **Fatal** with the default policy is an error value a program could
  ignore; it just can never make the context work again. Only `FatalExit`
  is terminal.
- **Lifetimes are run-time checks** in Go: a parent refuses to close
  while children are open, but the compiler cannot stop you from trying.
- **Wake-ups** come only from calls made through this SDK's `Budgets` and
  `PhaseClock` values (`Release`, `SetLimit`, `Advance`, `Cancel`).
- **Budget callbacks** (`OnEvent`, `OnExhausted`) run synchronously inside
  the call that caused them; one that calls back into libzxv can overwrite
  that call's error detail.
- **Not wrapped:** netting cycles, card issuing/authorization and VSS
  conformance checks (`NOTIMPL` in this library build). Card check digits
  are wrapped.
- **LoRA** is declared by the ABI but not built: `Model.LoadLoRA` returns
  `ErrNotImplemented` while `FeatLoRA` is off.
- **No network transport**: the ledger is in-process and nothing here is
  certified by any payment scheme.
- **cgo** is required; generation blocks an OS thread for its duration.
