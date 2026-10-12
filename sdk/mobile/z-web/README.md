<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Z-Web: ZXV in the browser

Package `@zedec/zxv-web`: the WebAssembly build of
[`@zedec/zxv`](../../typescript/) running in a Web Worker, so generation,
budgets and the ledger never block the page. The page and the worker talk
through a small typed protocol (`zxv-web/1`) in which no float ever
travels.

| Piece | File | What it does |
|---|---|---|
| Page client | [`src/client.ts`](src/client.ts) | `ZxvWebClient`: typed calls, explicit capability handles, `AbortSignal` on every call, streaming tokens |
| Worker | [`src/worker.ts`](src/worker.ts), [`src/worker-core.ts`](src/worker-core.ts) | owns the wasm instance and a handle table; checks every message before libzxv sees it |
| Protocol | [`src/protocol.ts`](src/protocol.ts), [`src/wire.ts`](src/wire.ts) | the ops and their exact encodings |
| Offline cache | [`src/cache.ts`](src/cache.ts), [`src/sha3.ts`](src/sha3.ts) | Cache API storage keyed by SHA3-256, checked before every use |
| Logic | [`src/logic.ts`](src/logic.ts) | `Truth` and `Hexagram` from `@zedec/zxv` (pure TypeScript) and their wire forms |
| Node shim | [`src/node.ts`](src/node.ts) | the same worker on `worker_threads` (`@zedec/zxv-web/node`), used by the tests |

## Quickstart

```ts
import { createZxvWeb, AssetCache, Cap } from '@zedec/zxv-web';

const cache = new AssetCache();                              // Cache API, keyed by SHA3-256
const zxv = await createZxvWeb({
  wasm: { url: '/zxv/zxv.wasm', sha3_256: 'b9a05853...' },   // checked before the worker sees it
  cache,
});
const { root } = await zxv.createContext();
const reader = await zxv.attenuate(root, Cap.LEDGER_READ);   // hand out narrower handles

const model = await zxv.loadModel(root,
  await cache.get({ url: '/models/tiny.gguf', sha3_256: '52bc3ca2...' }),
  await cache.get({ url: '/models/tok.gguf', sha3_256: '...' }));
const session = await zxv.newSession(model, { nCtx: 512 });

const stop = new AbortController();
const r = await zxv.generate(session, 'Hello', {
  maxTokens: 64, temperature: '7/10', topK: 40, seed: 42n, signal: stop.signal,
  onToken: (t) => output.append(t.text),
});

const budgets = await zxv.openBudgets(root);                 // needs Cap.BUDGET
const gpu = await zxv.createBudget(budgets, 'gpu-seconds', '10');
await zxv.consume(gpu, '5/2');
(await zxv.budgetStatus(gpu)).remaining.toString();          // "7.5", a Rational, exact
```

`Cap`, `Rational`, `Truth` and `Hexagram` are the `@zedec/zxv` exports,
re-exported. By default `createZxvWeb` starts `dist/src/worker.js` of this
package as a module worker; with a bundler, start
`@zedec/zxv-web/worker` with your bundler's worker syntax and pass it as
`worker`.

## Exact values

| Value | On the page | On the wire |
|---|---|---|
| Rational (limits, amounts of a budget, sampling parameters) | `Rational`, or exact text `"3/4"`, `"12.75"`, or an integer | `"num/den"`: reduced, `den > 0`, both parts within int64; `"7/1"`, `"0/1"` |
| 64-bit integer (minor units, balances, entry ids, seeds, capability bits) | `bigint` (a safe-integer `number` is accepted) | decimal string, `"250050"` |
| Small count or id | integer `number` | safe integer |
| Truth | `Truth.TRUE` ... (compare with `===`) | `"NEITHER"`, `"TRUE"`, `"FALSE"`, `"BOTH"` |
| Hexagram | `Hexagram` | tag byte 0..63 |

A fractional, NaN, infinite or unsafe-integer `number` throws
`ZxvWebError` (`kind` `FloatRejectedError`, `ZXV_E_FLOAT`) on the page,
before anything is sent. The worker checks again: any non-safe-integer
number anywhere in a message, a non-canonical rational (`"2/4"`, `"0.5"`,
`"-0/1"`) or a malformed integer string is refused with a usage error and
never reaches libzxv. `Rational.fromNumberExact(x)` is the one deliberate
way to bring a double in (its exact binary value).

## Capabilities

Nothing is implied: every gated call names the capability handle it
presents (`zxv.post(cap, ...)`, `zxv.openBudgets(cap)`,
`zxv.loadModel(cap, ...)`). `createContext({ caps })` limits the root;
`attenuate(cap, bits)` makes a narrower child, `revoke` kills a subtree,
`setAttestation(cap, 'BOTH')` blocks it. A handle is a number in a frozen
object; it grants nothing by itself, and a forged or closed one is refused
(`ZXV_E_NOT_FOUND`) or used with its real rights only. Closing a context
closes and forgets everything made from it.

## Errors

A libzxv failure arrives as `ZxvWebError` with `kind` (the SDK class:
`CapabilityDeniedError`, `BudgetExhaustedError`, ...), `status`,
`statusName` and `statusClass` (`USAGE`, `REFUSED`, `FATAL`, `SYSTEM`), the
same data zxvd puts in a JSON-RPC error. Branch on `status` or
`statusClass`. `fatal` is true for FATAL: the context is poisoned, close it.
A ledger post whose `reference` was already used is refused with
`ZXV_E_DUPLICATE` (class USAGE), which is what makes retries safe.
`DigestMismatchError` means a file did not hash to the expected digest;
`WorkerGoneError` means the worker died or was terminated (every pending
call rejects with it).

## Cancellation

Every call takes `{ signal }`. Aborting rejects the promise with the
signal's reason at once and tells the worker. For a generation the worker
is busy inside wasm, so a message cannot reach it; instead the page shares
a one-word `SharedArrayBuffer` flag that the worker reads between tokens,
and the run stops after the token in progress (stop reason CANCELLED, the
session usable at once). That needs shared memory: always in Node; in a
browser only on a cross-origin isolated page (`COOP: same-origin`,
`COEP: require-corp`). Without it the promise still rejects at once, but
the worker finishes the run and its tokens are discarded.
`zxv.terminate()` is the hard stop (the worker and every handle are gone).

## Offline cache

`new AssetCache()` stores files in the Cache API under
`https://zxv-cache.invalid/sha3-256/<digest>`: the key is the digest, not
the URL. `get({ url, sha3_256 })`:

1. a stored copy is hashed; if it matches it is used (offline);
2. a copy that does not match is deleted;
3. otherwise the file is fetched, hashed, and stored only if it matches;
   a mismatch throws `DigestMismatchError` and nothing is kept.

`get(spec, { offlineOnly: true })` never touches the network. The SHA3-256
is computed in TypeScript (WebCrypto has no SHA-3, and the wasm module
cannot vouch for itself); the tests check it against FIPS 202 examples,
`vectors.json` and libzxv's `zxv_sha3_256`. Without the Cache API (an
insecure context, Node) the helper still verifies but keeps nothing.

## Build and test

```sh
make -C sdk/libzxv all fixtures                      # libzxv and the test model
cd sdk/typescript && npm ci && npm run build:wasm && npm run build:ts
cd ../mobile/z-web && npm ci && npm test
```

`npm run build:wasm` needs `python3 -m pip install ziglang` (see the TS SDK
README). `npm test` runs 32 tests with `node:test`: the SHA3-256, the wire
encodings and float refusals, Truth and Hexagram against `vectors.json`,
the offline cache (in-memory CacheStorage, fake fetch: no network), and the
protocol end to end on a real `worker_threads` worker running the real
`zxv.wasm`: ledger, capabilities, budgets, greedy and seeded generation
token-for-token equal to `vectors.json`, cancellation mid-generation,
budget-bound generation, and raw hostile messages.

### The example

```sh
cd sdk/mobile/z-web
npm run example:prepare                 # writes example/digests.json
node scripts/serve-example.mjs          # http://127.0.0.1:8417/mobile/z-web/example/
```

The server sends COOP/COEP so the page is cross-origin isolated. The
example page imports `@zedec/zxv/wasm` through an import map and its
worker ([`example/worker.js`](example/worker.js)) uses relative imports
(module workers do not see the page's import map); an app uses a bundler.

## Limits

- **Browsers.** The example was run once in headless Chromium (the build
  Playwright 1.56 ships) on Linux: it loaded, generated, cancelled after
  three tokens, refused a float, and filled the Cache API. Nothing else was
  run in a browser: not Firefox, not Safari, not mobile browsers, not a
  bundler build. The test suite runs in Node on `worker_threads`.
- **wasm is not a sandbox against the page.** The worker runs in the
  page's origin. Any script on the page can post to the worker, create its
  own root capability, read whatever the page reads, or replace the worker.
  Capabilities here are least-privilege discipline inside one origin, not
  isolation; a hostile script on your page owns your ZXV state. Content
  Security Policy and not running third-party scripts are what protect it.
- **Secrets** (keys, seeds) in the worker live in wasm linear memory, which
  any code with the instance can read; this kit does not expose the key
  APIs of `@zedec/zxv` over the protocol.
- **Floats only for display.** The kit never produces or accepts a float
  value; if you show a `Rational` as a decimal (as the example does), that
  rounding is yours and must never flow back.
- **Cancellation without shared memory** cannot interrupt a running
  generation (see above). The wasm build is single-threaded: one worker
  runs one call at a time.
- **The digest is only as good as where you got it.** The cache proves the
  bytes match the digest you pass; ship the digests with your code (not
  next to the files they check).
- `@zedec/zxv` is a `file:` dependency on this repository's
  `sdk/typescript`; neither package is published to npm.
