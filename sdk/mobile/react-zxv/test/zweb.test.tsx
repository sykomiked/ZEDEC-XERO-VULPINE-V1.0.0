// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The hooks on @zedec/zxv-web: the real wasm build in a worker thread
// (through z-web's worker_threads shim), as a React DOM app in a browser
// would use it.

import './dom.js';

import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { after, afterEach, test } from 'node:test';

import { act, cleanup, render, renderHook, screen, waitFor } from '@testing-library/react';
import { useEffect, type ReactNode } from 'react';

import { sha3_256Hex, ZxvWebClient } from '@zedec/zxv-web';
import { startNodeWorker } from '@zedec/zxv-web/node';

import { createZWebBackend, useGenerate, useLedgerBalance, useZxv, ZxvClient, ZxvProvider } from '../src/index.js';
import { G, modelPath, prompt, tokenizerPath, wasmPath } from './real.js';

afterEach(cleanup);

const file = (p: string) => {
  const bytes = new Uint8Array(readFileSync(p));
  return { bytes, sha3_256: sha3_256Hex(bytes) };
};

const web = await ZxvWebClient.connect(startNodeWorker(), file(wasmPath));
after(() => web.terminate());
const { root } = await web.createContext();
await web.enableCurrency(root, 'NGN');
const bank = await web.openAccount(root, 'bank', 'NGN', 'issuer');
const ada = await web.openAccount(root, 'ada', 'NGN');
const bob = await web.openAccount(root, 'bob', 'NGN');
await web.post(root, { from: bank, to: ada, amount: 1000n, currency: 'NGN', reference: 'seed' });
const model = await web.loadModel(root, file(modelPath), file(tokenizerPath));
const session = await web.newSession(model, { nCtx: G.n_ctx });
const backend = createZWebBackend({ client: web, ledgerCap: root, session });

const wrap = (client: ZxvClient) => ({ children }: { children?: ReactNode }) => <ZxvProvider client={client}>{children}</ZxvProvider>;

test('useGenerate streams the wasm engine from the worker', async () => {
  const client = new ZxvClient(backend, { retryIntervalMs: 0 });
  const { result } = renderHook(() => useGenerate(), { wrapper: wrap(client) });
  let out: Awaited<ReturnType<typeof result.current.generate>>;
  await act(async () => { out = await result.current.generate(prompt, { maxTokens: G.greedy.max_tokens }); });
  assert.equal(result.current.status, 'done');
  assert.deepEqual(out!.tokens, G.greedy.tokens);
  assert.equal(result.current.chunks, G.greedy.tokens.length);
  await web.resetSession(session);
});

function StopAfterTwo() {
  const g = useGenerate();
  useEffect(() => {
    void g.generate(prompt, { maxTokens: 100, temperature: '7/10', topK: 40, seed: 42n });
  }, []); // eslint-disable-line react-hooks/exhaustive-deps
  useEffect(() => {
    if (g.chunks >= 2 && g.status === 'running') g.cancel();
  }, [g.chunks, g.status, g.cancel]);
  return <p data-testid="g">{g.status}:{g.chunks}</p>;
}

test('cancel from a component stops the stream', async () => {
  const client = new ZxvClient(backend, { retryIntervalMs: 0 });
  render(<ZxvProvider client={client}><StopAfterTwo /></ZxvProvider>);
  await waitFor(() => assert.match(screen.getByTestId('g').textContent!, /^(cancelled|done):/));
  const [status, chunks] = screen.getByTestId('g').textContent!.split(':');
  // a fast run can finish before React renders the second token; either way it ends cleanly
  assert.ok(status === 'cancelled' || Number(chunks) === 100, screen.getByTestId('g').textContent!);
  // the worker is free again
  await web.resetSession(session);
  assert.equal((await web.generate(session, prompt, { maxTokens: 2 })).tokens.length, 2);
  await web.resetSession(session);
});

test('posts through the worker are exactly once, even from a client with no memory', async () => {
  const client = new ZxvClient(backend, { retryIntervalMs: 0 });
  const { result } = renderHook(() => useLedgerBalance(ada), { wrapper: wrap(client) });
  await waitFor(() => assert.equal(result.current.status, 'ready'));
  assert.equal(result.current.balance!.equity, 1000n);
  let first!: { entryId: bigint; replayed: boolean };
  await act(async () => { first = await result.current.post({ to: bob, amount: 300n, currency: 'NGN', idempotencyKey: 'web-1' }); });
  assert.equal(result.current.balance!.equity, 700n);
  const fresh = new ZxvClient(backend, { retryIntervalMs: 0 });
  assert.deepEqual(await fresh.ledgerPost({ from: ada, to: bob, amount: 300n, currency: 'NGN', idempotencyKey: 'web-1' }),
    { entryId: first.entryId, replayed: true, idempotencyKey: 'web-1' });
  assert.equal((await web.balance(root, ada)).equity, 700n);
});

function Net() {
  const { online, pending } = useZxv();
  return <p data-testid="net">{online ? 'online' : 'offline'} {pending}</p>;
}

test('a dead worker makes the backend unreachable and calls wait in the queue', async () => {
  const other = await ZxvWebClient.connect(startNodeWorker(), file(wasmPath));
  const { root: r } = await other.createContext();
  const b = createZWebBackend({ client: other, ledgerCap: r });
  const client = new ZxvClient(b, { retryIntervalMs: 0 });
  render(<ZxvProvider client={client}><Net /></ZxvProvider>);
  other.terminate();
  const ac = new AbortController();
  let p!: Promise<unknown>;
  act(() => { p = client.ledgerBalance(0, { signal: ac.signal }); });
  await waitFor(() => assert.equal(screen.getByTestId('net').textContent, 'offline 1'));
  act(() => ac.abort());
  await assert.rejects(p, { name: 'AbortError' });
  await waitFor(() => assert.equal(screen.getByTestId('net').textContent, 'offline 0'));
});
