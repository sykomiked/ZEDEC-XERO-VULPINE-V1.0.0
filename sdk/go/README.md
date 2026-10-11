<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# ZXV Go SDK

Package `zxv` is a cgo binding to `libzxv`, the stable C ABI of the ZXV
platform (`sdk/libzxv/include/zxv_api.h`, ABI 1.3).

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

## API overview

| Area | Types | Key calls |
|---|---|---|
| Library | `Version`, `LiveFeatures` | ABI version, feature bits |
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
