<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# ZXV SDK design rules

Every ZXV SDK wraps the same C library, `sdk/libzxv` (header
`sdk/libzxv/include/zxv_api.h`). These rules make the SDKs feel like one
product in many languages. A developer who knows one SDK should be able to
guess the names in another.

For task-focused walk-throughs with tested examples, see the [guides](guides/README.md).

## 1. One library, many languages

- The only native dependency is `libzxv` (`libzxv.so`, `libzxv.dylib`,
  `zxv.dll`). SDKs never include kernel headers.
- Each SDK finds the library in this order: an explicit path the developer
  passes, the `ZXV_LIBRARY_PATH` environment variable, the copy bundled
  with the package, then the system loader path.
- Each SDK checks `zxv_version` at load time and refuses a library whose
  major version differs, or whose minor version is below 3 (ABI 1.3 added
  the hexagram tag every SDK now binds; 1.2 the capability-gated `*_cap`
  forms).

## 2. Names

Use the host language's conventions, with the same nouns everywhere:

| Concept | Type name | Notes |
|---|---|---|
| Library entry point | `Zxv` (static) / module `zxv` | version, features |
| Context | `ZxvContext` (or `Context` inside a `zxv` namespace) | owns the ledger |
| Capability | `Capability` | `attenuate`, `revoke`, `check` |
| Exact rational | `Rational` | immutable value type |
| Four-valued logic | `Truth` | `NEITHER`, `TRUE`, `FALSE`, `BOTH` |
| Hexagram tag | `Hexagram` (+ `HexBase`, `HexPolarity`, `HexPhase`, `HexAction`) | `action`, `step`, `advance`, `and`/`or`/`not`, `toTruth` |
| Quarantine log | `QuarantineLog` | `append`, `count`, `head`, `record`, `verify` |
| Ledger | `Ledger` | accounts, `post`, `balance`, `check` |
| Attestations | `Attestations` | `attest`, `evidence`, `gate` |
| Budgets | `Budgets` / `Budget` | `consume`, `consumeAsync`, `release` |
| Phase clock | `PhaseClock` | `now`, `advance`, `defer`, `untilTick` |
| Wire frame | `Ubh168Frame` | pure host code + C cross-check |
| Swarm economy | `Swarm` | `register`, `beginCycle`, `consume` |
| Crypto | `MlKem1024`, `MlDsa87`, `Ed25519`, `Hashes` | |
| Secret keys | `SecretKey` | off-heap, wiped on close |
| Engine | `Model`, `Session` | `generate`, `generateAsync`/stream |
| EVM | `StateChannel` | `encodeState`, `ledgerState` |
| Errors | `ZxvException` family | see section 5 |

## 3. Exactness: no floats at the boundary

- No public SDK method takes or returns `float`/`double` (or the host
  equivalent: JS non-integer `number`, Python `float`).
- `Rational` is constructed from integers (`Rational.of(3, 4)`), from exact
  text (`Rational.parse("12.75")`), or from the host decimal type when it is
  exact (Java `BigDecimal`, C# `decimal`, Python `Decimal`/`Fraction`).
- A host float is accepted in exactly one place, a method named so nobody
  calls it by accident: `Rational.fromDoubleExact(x)`. It passes the raw
  IEEE-754 bits to `zxv_rat_from_ieee754`, which returns the double's exact
  value (0.1 becomes 3602879701896397/36028797018963968). NaN and infinity
  raise `FloatRejectedException`.
- Where a dynamic language could slip a float in (Python, Ruby, PHP, JS),
  the SDK checks argument types at runtime and raises
  `FloatRejectedException` with a message naming `fromDoubleExact`.
- Engine sampling parameters are Q16.16 integers; SDKs take a `Rational`
  and convert with `zxv_rat_to_q16`.

## 4. Four-valued logic

- `Truth` is a closed enum with exactly four values, numbered as the kernel
  numbers them: `NEITHER = 0`, `TRUE = 1`, `FALSE = 2`, `BOTH = 3`.
- It has no implicit conversion to the host boolean. Converting needs an
  explicit policy: `truth.isCertainlyTrue()`, or
  `truth.toBoolean(onBoth, onNeither)`, or a `require...()` that raises.
  "Both" is a held contradiction, not an error and not null.
- `and`, `or`, `not` follow the C tables (`vectors.json` key `lpres`).
- Keep `Truth` a value type with room for one more accessor: the kernel is
  adding an orthogonal 2-bit `mode` beside the four states. Do not encode
  `Truth` in a way that would break if a `mode` field is added later.

### The hexagram tag (ABI 1.3)

- `Hexagram` is an immutable value holding one tag byte (0..63) with
  accessors `base`, `polarity`, `phase`, `isForbidden`, `action`, `name`
  and `tag`. The four field types are closed enums numbered as the
  `ZXV_HEX_*` constants. Construct from fields, from a tag (63 < tag is a
  USAGE error), or from a `Truth`.
- Every rule comes from libzxv (`zxv_hex_*`) or, where an SDK keeps a pure
  host copy (TypeScript, for its Wasm and browser builds), is checked
  against libzxv and `vectors.json` key `hex`.
- `step(phase)` returns a new value or raises REFUSED (`ZXV_E_STATE`) for a
  jump; `advance()` takes the next step and cannot fail. `withState(base,
  polarity)` refuses to un-forbid a forbidden tag. `forbid()` is absorbing.
- `and`, `or`, `not` (operators where the language has them) follow the
  libzxv tables. BOTH AND NEITHER = NEITHER and BOTH OR NEITHER = BOTH are
  pending an owner decision; SDKs must not hard-code them outside a pure
  host table that the conformance test checks.
- The base is NOT numbered like `Truth` (TRUE is base 3 but Truth 1). The
  only conversions are the explicit `toTruth()` and `fromTruth(...)`; no
  implicit or numeric coercion, and no conversion to the host boolean.
- `QuarantineLog(cap, capacity)` needs `ATTEST`. A full log raises REFUSED
  (`ZXV_E_CAPACITY`) and the caller must halt the context; `verify()`
  raises FATAL on a broken chain. Records expose `seq`, `tag`, `reason`,
  `submitter`, `subject`, `prev` and `hash`, so the SHA-256 chain can be
  recomputed outside the library.

## 5. Errors

Map every negative status through `zxv_status_class`:

| Class | Base type | Meaning |
|---|---|---|
| USAGE | `ZxvUsageException` (subclass of the host's argument error where idiomatic) | caller bug |
| REFUSED | `ZxvRefusedException` with subclasses `CapabilityDeniedException`, `BudgetExhaustedException`, `InsufficientFundsException`, `VerificationFailedException`, `NotImplementedException` | handle it |
| FATAL | `ZxvFatalError` | context halted; never retry |
| SYSTEM | `ZxvSystemException` | environment |

Every exception carries `status` (the int), `statusName`, and the detail
text from `zxv_last_error`.

### Fail-closed (FATAL)

A C library cannot make a host exception uncatchable. The strongest real
equivalent, used by every SDK:

1. The context is poisoned in C: every later call on it returns
   `ZXV_E_POISONED`, so a retry cannot succeed, whatever the host does.
2. The SDK marks its own handle dead and raises its terminal type, placed
   outside the ordinary exception hierarchy where the language allows:
   Java `ZxvFatalError extends Error`; Python `ZxvFatal(BaseException)`;
   C# `ZxvFatalException` (not derived from the SDK's other exceptions);
   Rust `Error::Fatal` (and the handle refuses further calls); Go
   `ErrFatal`; JS `ZxvFatalError` with `fatal = true`.
3. An opt-in policy `FatalPolicy.ABORT` terminates the process
   immediately (Java `Runtime.halt`, C# `Environment.FailFast`, Python
   `os.abort`, Rust `std::process::abort`, Go `os.Exit(70)` after logging,
   Node `process.abort`). This is the only truly uncatchable form.

Test it with `zxv_ctx_halt`.

## 6. Capabilities on every call

- Every object that makes gated calls holds a `Capability` and passes it on
  every call. The developer creates a root capability from a context and
  hands narrower ones (`attenuate`) to the parts of the program that need
  less. There is no global "current capability".
- Document the honest limit: in one process this is least privilege, not
  a sandbox.

## 7. Secrets

- Secret keys (ML-KEM decapsulation keys, ML-DSA and Ed25519 secret keys,
  shared secrets) live in memory the SDK allocates outside the garbage
  collected heap where the language allows (Java `Arena`/`MemorySegment`,
  C# `NativeMemory`, Python a cffi buffer, Go `C.malloc`, Rust a
  `Zeroizing` box, JS Wasm/addon memory) and are wiped with
  `zxv_secure_zero` on `close()`/`Dispose()`/`drop`, and by a
  cleaner/finalizer as a backstop.
- `SecretKey` never exposes a heap copy except through an explicitly named
  method (`exportBytesUnsafe()`), documented as defeating the wiping.
- Say plainly in each README what cannot be guaranteed (a GC may have
  copied data that the developer passed in from the heap; swap; core dumps).

## 8. Budgets and async

- `Budget.consume(amount)` raises `BudgetExhaustedException` (REFUSED) on
  `ZXV_E_BUDGET` and a USAGE error on `ZXV_E_RMAG_INVALID` (never retry).
- `Budget.consumeAsync(amount)` returns the host's future/promise/awaitable.
  It completes when the consume succeeds; on exhaustion it waits until the
  budget is replenished (a `release` or a larger limit through this SDK
  bumps the epoch reported by `zxv_rmag_status`) and retries; it fails
  immediately on INVALID; it supports cancellation and an optional timeout.
- `onExhausted(callback)` registers a host callback for denied consumes.

## 9. Phase ticks

- `PhaseClock` wraps `zxv_phase`. ZXV time is a tick count; one fundamental
  is 27,720 ticks.
- `PhaseClock.defer(dueTick)` returns a `DeferredEvent` whose
  `awaitCleared()` (future/promise/awaitable) completes when the clock
  passes its tick.
- Two drivers: manual (`advance(n)`, for deterministic tests and
  simulations) and real time (`startRealtime(ticksPerSecond)`, a host timer
  or event-loop task that advances the clock and completes waiters, then
  yields back to the host loop).

## 10. Engine

- `Model.open(cap, path[, tokenizerPath])`, `model.newSession(nCtx, kv)`,
  `session.generate(prompt, GenerateOptions)` returning a result with the
  text, token ids and the stop reason, plus a streaming form (callback,
  iterator or async stream as the language prefers).
- Python releases the GIL during generation (cffi does this for every
  call). Node runs generation off the main thread.
- LoRA: expose `Model.loadLora` that raises `NotImplementedException`
  while `ZXV_FEAT_LORA` is off, and say so in the README.

## 11. Wire framing in pure host code

Network-facing SDKs implement UBH-168 packing in the host language (so a
service can frame without the native library) and test it byte for byte
against `vectors.json` key `ubh168` and the C library.

## 12. Tests and docs

- Every SDK tests against `sdk/conformance/vectors.json` and the fixtures
  written by `make -C sdk/libzxv fixtures` (`sdk/conformance/fixtures/`).
- Every SDK has a README with: install, a 10-line quickstart, the concepts
  above in that language's terms, an API overview table, examples, and an
  "Honest limits" section.
