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
npm test               # 157 tests, both backends
```

`sdk/libzxv/build/libzxv.so` must exist (built by `make -C sdk/libzxv`).
The addon finds the library in the order of SDK_DESIGN.md §1: the
`libraryPath` you pass, `ZXV_LIBRARY_PATH`, the bundled copy (the
repository build `../libzxv/build` when run from this tree, otherwise `lib/`
of the published package, staged by `npm pack`), then the system loader. A library whose ABI major is not 1, or whose minor
is below 5, is refused (the SDK calls the ABI 1.5 groups, the `zxv_hw_*` functions added in ABI 1.4, the `zxv_hex_*` functions added in 1.3 and the `_cap` functions added in 1.2).

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

## Hardware emulation and hardware updates (ABI 1.4)

`src/hardware.ts` wraps the 55 `zxv_hw_*` functions on both backends; the
concepts are in [HW_EMULATION.md](../docs/HW_EMULATION.md) and the profile
format in [HW_PROFILE.md](../docs/HW_PROFILE.md).

- **Profiles** (`HwProfile`, plain data, no capability):
  `HwProfile.probeHost(zxv)`, `HwProfile.reference(zxv)` (R1, 16 nodes),
  `HwProfile.present(host, r1)`, `match`, `pair`, `encode` / `decode` /
  `digest`, and an opaque per-node extension (`setExt` / `getExt`). Under
  wasm the "host" is a wasm32 machine, so R1's CPU is emulated too.
- **The matrix** (`HwMatrix.create(cap)`, needs `Cap.HW_MATRIX`): the
  standard engines (`HwFn.SHA256`, `KECCAK`, `MAC`, `GCD`, `LOGIC64`,
  `TENSOR`) answer on any device; `providers()`, `functionInfo()` and
  `bound()` are discovery; `setPolicy` states placement (`KEY_BEARING`
  work never leaves the machine and is never memoized). `addProvider(fn,
  res, kind, cost, (op, input) => output)` adds a JS implementation that
  serves only after `attest()` reproduces the golden vectors.
- **Packages** (`HwReleaseKey.generate(cap)`, needs `Cap.CRYPTO_SIGN`):
  ML-DSA-87 **and** SLH-DSA-SHAKE-256s signatures; both secret halves are
  off-heap `SecretKey`s wiped on `close()` / `using`. `key.build(...)`
  signs (seconds: a release step). `encodeVectors` builds a VECTORS
  payload for `HwMatrix.applyContract` (needs `Cap.HW_RECONF`).
- **Regions** (`HwRegion.create(cap, device, options)`, needs
  `Cap.HW_RECONF`): a JS `device` (`load`, `readback` -> SHA-256 of the
  slot, `activate`) receives images through `stage` -> `trial` -> `commit`
  (or `rollback`, `tick`); every step is an event (`onEvent`) and a record
  in a hash-chained audit log (`log`, `logEntry`). A full log poisons the
  context (fail closed).

`Cap.ALL` keeps its 1.1 value; use `Cap.ALL_1_4` (or add `Cap.HW_MATRIX |
Cap.HW_RECONF`). JS implementations and devices are called synchronously
on the calling thread through C trampolines (the addon's `zxvjs_hw_*`
entry points; the shim's under wasm). Throwing a `ZxvError` (or returning
a negative status) fails the C step with that status; any other exception
fails it with `ZXV_E_IO` and is re-thrown from the SDK call. The new
statuses are `Status.E_NOPROVIDER` (-25) through `Status.E_POLICY` (-31).

```ts
import { Cap, HwFn, HwImageKind, HwMatrix, HwProfile, HwRegion, HwReleaseKey } from '@zedec/zxv';

using r1 = HwProfile.reference(zxv);
const [native, emulated] = HwProfile.match(HwProfile.probeHost(zxv), r1);
const cap = ctx.rootCapability({ caps: Cap.ALL_1_4 });
using mx = HwMatrix.create(cap);
mx.call(HwFn.SHA256, new TextEncoder().encode('abc'));   // the proven SOFT engine
using key = HwReleaseKey.generate(cap);
const image = key.build(cap, HwImageKind.PARTIAL, { targetClass: 0xd0, fnId: 0x200, version: 1, svn: 1, payload });
const region = HwRegion.create(cap, device, { regionId: 1, targetClass: 0xd0, matrix: mx, provider: fabric });
region.onEvent((e) => console.log(e));
region.trustKey(key.publicKey);
region.stage(image); region.trial(5); region.commit();
```

## ABI 1.5: treaties, CIDs and AIPI, coins, fees, the V-Bill hub, freight, Web 4, paradox

ABI 1.5 adds eight groups (197 functions) on both backends. They report
through a second feature mask: `zxv.features2()` lists the live ones,
`zxv.featureMask2` is the raw `ZXV_FEAT2_*` mask, and `zxv.has()` accepts
names from both masks (`zxv.has('FREIGHT')`). There are three new
capabilities, `Cap.AIPI`, `Cap.MINT` and `Cap.PARADOX`. `Cap.ALL` keeps its
1.1 value, so use `Cap.ALL_1_5` or add the new bits yourself. A capability
without the bit gets `CapabilityDeniedError`. Each new status has its own
error class:

| Status | Class |
|---|---|
| `E_DENOM` (-32) | `DenominationError` (usage) |
| `E_TREATY` (-33) | `TreatyError` (usage; carries `fault` and `offset`) |
| `E_CID` (-34) | `CidMismatchError` |
| `E_CONSENT` (-35) | `ConsentDeniedError` |
| `E_ERASURE` (-36) | `ErasureError` |
| `E_PEG` (-37) | `PegError` |
| `E_LIMIT` (-38) | `LimitExceededError` |
| `E_PARADOX_STEP` (-39) | `ParadoxStepLimitError` |
| `E_PARADOX_OWNER` (-40) | `OwnerApprovalRequiredError` |

Every class without "(usage)" is refused (`ZxvRefusedError`). Every 64-bit
value is a `bigint`. You never size a buffer: results of unknown length
come back as a `Uint8Array` or a string, sized exactly. Handles follow R6:
they close before their context, closing twice is fine, and a call after
`close()` throws `ZxvUsageError`. Handles bound to a context throw
`ZxvFatalError` once that context halts. The examples below assume
`const cap = ctx.rootCapability({ caps: Cap.ALL_1_5 })`.

### Treaties (`zxv.treaty`)

COBOL copybook records (2542 bytes) and Sutra text, with exact PIC S9(18)
arithmetic. A parse refusal throws `TreatyError` and gives the `TreatyFault`
reason and the byte offset of the problem.

```ts
using t = zxv.treaty.parseSutra('TREATY CORRIDOR-7 USD NET\nPARTY ACME 5000 4000 1/2\nPARTY BETA 3000 3000 1/2\nOWE ACME BETA 1200\nEND\n');
using result = t.run();
result.legs();                 // [{ from: 0, to: 1, amount: 1200n }]
zxv.treaty.picDiv(-7n, 2n);    // { quotient: -3n, remainder: -1n }
```

Limits: this module only parses and nets. It moves no money and does not
check a treaty against any law.

### CIDs and AIPI (`zxv.cid`, `zxv.aipi`, `AipiNode`)

IPFS CIDs (v0 and v1, Kubo's text form and base58btc) and AIPI function,
call and claim records. Records are decoded strictly, so one function has
exactly one CID. `AipiNode.create(cap)` needs `Cap.AIPI` and creates an
in-memory block store whose `get` checks every block against its CID.

```ts
const cid = zxv.cid.compute(new TextEncoder().encode('hello world'));
cid.toString();   // 'bafkrei...'
using node = AipiNode.create(cap);
const module = node.put(new TextEncoder().encode('zxv module bytes'));
const fn = node.publishFunction(zxv.aipi.encodeFunction({ intent: AipiIntent.FN, verify: AipiVerify.LOCAL, fuelMax: 1000n, module, entry: 'main' }));
zxv.aipi.callCid(fn, new TextEncoder().encode('{"n":42}'));
```

Limits: there is no network (no peers, gateway or libp2p) and no
EXECUTE, because running a function needs the Z-Wasm sandbox, which is not
in libzxv. Signing a claim is the caller's job.

### Coins and tokens (`zxv.coins`, `zxv.tokens`, `TokenWallet`)

The VFV Fibonacci coin ladder (face 4 is not a coin: `DenominationError`),
Zeckendorf splits, and swarm token tiers with octane, including canonical
form, fuse/split and Fibonacci codes. A holding is a frozen array of 64
bigints. `TokenWallet` needs `Cap.MINT`.

```ts
zxv.coins.split(100n);                  // [10, 5, 3] (rungs: 89 + 8 + 3)
using w = TokenWallet.create(cap, { octane: 4 });
w.mint(100n); w.spend(30n);
w.info();                               // { value: 70n, pieces: 10n, octane: 4, modelId: 0 }
```

Limits: minting real coins is not in this ABI. A wallet is in-process
bookkeeping.

### The assurance fee (`zxv.fees`)

The fee is floor(gross × 8889 / 10^7), which is 0.08889%, exact for every
u64. A carry makes many small charges pay exactly what one charge of their
sum would pay. The fee splits 50/25/15/10 into four buckets, and the
reserve floor takes every remainder unit.

```ts
zxv.fees.assure(1_000_000n);                    // 888n
zxv.fees.split(889n).parts;                     // [446n, 222n, 133n, 88n]
let rem = 0n;
for (let i = 0; i < 10; i++) rem = zxv.fees.assureWithCarry(rem, 100n).remainder;
```

Limits: these are the arithmetic rules only. How the fee is treated for tax
or under securities law is a question for counsel.

### The V-Bill hub (`VHub`)

A demonstration hub with test currencies. Each currency has one exact peg,
signed by its issuer with ML-DSA-87. A swap is a single all-or-nothing
posting (A → V-Bill → B), and a quote returns the largest exact amount. A
currency with no rule refuses every swap, so add a limit first.
`LimitExceededError` and `PegError` are refusals.

```ts
const iss = MlDsa87.fromSeed(cap, new Uint8Array(32).fill(0x40));
using hub = VHub.create(cap);
hub.addCurrency(0, { code: 'ZTA', minorUnits: 2, issuerPublicKey: iss.publicKey, poolFunding: 500000n, vbillBacking: 10000n });
hub.setPeg(0, 350n, 1n, 1n, MlDsa87.sign(cap, iss.secretKey, hub.pegMessage(0, 350n, 1n, 1n)));
// ...a second currency, hub.addLimit(slot, limit), hub.openAccount, hub.issue...
hub.quote(0, 1, 35001n);   // { amount: 35000n, vbills: 100n, received: 12000n }
```

Limits: test currencies only (ISO 4217 codes are refused) and no real
money. The ledger lives in process memory. The hub is connected to no rail,
central bank or issuer, and V-Bills likely need legal review before any
real use. The kernel's C rule plug-ins are not exposed.

### Freight (`zxv.freight`)

A payload of up to 3360 bytes is erasure-coded as 168 rows of 21 bytes,
and any k rows rebuild it. Larger payloads are split into sets
(`members`, `join`). Op streams cover LITERAL, COPY, SEED and CIDREF. A
CIDREF is resolved by a JS callback. The callback runs synchronously, and
any exception it throws is caught at the C boundary and re-thrown after the
call.

```ts
const header = zxv.freight.header(payload, { freightId: 42n, k: 10 });
const packets = zxv.freight.encode(header, payload);            // 3528 bytes
zxv.freight.decode(header, packets.subarray(21 * 100, 21 * 110)); // any 10 rows
const abc = zxv.cid.compute(new TextEncoder().encode('abc'));
using w = zxv.freight.opsWriter(5n);
const ops = w.literal(lt).cidref(abc, 3n).literal(gt).finish();
zxv.freight.expand(ops, { limit: 1024n, resolve: (cid, len) => (zxv.cid.fromBytes(cid).equals(abc) ? abcBytes : undefined) });
```

Limits: erasure coding recovers a lost row, but a corrupted row is only
detected (`VerificationFailedError`) and must be dropped. The header is not
authenticated. Op streams give no size guarantee.

### Web 4 agents (`W4Agent`, `zxv.web4`)

ML-DSA-65 agent keys are generated inside a `W4Agent` handle. The SDK
cannot read the secret key: no call returns it, the object never holds it,
`toString()` and `toJSON()` show only the agent id, and `close()` wipes the
key. Creating keys and signing need `Cap.CRYPTO_SIGN`. A consent token
needs `Cap.ATTEST`. Cards, manifests, the directory, the consent log and
envelopes are covered too. No call reads a clock: every time is `nowMs`,
which you pass.

```ts
using alice = W4Agent.create(cap, { name: 'alice', nowMs });
using human = W4Agent.create(cap, { name: 'human', nowMs });
const action = zxv.web4.actionDigest('pay', alice.agentId, bob.agentId, 7n, payload);
const token = human.issueConsent({ agentId: alice.agentId, action, scope: W4ConsentScope.MONEY, maxVfv: 1000n, issuedMs: nowMs, expiresMs: nowMs + 600_000n });
const env = alice.seal({ kind: W4MessageKind.REQUEST, to: bob.agentId, reqId: 7n, tool: 'pay', payload, consent: token, nowMs });
zxv.web4.verifyEnvelope(env, alice.publicKey);
using log = zxv.web4.consentLog(1024);
log.check(zxv.web4.envelopeConsent(env)!, human.publicKey, alice.agentId, action, W4ConsentScope.MONEY, 500n, nowMs + 100n);
// a second check of the same token throws ConsentDeniedError (single use)
```

Limits: envelopes are signed, not encrypted. A consent token proves only
that a key holder signed. Whether that person was the right human and
understood what they signed depends on the device and UI that showed the
action. Replay windows, rate limits and the Web 2/Web 3 bridges are not in
this ABI.

### The paradox engine (`zxv.paradox`, `ParadoxBook`)

The engine reads a hexagram tag as one of four kinds, then runs a fixed,
logged operator for that kind: constructive (a bounded policy update),
destructive (contain, quarantine, roll back and produce a `RepairProof`),
neutral (hold every branch) or special (a frontier record plus a draft for
review). Complex paradoxes are graphs resolved pass by pass. Every handle
needs `Cap.PARADOX`. An outcome such as `PROPOSAL_FOR_OWNER` is a decision,
not an error.

```ts
using book = ParadoxBook.create(cap, { logCapacity: 64, registryCapacity: 16 });
const cls = book.classify(39, { evidence: [3, 1, 5, 1] });   // { kind: CONSTRUCTIVE, tag: 47, registryIndex: -1 }
using policy = ParadoxPolicy.create(cap, { lo: [0n, 0n], hi: [100n, 50n], initial: [50n, 25n] });
book.constructive(policy, cls, [60n, 20n], 5);    // ParadoxOutcome.APPLIED
book.constructive(policy, cls, [200n, 20n], 5);   // PROPOSAL_FOR_OWNER: outside the bounds, nothing changes
```

Limits: this is bookkeeping that tolerates contradictions. It contains,
holds and routes them, and never makes both sides true. Automatic changes
happen only inside published bounds. Anything flagged MONEY or DEVICE goes
to the owner, and `ownerOk` is the caller's own assertion. The engine
detects only what the caller's checks look at. It never replies to or acts
against a sender.

## API overview

| Area | API |
|---|---|
| Library | `Zxv.load({ backend, libraryPath, wasm })`, `zxv.version`, `zxv.has('TENSOR')`, `zxv.features()`, `zxv.features2()`, `featureMask2`, `statusName`, `statusClass`, `random`, `secretKey`, `iso4217`, `kernel` (C rationals/logic), `wire` (C framing) |
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
| Treaty | `zxv.treaty.parseSutra/parseRecord/create`, `Treaty.run/toSutra/toRecord/parties/obligations/validate`, `TreatyResult`, `picAdd/picSub/picMul/picDiv` |
| CID / AIPI | `zxv.cid.compute/unixfs/parse/fromBytes`, `Cid.toString/verify/toV1/providerKey`; `zxv.aipi.encode*/decode*/functionCid/callCid`; `AipiNode.create(cap)`: `put`, `get`, `publishFunction`, `loadFunction`, `plan` |
| Coin / token | `zxv.coins.face/rung/split/splitCount`; `zxv.tokens.holding/value/canonical/compress/fuse/split/encodeCodes/decodeCodes`; `TokenWallet.create(cap)`: `mint`, `spend`, `transferTo`, `setOctane` |
| Fee | `zxv.fees.assure/assureWithCarry/split/policyDefault` |
| V-Bill hub | `VHub.create(cap)`: `addCurrency`, `setPeg`, `openAccount`, `issue`, `addLimit`, `quote`, `swap`, `balance`, `check` |
| Freight | `zxv.freight.header/encode/decode/split/members/join/encodeOps/expand/opsWriter/generate` |
| Web 4 | `W4Agent.create(cap)`: `card`, `seal`, `reply`, `issueConsent`, `signManifest`; `zxv.web4.verifyCard/actionDigest/manifest/decodeManifest/directory/consentLog/verifyEnvelope` |
| Paradox | `zxv.paradox.signature/kindOfTag/action/verifyRepair`; `ParadoxBook.create(cap)`: `classify`, `registryAdd`, `constructive`, `destructive`, `neutral`, `special`, `draft`, `review`, `graphPass`, logs; `ParadoxPolicy`, `ParadoxHold`, `ParadoxGraph` |
| Hardware | `HwProfile`, `HwMatrix.create(cap)`: `call`, `shadow`, `memo`, `addProvider`, `attest`, `providers`, `setPolicy`, `applyContract`; `HwReleaseKey.generate/build`; `HwRegion.create(cap, device, options)`: `stage`, `trial`, `commit`, `rollback`, `log` |

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
engine is included (a 3.8 MB module with the ABI 1.5 groups); `ZXV_WASM_NO_TENSOR=1` leaves it out.
This SDK adds one C file, `wasm/zxv_wasm_shim.c`: single-threaded pthread
mutex stubs, `arc4random_buf` over WASI `random_get`, a
`zxvjs_generate` that forwards tokens to JavaScript, and
`zxvjs_hw_provider_add` / `zxvjs_hw_region_create` / `zxvjs_hw_region_events`,
whose C trampolines forward hardware implementations and region devices to
the `zxv.hw_*` imports, and `zxvjs_freight_ops_expand`, whose trampoline
forwards freight CIDREF lookups to the `zxv.freight_resolve` import. The JS host provides a
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
