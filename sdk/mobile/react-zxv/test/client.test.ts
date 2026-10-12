// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The offline-first client without React: queueing, ordered replay,
// exactly-once ledger posts (lost answers, repeated keys, restarts), no
// replay of non-idempotent calls, aborts, and float refusals.

import assert from 'node:assert/strict';
import { test } from 'node:test';

import {
  BackendUnavailableError, FloatRejectedError, IdempotencyConflictError, NotSupportedError, OutcomeUnknownError, type ReplayEvent, ZxvCallError, ZxvClient,
} from '../src/index.js';
import { ExactRational } from '../src/rational.js';
import { FakeBackend, MemoryStorage, tick } from './fake.js';

function setup(opts: { storage?: MemoryStorage } = {}) {
  const backend = new FakeBackend();
  backend.opening.set(1, 1_000_000n); // account 1 is funded
  backend.budgets.set('gpu', { limit: ExactRational.parse('10'), used: ExactRational.ZERO });
  let n = 0;
  const client = new ZxvClient(backend, {
    retryIntervalMs: 0, newIdempotencyKey: () => `k-${++n}`, ...(opts.storage ? { storage: opts.storage } : {}),
  });
  const detach = client.attach();
  return { backend, client, detach };
}

test('online: calls go straight through, exactly', async () => {
  const { backend, client } = setup();
  const p = await client.ledgerPost({ from: 1, to: 2, amount: 250_050n, currency: 'NGN' });
  assert.deepEqual(p, { entryId: 100n, replayed: false, idempotencyKey: 'k-1' });
  assert.deepEqual(await client.ledgerBalance(2), { debit: 250_050n, credit: 0n, equity: 250_050n });
  await client.budgetConsume('gpu', '5/2');
  const s = await client.budgetStatus('gpu');
  assert.equal(s.remaining.toString(), '7.5');
  assert.equal(s.used.toWire(), '5/2');
  assert.deepEqual(client.getState(), { online: true, pending: 0, replaying: false });
  assert.deepEqual(backend.calls, ['ledgerPost:k-1', 'ledgerBalance:2', 'budgetConsume:gpu:5/2', 'budgetStatus:gpu']);
});

test('offline: calls queue in order and replay in that order', async () => {
  const { backend, client } = setup();
  backend.setReachable(false);
  assert.equal(client.getState().online, false);
  const queued: string[] = [];
  const a = client.ledgerPost({ from: 1, to: 2, amount: 10, currency: 'NGN' }, { onQueued: () => queued.push('post1') });
  const b = client.budgetConsume('gpu', 3, { onQueued: () => queued.push('consume') });
  const c = client.ledgerPost({ from: 1, to: 3, amount: 20, currency: 'NGN' }, { onQueued: () => queued.push('post2') });
  const d = client.ledgerBalance(2, { onQueued: () => queued.push('balance') });
  await tick(5);
  assert.deepEqual(queued.sort(), ['balance', 'consume', 'post1', 'post2']); // all queued; the replay order is checked below
  assert.equal(client.getState().pending, 4);
  assert.deepEqual(backend.calls, []);

  backend.setReachable(true); // the reachability event replays
  assert.deepEqual((await a).replayed, false);
  await b;
  assert.equal((await c).entryId, 101n);
  assert.deepEqual(await d, { debit: 10n, credit: 0n, equity: 10n });
  assert.deepEqual(backend.calls, ['ledgerPost:k-1', 'budgetConsume:gpu:3/1', 'ledgerPost:k-2', 'ledgerBalance:2']);
  assert.equal(client.getState().pending, 0);
});

test('a post whose answer was lost is replayed and posted once', async () => {
  const { backend, client } = setup();
  backend.loseAnswerAfterCommit = 1;
  const p = client.ledgerPost({ from: 1, to: 2, amount: 500n, currency: 'NGN', idempotencyKey: 'E2E-77' });
  await tick(5);
  assert.equal(backend.postings.length, 1, 'the first attempt landed');
  assert.equal(client.getState().pending, 1, 'and is queued for replay');
  await client.retry();
  const r = await p;
  assert.deepEqual(r, { entryId: 100n, replayed: true, idempotencyKey: 'E2E-77' });
  assert.equal(backend.postings.length, 1);
  assert.deepEqual(backend.balanceOf(2), { debit: 500n, credit: 0n });
});

test('the same key: one posting; a different transfer with it: refused', async () => {
  const { backend, client } = setup();
  const t = { from: 1, to: 2, amount: 7n, currency: 'NGN', idempotencyKey: 'pay-1' };
  const [x, y] = await Promise.all([client.ledgerPost(t), client.ledgerPost(t)]);
  assert.equal(x, y, 'concurrent calls share one post');
  const z = await client.ledgerPost(t);
  assert.deepEqual(z, { entryId: x.entryId, replayed: true, idempotencyKey: 'pay-1' });
  assert.equal(backend.calls.filter((c) => c.startsWith('ledgerPost')).length, 1);
  await assert.rejects(client.ledgerPost({ ...t, amount: 8n }), IdempotencyConflictError);
  // a fresh client (no memory) still cannot post it twice: the ledger refuses the reference
  const again = new ZxvClient(backend, { retryIntervalMs: 0 });
  assert.deepEqual(await again.ledgerPost(t), { entryId: x.entryId, replayed: true, idempotencyKey: 'pay-1' });
  await again.ledgerPost({ ...t, idempotencyKey: 'pay-2', to: 3 });
  // a client that never saw pay-2 learns of the clash from the journal
  const fresh = new ZxvClient(backend, { retryIntervalMs: 0 });
  await assert.rejects(fresh.ledgerPost({ ...t, idempotencyKey: 'pay-2' }), IdempotencyConflictError);
  assert.equal(backend.postings.length, 2);
});

test('pending posts survive a restart and are replayed once', async () => {
  const storage = new MemoryStorage();
  const one = setup({ storage });
  // one post lands but its answer never comes back before the "crash"...
  one.backend.loseAnswerAfterCommit = 1;
  const inflight = one.client.ledgerPost({ from: 1, to: 3, amount: 60n, currency: 'NGN', idempotencyKey: 'inflight-1' });
  inflight.catch(() => undefined);
  await tick(5);
  assert.equal(one.backend.postings.length, 1);
  // ...and one is made offline and never sent
  one.backend.setReachable(false);
  const lost = one.client.ledgerPost({ from: 1, to: 2, amount: 40n, currency: 'NGN', idempotencyKey: 'offline-1' });
  lost.catch(() => undefined);
  await tick(5);
  one.detach(); // the app dies here
  const saved = JSON.parse(storage.data!) as { posts: { idempotencyKey: string }[] };
  assert.deepEqual(saved.posts.map((p) => p.idempotencyKey), ['inflight-1', 'offline-1']);

  const backend = one.backend;
  backend.setReachable(true);
  const events: ReplayEvent[] = [];
  const two = new ZxvClient(backend, { storage, retryIntervalMs: 0 });
  two.onReplay((e) => events.push(e));
  await two.ready;
  for (let i = 0; i < 20 && events.length < 2; i++) await tick(5);
  assert.deepEqual(events.map((e) => [e.idempotencyKey, e.outcome?.replayed]).sort(), [['inflight-1', true], ['offline-1', false]]);
  assert.equal(backend.postings.length, 2);
  assert.deepEqual(backend.balanceOf(3), { debit: 60n, credit: 0n });
  assert.deepEqual((JSON.parse(storage.data!) as { posts: unknown[] }).posts, []);
  // a third start replays nothing
  const three = new ZxvClient(backend, { storage, retryIntervalMs: 0 });
  await three.ready;
  await tick(10);
  assert.equal(backend.postings.length, 2);
});

test('a non-idempotent call that may have landed is not replayed', async () => {
  const { backend, client } = setup();
  backend.loseConsumeAnswer = 1;
  await assert.rejects(client.budgetConsume('gpu', '4'), OutcomeUnknownError);
  assert.equal(client.getState().pending, 0);
  await client.retry();
  assert.equal(backend.budgets.get('gpu')!.used.toString(), '4', 'consumed once, not twice');
  // but one that was never sent waits and runs once
  backend.setReachable(false);
  const c = client.budgetConsume('gpu', '1/2');
  await tick(1);
  backend.setReachable(true);
  await c;
  assert.equal(backend.budgets.get('gpu')!.used.toString(), '4.5');
});

test('an abort while queued removes the call; refusals pass through', async () => {
  const { backend, client } = setup();
  backend.setReachable(false);
  const ac = new AbortController();
  const p = client.ledgerBalance(2, { signal: ac.signal });
  await tick(1);
  assert.equal(client.getState().pending, 1);
  ac.abort();
  await assert.rejects(p, { name: 'AbortError' });
  assert.equal(client.getState().pending, 0);
  backend.setReachable(true);
  await client.retry();
  assert.deepEqual(backend.calls, []);
  await assert.rejects(client.budgetConsume('gpu', 11), (e: unknown) => e instanceof ZxvCallError && e.statusName === 'ZXV_E_BUDGET');
  await assert.rejects(client.ledgerPost({ from: 2, to: 1, amount: 1n, currency: 'NGN' }), { statusName: 'ZXV_E_FUNDS' });
});

test('floats never reach the backend', async () => {
  const { backend, client } = setup();
  await assert.rejects(client.budgetConsume('gpu', 0.5), FloatRejectedError);
  await assert.rejects(client.ledgerPost({ from: 1, to: 2, amount: 1.5, currency: 'NGN' }), FloatRejectedError);
  await assert.rejects(client.ledgerPost({ from: 1, to: 2, amount: '1.5', currency: 'NGN' }));
  await assert.rejects(client.ledgerPost({ from: 1, to: 2, amount: 0, currency: 'NGN' }), RangeError);
  await assert.rejects(client.ledgerPost({ from: 1, to: 2, amount: 1, currency: 'NGN', idempotencyKey: 'x'.repeat(36) }), TypeError);
  assert.deepEqual(backend.calls, []);
});

test('a feature the backend lacks is NotSupportedError', async () => {
  const { backend, client } = setup();
  backend.features.delete('budget');
  await assert.rejects(client.budgetStatus('gpu'), NotSupportedError);
});

test('the retry timer replays without an event', async () => {
  const backend = new FakeBackend();
  backend.opening.set(1, 100n);
  let down = true;
  const real = backend.ledgerBalance.bind(backend);
  backend.ledgerBalance = async (a: number) => {
    if (down) throw new BackendUnavailableError('down, and it never says so', 'no');
    return real(a);
  };
  const client = new ZxvClient(backend, { retryIntervalMs: 10 });
  const detach = client.attach();
  const p = client.ledgerBalance(1);
  await tick(25);
  assert.equal(client.getState().pending, 1);
  down = false; // no reachability event
  assert.deepEqual(await p, { debit: 100n, credit: 0n, equity: 100n });
  assert.equal(client.getState().online, true);
  detach();
});
