// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The hooks in React DOM (jsdom) with @testing-library/react and the fake
// backend: streaming and cancel, queued while offline, exact budgets and
// balances, exactly-once posts, StrictMode.

import './dom.js';

import assert from 'node:assert/strict';
import { afterEach, test } from 'node:test';

import { act, cleanup, fireEvent, render, renderHook, screen, waitFor } from '@testing-library/react';
import { StrictMode, useState, type ReactNode } from 'react';

import {
  FloatRejectedError, useBudget, useGenerate, useLedgerBalance, useRational, useZxv, ZxvCallError, ZxvClient, ZxvProvider,
} from '../src/index.js';
import { ExactRational } from '../src/rational.js';
import { FakeBackend } from './fake.js';

afterEach(cleanup);

function fake(): FakeBackend {
  const b = new FakeBackend();
  b.opening.set(1, 1_000n);
  b.budgets.set('gpu', { limit: ExactRational.parse('10'), used: ExactRational.ZERO });
  return b;
}

function Status() {
  const { online, pending } = useZxv();
  return <p data-testid="net">{online ? 'online' : 'offline'} {pending}</p>;
}

function Generator() {
  const g = useGenerate();
  return (
    <div>
      <button onClick={() => void g.generate('hi', { maxTokens: 5, temperature: '7/10', seed: 42n })}>go</button>
      <button onClick={g.cancel}>stop</button>
      <p data-testid="status">{g.status}</p>
      <p data-testid="text">{g.text}</p>
      <p data-testid="chunks">{g.chunks}</p>
    </div>
  );
}

test('useGenerate streams tokens as they arrive', async () => {
  const backend = fake();
  backend.tokenDelayMs = 15;
  render(<ZxvProvider backend={backend} options={{ retryIntervalMs: 0 }}><Generator /></ZxvProvider>);
  fireEvent.click(screen.getByText('go'));
  await waitFor(() => assert.equal(screen.getByTestId('text').textContent, 'Hel'));
  assert.equal(screen.getByTestId('status').textContent, 'running');
  await waitFor(() => assert.equal(screen.getByTestId('status').textContent, 'done'));
  assert.equal(screen.getByTestId('text').textContent, 'Hello, world');
  assert.equal(screen.getByTestId('chunks').textContent, '5');
  assert.deepEqual(backend.calls, ['generate:hi']);
});

test('useGenerate cancel stops the stream and reaches the backend', async () => {
  const backend = fake();
  backend.tokenDelayMs = 20;
  render(<ZxvProvider backend={backend} options={{ retryIntervalMs: 0 }}><Generator /></ZxvProvider>);
  fireEvent.click(screen.getByText('go'));
  await waitFor(() => assert.equal(screen.getByTestId('chunks').textContent, '2'));
  fireEvent.click(screen.getByText('stop'));
  await waitFor(() => assert.equal(screen.getByTestId('status').textContent, 'cancelled'));
  const text = screen.getByTestId('text').textContent;
  await act(() => new Promise((ok) => setTimeout(ok, 80)));
  assert.equal(screen.getByTestId('text').textContent, text, 'nothing arrives after cancel');
  assert.equal(backend.genSignals[0]!.aborted, true);
});

test('offline: useGenerate waits in the queue and runs when the backend is back', async () => {
  const backend = fake();
  backend.setReachable(false);
  render(<ZxvProvider backend={backend} options={{ retryIntervalMs: 0 }}><Status /><Generator /></ZxvProvider>);
  assert.equal(screen.getByTestId('net').textContent, 'offline 0');
  fireEvent.click(screen.getByText('go'));
  await waitFor(() => assert.equal(screen.getByTestId('status').textContent, 'queued'));
  assert.equal(screen.getByTestId('net').textContent, 'offline 1');
  act(() => backend.setReachable(true));
  await waitFor(() => assert.equal(screen.getByTestId('status').textContent, 'done'));
  assert.equal(screen.getByTestId('text').textContent, 'Hello, world');
  assert.equal(screen.getByTestId('net').textContent, 'online 0');
});

test('useBudget: exact values in, exact values out', async () => {
  const backend = fake();
  const wrapper = ({ children }: { children?: ReactNode }) => <ZxvProvider backend={backend} options={{ retryIntervalMs: 0 }}>{children}</ZxvProvider>;
  const { result } = renderHook(() => useBudget('gpu'), { wrapper });
  await waitFor(() => assert.ok(result.current.state));
  assert.equal(result.current.state!.remaining.toString(), '10');
  await act(() => result.current.consume('5/2'));
  assert.equal(result.current.state!.remaining.toString(), '7.5');
  assert.equal(result.current.state!.used.toWire(), '5/2');
  await act(() => result.current.release(ExactRational.of(1n, 3n)));
  assert.equal(result.current.state!.remaining.toString(), '47/6');
  await act(async () => {
    await assert.rejects(result.current.consume(0.5), FloatRejectedError);
  });
  await act(async () => {
    await assert.rejects(result.current.consume(100), (e: unknown) => e instanceof ZxvCallError && e.kind === 'BudgetExhaustedError');
  });
  assert.equal((result.current.error as ZxvCallError).statusName, 'ZXV_E_BUDGET');
  assert.equal(result.current.state!.remaining.toString(), '47/6', 'nothing consumed by the refusals');
});

test('useRational parses exact text and refuses floats', () => {
  const { result, rerender } = renderHook(({ v }: { v: string | number }) => useRational(v), { initialProps: { v: '12.75' as string | number } });
  assert.equal(result.current.text, '12.75');
  assert.equal(result.current.wire, '51/4');
  assert.equal(result.current.display(1), '12.8');
  rerender({ v: '1/3' });
  assert.equal(result.current.text, '1/3');
  assert.equal(result.current.display(4), '0.3333');
  rerender({ v: 0.1 });
  assert.equal(result.current.value, null);
  assert.ok(result.current.error instanceof FloatRejectedError);
  rerender({ v: 'abc' });
  assert.equal(result.current.wire, null);
  assert.ok(result.current.error);
});

function Wallet({ onPosted }: { onPosted?: (id: bigint) => void }) {
  const { balance, status, post } = useLedgerBalance(1);
  const [key] = useState('pay-rent-2026-10');
  return (
    <div>
      <p data-testid="bal">{status}:{balance ? balance.equity.toString() : '-'}</p>
      <button onClick={() => void post({ to: 2, amount: 250n, currency: 'NGN', idempotencyKey: key }).then((p) => onPosted?.(p.entryId))}>pay</button>
    </div>
  );
}

test('useLedgerBalance: a post made offline is queued, replayed and posted once', async () => {
  const backend = fake();
  const posted: bigint[] = [];
  render(<ZxvProvider backend={backend} options={{ retryIntervalMs: 0 }}><Status /><Wallet onPosted={(id) => posted.push(id)} /></ZxvProvider>);
  await waitFor(() => assert.equal(screen.getByTestId('bal').textContent, 'ready:1000'));
  act(() => backend.setReachable(false));
  fireEvent.click(screen.getByText('pay'));
  fireEvent.click(screen.getByText('pay')); // an impatient second tap: same key
  await waitFor(() => assert.equal(screen.getByTestId('net').textContent, 'offline 1'));
  assert.equal(backend.postings.length, 0);
  act(() => backend.setReachable(true));
  await waitFor(() => assert.equal(screen.getByTestId('bal').textContent, 'ready:750'));
  await waitFor(() => assert.equal(posted.length, 2));
  assert.equal(posted[0], posted[1]);
  assert.equal(backend.postings.length, 1);
  assert.equal(backend.calls.filter((c) => c.startsWith('ledgerPost')).length, 1);
});

test('a lost answer during a post is replayed without a second posting', async () => {
  const backend = fake();
  backend.loseAnswerAfterCommit = 1;
  const client = new ZxvClient(backend, { retryIntervalMs: 0 });
  render(<ZxvProvider client={client}><Status /><Wallet /></ZxvProvider>);
  await waitFor(() => assert.equal(screen.getByTestId('bal').textContent, 'ready:1000'));
  fireEvent.click(screen.getByText('pay'));
  await waitFor(() => assert.equal(screen.getByTestId('net').textContent, 'offline 1'));
  await act(() => client.retry());
  await waitFor(() => assert.equal(screen.getByTestId('bal').textContent, 'ready:750'));
  assert.equal(backend.postings.length, 1);
});

test('StrictMode: double mounting keeps one subscription and works', async () => {
  const backend = fake();
  render(<StrictMode><ZxvProvider backend={backend} options={{ retryIntervalMs: 0 }}><Status /><Wallet /></ZxvProvider></StrictMode>);
  await waitFor(() => assert.equal(screen.getByTestId('bal').textContent, 'ready:1000'));
  act(() => backend.setReachable(false));
  await waitFor(() => assert.equal(screen.getByTestId('net').textContent, 'offline 0'));
  act(() => backend.setReachable(true));
  await waitFor(() => assert.equal(screen.getByTestId('net').textContent, 'online 0'));
});

test('hooks outside a provider say so', () => {
  const quiet = console.error;
  console.error = () => undefined; // React reports the render error too
  try {
    assert.throws(() => renderHook(() => useZxv()), /inside <ZxvProvider>/);
  } finally {
    console.error = quiet;
  }
});
