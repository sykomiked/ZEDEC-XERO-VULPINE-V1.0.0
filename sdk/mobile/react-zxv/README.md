<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# React-ZXV: offline-first React hooks

Package `@zedec/zxv-react`: `ZxvProvider` and five hooks for React DOM and
React Native. The hooks run on an injected backend, so the same component
works in a browser (over [Z-Web](../z-web/)), in Node or Electron (over the
[`@zedec/zxv`](../../typescript/) addon) and in React Native or Expo (over
the local [zxvd](../../../docs/ZXVD.md) daemon). Its only runtime dependency
is React; it imports nothing from Node, the DOM or a bundler.

| Hook | Gives you |
|---|---|
| `useZxv()` | the client, `online`, `pending` (queued calls), `replaying`, `features`, `retry()` |
| `useGenerate()` | `generate(prompt, options)` streaming into `text` / `chunks`, `status` (`idle`, `queued`, `running`, `done`, `cancelled`, `error`), `stop`, `cancel()` |
| `useBudget(name)` | exact `state.remaining` / `state.used`, `consume(amount)`, `release(amount)`, `refresh()` |
| `useRational(input)` | an exact value from text such as `"12.75"` or `"3/4"`, its wire form, `display(digits)` |
| `useLedgerBalance(account)` | exact `balance` (`bigint` minor units), `status` (`loading`, `queued`, `ready`, `error`), `post(transfer)` exactly once |

## Quickstart

```tsx
import { ZxvProvider, createZWebBackend, useGenerate, useLedgerBalance } from '@zedec/zxv-react';
import { createZxvWeb } from '@zedec/zxv-web';

const web = await createZxvWeb({ wasm: { url: '/zxv.wasm', sha3_256: WASM_SHA3 } });
const { root } = await web.createContext();
// ... load a model and open a session with z-web, then hand the backend the handles it presents:
const backend = createZWebBackend({ client: web, ledgerCap: root, session, budgets: { gpu } });

function App() {
  return <ZxvProvider backend={backend}><Chat /><Wallet /></ZxvProvider>;
}

function Chat() {
  const g = useGenerate();
  return <>
    <button onClick={() => g.generate('Hello', { maxTokens: 64, temperature: '7/10', seed: 42n })}>ask</button>
    <button onClick={g.cancel} disabled={g.status !== 'running'}>stop</button>
    <p>{g.text}</p>
  </>;
}

function Wallet() {
  const { balance, status, post } = useLedgerBalance(ADA);
  return <>
    <p>{status === 'queued' ? 'offline: waiting' : balance?.equity.toString()}</p>
    <button onClick={() => post({ to: BOB, amount: 250n, currency: 'NGN', idempotencyKey: 'rent-2026-10' })}>pay</button>
  </>;
}
```

## Backends

| Backend | Use | Features |
|---|---|---|
| `createZWebBackend({ client, ledgerCap, session, budgets })` | React DOM in a browser, over `@zedec/zxv-web` | generate (token stream, cancel), budget, ledger |
| `createSdkBackend({ ledger, session, budgets })` | Node, Electron, server rendering: `@zedec/zxv` in-process (addon or wasm) | generate (token stream, cancel), budget, ledger |
| `createZxvdBackend({ transport })` | React Native / Expo, talking to the local zxvd | generate (`swarm.ask`: one answer, no stream), ledger; no budgets (zxvd has no budget methods) |

Capabilities are bound when the backend is made: pass the capability (or
objects that hold one, like `Ledger.of(cap)`) that the app should present,
attenuated to what it needs. The hooks never create or widen one. A feature
a backend lacks fails with `NotSupportedError`.

zxvd speaks JSON-RPC 2.0, one request per line, over a 0600 UNIX socket
([`api/zxvd.openrpc.json`](../../../api/zxvd.openrpc.json)). React Native has
no UNIX socket API: `createLineTransport({ connect })` takes a connection
you provide (from a native module, or any duplex stream that reaches the
daemon) and does the framing, id matching, timeouts and reconnection;
`lineSplitter` turns raw socket chunks into lines. The adapter follows the
daemon's conventions: params by name, `amount_minor` and every 64-bit
result as a decimal string (a 64-bit value that arrives as a JSON number is
refused), errors as `ZxvCallError` with `code` and, for libzxv failures
(-32000), `status`, `statusName` and `statusClass` from `error.data`.

Your own backend implements `ZxvBackend` (see [`src/backend.ts`](src/backend.ts)):
exact values cross it as strings (rationals `"num/den"`, 64-bit integers in
decimal), it throws `BackendUnavailableError` with `delivered: 'no' | 'maybe'`
when it cannot be reached, and its `ledgerPost` is idempotent on the key.

## Offline first

`ZxvProvider` wraps the backend in a `ZxvClient`:

- While the backend is reachable and nothing is waiting, calls run at once
  (concurrently).
- While it is not, calls wait in a queue ordered by when they were made and
  are replayed one at a time, in that order, when it comes back (a
  reachability event, `retry()`, or every `retryIntervalMs`, default 5 s).
  `useZxv().pending` counts them; hooks show `queued`. Aborting a queued
  call removes it.
- **Ledger posts are never duplicated.** Each post carries an idempotency
  key (yours, e.g. an EndToEndId, or a generated `zx-` + 32 hex digits)
  that becomes the posting's ledger `reference`. libzxv refuses a second
  posting with a reference (`ZXV_E_DUPLICATE`); the adapters then find the
  original in the sender's journal and return it with `replayed: true`
  (or `IdempotencyConflictError` if that reference was a different
  transfer). So a post that reached the ledger but lost its answer is
  replayed safely, two taps with one key post once, and the client also
  remembers completed keys (the same key returns the first outcome without
  a call).
- With `options.storage` (`{ load, save }`, e.g. AsyncStorage or
  localStorage), a post is written to storage **before** it is sent and
  removed when answered, so posts pending at a crash or restart are replayed
  on the next start; `client.onReplay` reports them.
- Calls that are not idempotent (budget consume/release, generation) are
  replayed only if they were never sent. If one was sent and the answer was
  lost, it fails with `OutcomeUnknownError` instead of running twice.

## Exact numbers

Rationals are `ExactRational` (reduced, `den > 0`, both parts within int64
like libzxv's `rat_t`, overflow throws, the same text grammar as
`@zedec/zxv`'s `Rational` and cross-checked against it). It is
self-contained so it loads under Hermes; a `@zedec/zxv` `Rational` converts
to it exactly. Amounts and balances are `bigint` minor units. Every input
takes a `bigint`, a safe-integer `number` or exact text; a fractional, NaN,
infinite or unsafe-integer `number` throws `FloatRejectedError` before
anything is sent. `toDisplay(digits)` rounds half to even and returns a
string, for showing a value only.

## Build and test

```sh
make -C sdk/libzxv all fixtures                                   # libzxv + test model
(cd sdk/typescript && npm ci && npm run build:wasm && npm run build:ts)
(cd sdk/mobile/z-web && npm ci && npm run build)
cd sdk/mobile/react-zxv && npm ci && npm run typecheck && npm test
```

`npm test` runs 41 tests with `node:test`, React DOM on jsdom and
`@testing-library/react`: `ExactRational` against `@zedec/zxv`'s
`Rational` (4000 random pairs) and the import graph (React only); the
client with a fake backend (ordering, lost answers, repeated keys,
restart from storage, no replay of non-idempotent calls, aborts, floats);
every hook with the fake backend (streaming, cancel, offline queue,
StrictMode); the zxvd adapter against an in-memory daemon that follows the
OpenRPC document; the hooks on the real `@zedec/zxv` Node-API addon
(libzxv's own duplicate-reference refusal, capability denial, exact
budgets, token-for-token generation); and the hooks on Z-Web's real wasm
worker. No network is used.

## Limits

- **No React Native device or simulator was run**, and no Expo app was
  built. The React Native support is: no imports beyond React (a test
  checks), no Node, DOM, `import.meta` or bundler constructs, and an abort
  helper that does not need `AbortSignal.reason`. It needs Hermes with
  `BigInt` (React Native 0.70+), `crypto.getRandomValues` for generated
  keys (or pass `newIdempotencyKey`), and a native module that connects to
  zxvd's UNIX socket; none of that was exercised.
- **Browsers**: the hooks were tested in jsdom, not in a browser. Z-Web's
  own README says what ran in a browser.
- **zxvd**: tested against an in-memory imitation of the protocol, not the
  real daemon. Generation over zxvd is `swarm.ask`: one answer, no token
  stream; aborting stops the wait, not the daemon. zxvd has no budget
  methods.
- **wasm in a browser is not a sandbox against the page**: see Z-Web.
  Capabilities bound into a backend are least-privilege discipline inside
  one process or origin, not isolation.
- **Exactly once** holds for ledger posts because libzxv refuses duplicate
  references and the key is kept until the post is answered (with
  storage, across restarts). It does not hold if you lose the storage
  while a post is pending and then post again with a new key, or if the
  ledger itself is replaced. Completed keys are remembered up to
  `rememberKeys` (default 1000); beyond that the ledger's refusal still
  prevents a second posting.
- **Floats only for display**: `toDisplay` output is text and must not be
  parsed back into an amount.
- `@zedec/zxv` and `@zedec/zxv-web` are `file:` dev dependencies on this
  repository (for the integration tests); nothing is published to npm.
