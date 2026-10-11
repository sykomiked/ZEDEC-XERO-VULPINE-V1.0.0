<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# @zedec/zxv: the ZXV TypeScript SDK

One npm package, two backends, one typed API over `libzxv` (the C ABI in
`sdk/libzxv/include/zxv_api.h`):

| Backend | Where | How |
|---|---|---|
| `native` | Node.js servers | A Node-API addon (`native/zxv_addon.cc`) that opens `libzxv` at run time. Generation runs on a libuv worker thread and streams tokens back as an `AsyncIterable`; the event loop is never blocked. |
| `wasm` | Browsers, Workers, Deno, Bun, Node | `wasm/zxv.wasm`: libzxv and the kernel sources compiled to wasm32-wasi by this package (`scripts/build-wasm.mjs`). Single-threaded, strictly deterministic, no file system, no clock. |

Both backends produce the same bytes: the test suite runs every check on
both, and the greedy and seeded generations of `vectors.json` come out
token-for-token identical.

## Install

From this repository (Node 20.10+; Node 22 is what CI uses):

```sh
cd sdk/typescript
npm install            # also builds the addon with node-gyp (a failure falls back to wasm)
npm run build:wasm     # needs `python3 -m pip install ziglang` (zig ships wasi-libc), or ZXV_WASM_CC
npm run build:ts
npm test               # 102 tests, both backends
```

`sdk/libzxv/build/libzxv.so` must exist (built by `make -C sdk/libzxv`).
The addon finds the library in the order of SDK_DESIGN.md §1: the
`libraryPath` you pass, `ZXV_LIBRARY_PATH`, the bundled copy (the
repository build `../libzxv/build` when run from this tree, otherwise `lib/`
of the published package, staged by `npm pack`), then the system loader. A library whose ABI major is not 1, or whose minor
is below 3, is refused (the SDK calls the `zxv_hex_*` functions added in ABI 1.3 and the `_cap` functions added in 1.2).

In a published package, `import '@zedec/zxv'` resolves to the Node entry
(`node` condition) or the browser entry (`browser` condition, wasm only, no
Node built-ins). `@zedec/zxv/wasm` forces the browser entry and
`@zedec/zxv/zxv.wasm` is the module file for bundlers.

## Quickstart

```ts
import { Zxv, Ledger, AccountKind, Cap, Rational } from '@zedec/zxv';

const zxv = await Zxv.load();                       // native in Node, wasm in a browser
using ctx = zxv.createContext();
const root = ctx.rootCapability();                  // hand out root.attenuate(...) to the rest
ctx.enableCurrency(root, 'NGN');                    // needs Cap.LEDGER_POST
const ledger = Ledger.of(root);
const bank = ledger.openAccount('bank', 'NGN', AccountKind.ISSUER);
const ada = ledger.openAccount('ada', 'NGN');
ledger.post(bank, ada, 250_050n, 'NGN', 'E2E-1');   // exact kobo, as bigint
console.log(Ledger.of(root.attenuate(Cap.LEDGER_READ)).balance(ada).equity, Rational.parse('0.1').add('0.2').toString());
```

Runnable: `npm run build:ts && node dist/examples/quickstart.js [native|wasm]`
(exact numbers, capabilities, contradictions, ML-DSA-87, UBH-168, budgeted
generation) and `node dist/examples/abort-on-fatal.js [abort]`.

## Concepts in TypeScript terms

**No floats at the boundary.** No public method takes or returns a
fractional `number`. Integers that can pass 2^53 (amounts, balances, ticks,
nonces, seeds, entry and card ids) are `bigint`; small counts and ids are
integer `number`s. Every integer parameter accepts `bigint | number` and a
fractional number throws `FloatRejectedError` (a `ZxvUsageError`) naming
`Rational.fromNumberExact`. That method is the only float entry: it returns
the double's exact value (`fromNumberExact(0.1)` is
`3602879701896397/36028797018963968`), and NaN or infinity throw.

**`Rational`** is immutable with `bigint` parts, always reduced, and bounded
exactly like the kernel's `rat_t` (|num|, den <= 2^63 - 1; an overflowing
result throws ZXV_E_RANGE instead of rounding). Build it with
`Rational.of(3n, 4n)`, `Rational.parse('12.75')` or `Rational.fromQ16(q)`.
`toString()` never rounds (`"1/3"` stays `"1/3"`). The arithmetic runs in
TypeScript (so it works before a backend loads) and the tests cross-check it
against `zxv_rat_*` on random inputs, overflow included. `+r`, `r * 2` and
`r < 1` throw `TypeError`.

**`Truth`** is four frozen singletons, `Truth.NEITHER/TRUE/FALSE/BOTH`
(kernel numbers 0..3; compare with `===`). BOTH is a held contradiction, not
an error. There is no implicit boolean: use `isCertainlyTrue()`,
`toBoolean({ onBoth, onNeither })` (also a standalone `toBoolean(t, policy)`),
or `require()`. `and/or/not` are the kernel tables.

**Capabilities.** `ctx.rootCapability()` makes a root; `cap.attenuate(bits)`
makes a narrower child that dies with its parent; `revoke()` kills a whole
subtree; `setAttestation(Truth.BOTH)` blocks every gated call. Objects that
make gated calls hold their capability (`Ledger.of(cap)`,
`Budgets.open(cap)`, `PhaseClock.open(cap)`, `Model.open(cap, ...)`) and
present it on every call. Bits are bigint flags: `Cap.LEDGER_READ | Cap.EVM`.

Since ABI 1.2 every gated operation carries a capability:
`ctx.enableCurrency(cap, 'NGN')` and `ctx.registerPrivateUnit(cap, 'VFV', 2)`
need `LEDGER_POST`, as does `Ledger.openAccount`; `Ledger.account`,
`accountCount` and `journal` need `LEDGER_READ`;
`Iso20022Message.create(cap, kind)` needs `LEDGER_READ` and re-checks it on
every call; `Budgets.open`, `PhaseClock.open`, `Attestations.create` and
`Swarm.create` need `BUDGET`, `PHASE`, `ATTEST` and `SWARM`. A capability of
another context throws `CapabilityDeniedError` (`ZXV_E_CAP`). The forms
without a capability (`ctx.enableCurrency('NGN')`,
`Iso20022Message.create(ctx, kind)`) still work and are marked
`@deprecated`.

**Errors** map every negative status through `zxv_status_class`:

| Class | Type | |
|---|---|---|
| USAGE | `ZxvUsageError` (`FloatRejectedError`) | fix the call |
| REFUSED | `ZxvRefusedError`: `CapabilityDeniedError`, `BudgetExhaustedError`, `InsufficientFundsError`, `VerificationFailedError`, `NotImplementedError` | handle it |
| FATAL | `ZxvFatalError` (`fatal === true`, NOT a `ZxvError`) | the context is poisoned; never retry |
| SYSTEM | `ZxvSystemError` | environment |

Each carries `status`, `statusName` and `detail` (from `zxv_last_error`).
After a FATAL status every call on that context throws `ZxvFatalError`
without reaching C; only `close()` works. `createContext({ fatalPolicy:
'abort' })` calls `process.abort()` instead, the only uncatchable form.

**Lifetimes.** Every handle class has `close()` and `[Symbol.dispose]`
(`using`). Closing a context closes everything made from it, children
first. A generation in flight is cancelled and its C handles are destroyed
only after the worker thread returns.

**Secrets** (`SecretKey`: Ed25519 and ML-DSA secret keys, ML-KEM
decapsulation keys, shared secrets) live in addon-allocated native memory or
in wasm linear memory, never on the JS heap, and are wiped with
`zxv_secure_zero` on `close()` (or by a FinalizationRegistry backstop).
`exportBytesUnsafe()` is the only way to get a heap copy.

**Budgets.** `budget.consume(amount)` throws `BudgetExhaustedError` on
exhaustion (nothing consumed) and a usage error (ZXV_E_RMAG_INVALID) for a
negative or overflowing amount. `consumeAsync(amount, { signal, timeoutMs })`
waits while exhausted and retries whenever a `release` or a larger
`setLimit` through this SDK bumps the epoch. `onExhausted(cb)` reports
denials. `session.bindBudget(budget)` charges one unit per token.

**Phase ticks.** `PhaseClock` is logical time (one fundamental =
`PHASE_CYCLE` = 27,720 ticks). `defer(dueTick).awaitCleared()` and
`untilTick(t)` resolve when the clock passes the tick; drive it with
`advance(n)` (tests, simulations) or `startRealtime(ticksPerSecond)`.

**Engine.** `Model.open(cap, path, tokenizerPath?)` (or `Model.fromBytes`),
`model.newSession({ nCtx, kv })`, then `session.generate(prompt, options)`
or `for await (const tok of session.stream(prompt, options))`. Sampling
parameters are exact (`temperature: '0.7'` or a `Rational`), converted to
Q16.16 like `zxv_rat_to_q16`. `onToken` returning false, `signal`, or
`stream.cancel()` stop generation (stop reason `CANCELLED`). On the native
backend the worker waits for the event loop to accept each token, so a stop
from `onToken` takes effect exactly after that token, as on wasm.

**UBH-168** framing is pure TypeScript (`Ubh168Frame.pack/unpack/idle/tag`,
DataView with the fixed little/big/little/big/little word order), checked
byte for byte against `vectors.json` and the C library; `zxv.wire.resolve`
runs the C W5 resolver.

## The hexagram tag (ABI 1.3)

A `Hexagram` tags one unit of work with a four-valued base, the polarity
of its evidence (entropic, neutral, forbidden, negentropic) and its phase
(potential -> emerging -> manifest -> dissolving), packed into one byte
0..63. Its action is proceed, wait, refuse, escalate or rollback. Every rule
comes from libzxv and is tested against `vectors.json` key `hex`.
Forbidden is absorbing, a phase moves one step at a time, and the base is
**not** numbered like `Truth` (TRUE is base 3 but Truth 1): convert only
with the explicit `toTruth()` / `Hexagram.fromTruth(...)`. Combining BOTH with NEITHER follows
the `Truth` tables (AND gives NEITHER, OR gives BOTH); those two results
are pending an owner decision. A `QuarantineLog` (needs the ATTEST
capability) records forbidden work in a SHA-256 hash chain; when it is
full, append is refused with `ZXV_E_CAPACITY` and you must halt the
context. See [ABI.md, Changes in 1.3](../docs/ABI.md#changes-in-13).

```ts
import { Hexagram, HexBase, HexPolarity, HexPhase, HexAction, QuarantineLog } from '@zedec/zxv';

let h = Hexagram.of(HexBase.TRUE, HexPolarity.NEGENTROPIC, HexPhase.POTENTIAL);
h.action === HexAction.PROCEED;           // true
h = h.advance();                          // EMERGING
h.step(HexPhase.DISSOLVING);              // ZxvRefusedError (ZXV_E_STATE): a jump
const bad = h.forbid();                   // ROLLBACK, forever
using log = QuarantineLog.create(cap, 1024);   // needs Cap.ATTEST
const seq = log.append(bad, 7, 42, digest32);  // bigint
log.verify();
```

`Hexagram` is pure TypeScript (the 64 frozen values in `Hexagram.all`), so
it also works in the browser before the wasm module loads; its tests
compare every result with `zxv_hex_*` on both backends. `valueOf()` throws,
so `h + 1` or `h < g` never coerce silently. `QuarantineLog` uses the
native or wasm backend.

## API overview

| Area | API |
|---|---|
| Library | `Zxv.load({ backend, libraryPath, wasm })`, `zxv.version`, `zxv.has('TENSOR')`, `zxv.features()`, `statusName`, `statusClass`, `random`, `secretKey`, `iso4217`, `kernel` (C rationals/logic), `wire` (C framing) |
| Context | `zxv.createContext({ fatalPolicy })`, `rootCapability`, `enableCurrency(cap, alpha[, minorUnits])`, `registerPrivateUnit(cap, code, minorUnits)`, `currency`, `enabledCurrencies`, `halt`, `isPoisoned`, `close` |
| Capability | `Cap.*`, `attenuate`, `revoke`, `setAttestation`, `info`, `check`, `require` |
| Exact numbers | `Rational.of/parse/fromNumberExact/fromIeee754Bits/fromQ16`, `add/sub/mul/div/cmp/equals/toQ16/toString` |
| Logic | `Truth.*`, `and/or/not`, `fromEvidence`, `toBoolean(policy)`, `require` |
| Ledger | `Ledger.of(cap)`: `openAccount`, `post`, `balance`, `journal`, `account`, `check` |
| ISO 20022 | `Iso20022Message.create(cap, MxKind.PACS008)`: `setText`, `setAmount`, `addEntry`, `render`; `NettingCycle`, `Cards`, `runVss` (pending kernel modules) |
| Attestations | `Attestations.create(cap)`: `attest`, `evidence`, `state`, `find`, `gate` |
| Budgets | `Budgets.open(cap).create(name, limit)`: `consume`, `consumeAsync`, `release`, `setLimit`, `status`, `onExhausted` |
| Phase | `PhaseClock.open(cap)`: `now`, `advance`, `admit`, `setHealth`, `defer` -> `DeferredEvent.awaitCleared/cancel`, `untilTick`, `nextDue`, `startRealtime` |
| Swarm | `Swarm.create(cap, { levels, tokensPerCycle })`: `register`, `setMood`, `setFeeling`, `beginCycle`, `consume`, `allotment`, `endCycle`; `zxv.swarm.levelCapacity/emotionProfile` |
| Crypto | `Hashes` (`zxv.hashes.sha3_256/shake256/keccak256`), `Ed25519`, `MlDsa87`, `MlKem1024`, `SecretKey` |
| Engine | `Model.open/fromBytes`, `info`, `tokenize`, `detokenize`, `newSession`, `loadLora`; `Session.generate/stream/reset/position/bindBudget` |
| EVM | `new StateChannel(cap, channelId)`: `encodeState`, `ledgerState` |
| Wire | `Ubh168Frame`, `WireTruth` |

## Examples

Post-quantum signing with an off-heap key:

```ts
const { publicKey, secretKey } = MlDsa87.generate(cap);
using sk = secretKey;
const sig = MlDsa87.sign(cap, sk, 'agent message', { context: 'zxv' });
MlDsa87.verify(cap, publicKey, 'agent message', sig, { context: 'zxv' }); // true
```

Waiting on a budget:

```ts
using budgets = Budgets.open(cap);
const gpu = budgets.create('gpu-seconds', 10);
await gpu.consumeAsync(Rational.of(5, 2), { timeoutMs: 30_000 }); // waits for gpu.release(...) if exhausted
```

Streaming deterministic generation:

```ts
using model = await Model.open(cap, 'tiny-qwen2.gguf', 'tiny-tokenizer.gguf');
using session = model.newSession({ nCtx: 128 });
for await (const tok of session.stream(prompt, { maxTokens: 12, temperature: '0.7', topK: 40, seed: 42n })) {
  process.stdout.write(tok.text);
}
```

In a browser, inside a Web Worker:

```ts
import { Zxv, Model, Cap } from '@zedec/zxv';            // resolves to the wasm entry
const zxv = await Zxv.load();                             // fetches zxv.wasm next to the package
const ctx = zxv.createContext();
const model = await Model.open(ctx.rootCapability({ caps: Cap.AI_INFERENCE }), '/models/tiny-qwen2.gguf');
```

## Building the wasm module

`npm run build:wasm` compiles `../libzxv/src/*.c` plus the kernel sources
that `sdk/libzxv/Makefile` lists (read with `make -s print-config`, which
only prints) into `build/wasm/`, links `wasm/zxv.wasm` as a WASI reactor,
and never writes into `sdk/libzxv`. The compiler is `python3 -m ziglang cc
-target wasm32-wasi` or `$ZXV_WASM_CC` (e.g. wasi-sdk's clang). The full
engine is included (2.2 MB module); `ZXV_WASM_NO_TENSOR=1` leaves it out.
This SDK adds one C file, `wasm/zxv_wasm_shim.c`: single-threaded pthread
mutex stubs, `arc4random_buf` over WASI `random_get`, and a
`zxvjs_generate` that forwards tokens to JavaScript. The JS host provides a
minimal WASI: `random_get` is `crypto.getRandomValues`, stdout/stderr go to
the console, and there is no file system and no clock.

## Honest limits

- **The wasm build is single-threaded.** Generation runs synchronously on
  the calling thread and blocks it: in a page, use a Web Worker. Breaking out
  of `stream()` cannot interrupt a wasm run (it has already finished); stop
  it from `onToken` or with an `AbortSignal` aborted inside `onToken`.
- **Capabilities are least privilege, not a sandbox.** Any code in the
  process can create its own root capability; enforcement needs a process
  boundary.
- **Secrets.** Bytes you pass in from the JS heap (seeds, `fromBytes`,
  messages) stay on the heap until the GC reclaims them, and the GC may
  have copied them. wasm linear memory is an `ArrayBuffer` that any code
  holding the instance can read, and growing it may leave an old copy in
  freed host memory. Neither backend can prevent swap or core dumps.
  `exportBytesUnsafe()` copies defeat wiping. The crypto is not FIPS 140-3
  validated.
- **`if (truth)` is always taken** (every `Truth` is an object); TypeScript
  cannot forbid it. Use the explicit methods.
- **`ZxvUsageError` does not extend `TypeError`/`RangeError`**: JS has single
  inheritance and the SDK hierarchy comes first.
- **`consumeAsync` wakes on replenishment through this SDK** (`release`,
  `setLimit`); a replenishment made by other code straight through C is seen
  only at the next retry.
- **`fatalPolicy: 'abort'`** ends a Node process; a browser page cannot be
  aborted, so there it throws `ZxvFatalError` like `'throw'`.
- **Not built yet in libzxv:** LoRA (`loadLora` throws
  `NotImplementedError`; `ZXV_FEAT_LORA` is off), netting, card issuing,
  VSS and the pacs.002/004/009, camt.056/029 builders (NotImplementedError
  until the kernel modules land). There is no network transport: no PAPSS,
  SWIFT, CIPS or RTGS connectivity, and nothing is certified by any scheme.
- **Native addon**: one `libzxv` per process (a second `libraryPath` is
  refused). The addon must be built for the Node ABI in use (node-gyp).
- Handles that are never closed are not freed by the GC (only `SecretKey`
  has a finalizer): use `using` or `close()`.
