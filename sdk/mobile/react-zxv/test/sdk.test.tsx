// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The hooks on the real @zedec/zxv Node-API addon, in-process: libzxv's
// own ledger (its duplicate-reference refusal is what makes a replay
// safe), its exact budgets and its deterministic engine.

import './dom.js';

import assert from 'node:assert/strict';
import { afterEach, after, test } from 'node:test';

import { act, cleanup, renderHook, waitFor } from '@testing-library/react';
import type { ReactNode } from 'react';

import { AccountKind, Budgets, Cap, Ledger, Model, Zxv } from '@zedec/zxv';

import { createSdkBackend, useBudget, useGenerate, useLedgerBalance, ZxvCallError, ZxvClient, ZxvProvider } from '../src/index.js';
import { G, modelPath, prompt, tokenizerPath } from './real.js';

afterEach(cleanup);

const zxv = await Zxv.load({ backend: 'native' });
const ctx = zxv.createContext();
const root = ctx.rootCapability({ caps: Cap.ALL });
ctx.enableCurrency(root, 'NGN');
const ledger = Ledger.of(root);
const bank = ledger.openAccount('bank', 'NGN', AccountKind.ISSUER);
const ada = ledger.openAccount('ada', 'NGN');
const bob = ledger.openAccount('bob', 'NGN');
ledger.post(bank, ada, 1000n, 'NGN', 'seed-ada');
const gpu = Budgets.open(root).create('gpu-seconds', '10');
const model = await Model.open(root, modelPath, tokenizerPath);
const backend = createSdkBackend({ ledger, session: model.newSession({ nCtx: G.n_ctx }), budgets: { gpu } });
after(() => ctx.close());

const wrap = (client: ZxvClient) => ({ children }: { children?: ReactNode }) => <ZxvProvider client={client}>{children}</ZxvProvider>;

test('addon: zxv.backend is native', () => {
  assert.equal(zxv.backend, 'native');
});

test('useLedgerBalance + post on the real ledger: a replay with the same key posts nothing', async () => {
  const client = new ZxvClient(backend, { retryIntervalMs: 0 });
  const { result } = renderHook(() => useLedgerBalance(ada), { wrapper: wrap(client) });
  await waitFor(() => assert.equal(result.current.status, 'ready'));
  assert.deepEqual(result.current.balance, { debit: 1000n, credit: 0n, equity: 1000n });
  let first!: { entryId: bigint; replayed: boolean };
  await act(async () => { first = await result.current.post({ to: bob, amount: 250n, currency: 'NGN', idempotencyKey: 'rent-2026-10' }); });
  assert.equal(first.replayed, false);
  assert.equal(result.current.balance!.equity, 750n);
  // a client that never saw the key (an app restart without storage): libzxv refuses the reference, the adapter finds the posting
  const fresh = new ZxvClient(backend, { retryIntervalMs: 0 });
  const again = await fresh.ledgerPost({ from: ada, to: bob, amount: 250n, currency: 'NGN', idempotencyKey: 'rent-2026-10' });
  assert.deepEqual(again, { entryId: first.entryId, replayed: true, idempotencyKey: 'rent-2026-10' });
  assert.equal(ledger.balance(ada).equity, 750n);
  assert.equal(ledger.journal(ada).filter((j) => j.reference === 'rent-2026-10').length, 1);
  await assert.rejects(fresh.ledgerPost({ from: ada, to: bob, amount: 251n, currency: 'NGN', idempotencyKey: 'rent-2026-10' }), {
    name: 'IdempotencyConflictError',
  });
  await assert.rejects(fresh.ledgerPost({ from: bob, to: ada, amount: 10_000n, currency: 'NGN' }), (e: unknown) =>
    e instanceof ZxvCallError && e.statusName === 'ZXV_E_FUNDS' && e.statusClass === 'REFUSED');
});

test('a read-only capability cannot post', async () => {
  const reader = createSdkBackend({ ledger: Ledger.of(root.attenuate(Cap.LEDGER_READ)) });
  const client = new ZxvClient(reader, { retryIntervalMs: 0 });
  assert.equal((await client.ledgerBalance(ada)).equity, 750n);
  await assert.rejects(client.ledgerPost({ from: ada, to: bob, amount: 1n, currency: 'NGN' }), (e: unknown) =>
    e instanceof ZxvCallError && e.kind === 'CapabilityDeniedError' && e.status === -17 && e.statusClass === 'REFUSED');
});

test('useBudget on a real budget: exact, and refusals keep their class', async () => {
  const client = new ZxvClient(backend, { retryIntervalMs: 0 });
  const { result } = renderHook(() => useBudget('gpu'), { wrapper: wrap(client) });
  await waitFor(() => assert.ok(result.current.state));
  await act(() => result.current.consume('5/2'));
  assert.equal(result.current.state!.remaining.toString(), '7.5');
  await act(async () => {
    await assert.rejects(result.current.consume('7.6'), (e: unknown) =>
      e instanceof ZxvCallError && e.kind === 'BudgetExhaustedError' && e.statusClass === 'REFUSED');
  });
  assert.equal(gpu.status().remaining.toString(), '7.5');
});

test('useGenerate streams the addon engine token by token', async () => {
  const client = new ZxvClient(backend, { retryIntervalMs: 0 });
  const { result } = renderHook(() => useGenerate(), { wrapper: wrap(client) });
  let out: Awaited<ReturnType<typeof result.current.generate>>;
  await act(async () => { out = await result.current.generate(prompt, { maxTokens: G.greedy.max_tokens }); });
  assert.equal(result.current.status, 'done');
  assert.deepEqual(out!.tokens, G.greedy.tokens);
  assert.equal(result.current.chunks, G.greedy.tokens.length);
  assert.equal(result.current.stop, 'MAX_TOKENS');
  // the same text as the SDK's own generate on a fresh session
  const s = model.newSession({ nCtx: G.n_ctx });
  assert.equal(result.current.text, (await s.generate(prompt, { maxTokens: G.greedy.max_tokens })).text);
  s.close();
});
