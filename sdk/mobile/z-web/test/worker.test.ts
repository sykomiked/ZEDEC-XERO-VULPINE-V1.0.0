// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The page <-> worker protocol end to end: a real worker thread running
// the real wasm build of @zedec/zxv (sdk/typescript/wasm/zxv.wasm), driven
// through the client, plus raw messages that a buggy or hostile page could
// send.

import assert from 'node:assert/strict';
import { join } from 'node:path';
import { after, before, test } from 'node:test';
import { randomBytes } from 'node:crypto';

import { Cap } from '@zedec/zxv/wasm';

import {
  type CapHandle, DigestMismatchError, PROTOCOL, Rational, sha3_256Hex, type WorkerLike, WorkerGoneError, ZxvWebClient, ZxvWebError,
} from '../src/index.js';
import { startNodeWorker } from '../src/node.js';
import { asset, connect, fixtures, rejectsWith, V, wasm } from './helpers.js';

let zxv: ZxvWebClient;
let root: CapHandle;

before(async () => {
  zxv = await connect();
  ({ root } = await zxv.createContext());
});
after(() => zxv.terminate());

const prompt = (): string => new TextDecoder().decode(asset(join(fixtures, V.generate.prompt)).bytes);

async function tinySession(client: ZxvWebClient, cap: CapHandle) {
  const model = await client.loadModel(cap, asset(join(fixtures, V.generate.model)), asset(join(fixtures, V.generate.tokenizer)));
  return { model, session: await client.newSession(model, { nCtx: V.generate.n_ctx }) };
}

test('the worker runs libzxv ABI 1.4+ on wasm', () => {
  assert.equal(zxv.version.major, 1);
  assert.ok(zxv.version.minor >= 4);
  assert.equal(zxv.version.backend, 'wasm');
});

test('a wasm module with the wrong digest never reaches the worker', async () => {
  const w = startNodeWorker();
  let posted = 0;
  const spy: WorkerLike = { ...w, postMessage: (m, t) => { posted++; w.postMessage(m, t); } };
  await assert.rejects(ZxvWebClient.connect(spy, { bytes: wasm.bytes, sha3_256: '0'.repeat(64) }), DigestMismatchError);
  assert.equal(posted, 0);
});

test('libzxv SHA3-256 in the worker equals the page-side check', async () => {
  const d = new Uint8Array(randomBytes(5000));
  assert.equal(await zxv.sha3_256(d), sha3_256Hex(d));
});

test('ledger: exact minor units, explicit capabilities', async () => {
  await zxv.enableCurrency(root, 'NGN');
  const bank = await zxv.openAccount(root, 'bank', 'NGN', 'issuer');
  const ada = await zxv.openAccount(root, 'ada', 'NGN');
  const id = await zxv.post(root, { from: bank, to: ada, amount: 250_050n, currency: 'NGN', reference: 'E2E-1' });
  assert.equal(typeof id, 'bigint');
  const reader = await zxv.attenuate(root, Cap.LEDGER_READ);
  assert.equal(reader.caps, Cap.LEDGER_READ);
  const b = await zxv.balance(reader, ada);
  assert.deepEqual(b, { debit: 250_050n, credit: 0n, equity: 250_050n });

  // the read-only capability cannot post
  await rejectsWith(zxv.post(reader, { from: bank, to: ada, amount: 1n, currency: 'NGN', reference: 'E2E-2' }), {
    kind: 'CapabilityDeniedError', status: -17, statusName: 'ZXV_E_CAP', statusClass: 'REFUSED',
  });
  // a reference is posted once: a retry is refused, not doubled
  await rejectsWith(zxv.post(root, { from: bank, to: ada, amount: 250_050n, currency: 'NGN', reference: 'E2E-1' }), {
    status: -13, statusName: 'ZXV_E_DUPLICATE', statusClass: 'USAGE',
  });
  const j = await zxv.journal(reader, ada);
  assert.equal(j.length, 1);
  assert.deepEqual(j[0], { entryId: id, amount: 250_050n, counterparty: bank, reference: 'E2E-1' });
  // beyond 2^53 - 1 minor units: exact on the wire, refused by libzxv (never rounded)
  await rejectsWith(zxv.post(root, { from: bank, to: ada, amount: 2n ** 53n, currency: 'NGN', reference: 'E2E-3' }), { statusClass: 'USAGE' });
  // a float never leaves the page
  await rejectsWith(zxv.post(root, { from: bank, to: ada, amount: 1.5, currency: 'NGN', reference: 'E2E-4' }), { kind: 'FloatRejectedError' });
  assert.equal((await zxv.balance(reader, ada)).equity, 250_050n);
});

test('capabilities: revoke, attestation, info', async () => {
  const child = await zxv.attenuate(root, Cap.LEDGER_READ | Cap.BUDGET);
  const info = await zxv.capInfo(child);
  assert.deepEqual(info, { caps: Cap.LEDGER_READ | Cap.BUDGET, attestation: 'TRUE', revoked: false });
  await zxv.setAttestation(child, 'BOTH');
  await rejectsWith(zxv.openBudgets(child), { kind: 'CapabilityDeniedError' });
  await zxv.setAttestation(child, 'TRUE');
  await zxv.openBudgets(child);
  await zxv.revoke(child);
  assert.equal((await zxv.capInfo(child)).revoked, true);
  await rejectsWith(zxv.openBudgets(child), { kind: 'CapabilityDeniedError', status: -17 });
  const noBudget = await zxv.attenuate(root, Cap.LEDGER_READ);
  await rejectsWith(zxv.openBudgets(noBudget), { kind: 'CapabilityDeniedError' });
});

test('budgets are exact rationals both ways', async () => {
  const budgets = await zxv.openBudgets(root);
  const gpu = await zxv.createBudget(budgets, 'gpu-seconds', '10');
  await zxv.consume(gpu, '5/2');
  let s = await zxv.budgetStatus(gpu);
  assert.ok(s.remaining.equals(Rational.of(15n, 2n)));
  assert.ok(s.used.equals(Rational.parse('2.5')));
  await rejectsWith(zxv.consume(gpu, 8), { kind: 'BudgetExhaustedError', status: -19, statusClass: 'REFUSED' });
  assert.ok((await zxv.budgetStatus(gpu)).remaining.equals(Rational.of(15n, 2n)), 'a denied consume takes nothing');
  await zxv.release(gpu, Rational.of(1n, 3n));
  s = await zxv.budgetStatus(gpu);
  assert.equal(s.remaining.toString(), '47/6');
  assert.ok(s.epoch >= 1n);
  await zxv.setLimit(gpu, '100.25');
  assert.equal((await zxv.budgetStatus(gpu)).remaining.toString(), '1177/12');
  await assert.rejects(zxv.consume(gpu, 0.5), { kind: 'FloatRejectedError' });
});

test('greedy generation streams the vectors.json tokens', async () => {
  const { session } = await tinySession(zxv, root);
  const seen: number[] = [];
  const r = await zxv.generate(session, prompt(), { maxTokens: V.generate.greedy.max_tokens, onToken: (t) => seen.push(t.id) });
  assert.deepEqual(r.tokens, V.generate.greedy.tokens);
  assert.deepEqual(seen, V.generate.greedy.tokens);
  assert.equal(r.promptTokens, V.generate.prompt_tokens.length);
  assert.equal(r.stop, 'MAX_TOKENS');

  await zxv.resetSession(session);
  const ids: number[] = [];
  const st = zxv.stream(session, prompt(), { maxTokens: V.generate.greedy.max_tokens });
  for await (const t of st) ids.push(t.id);
  assert.deepEqual(ids, V.generate.greedy.tokens);
  assert.equal((await st.result).text.length > 0, true);
});

test('seeded sampling with exact parameters reproduces vectors.json', async () => {
  const { session } = await tinySession(zxv, root);
  const s = V.generate.sampled;
  const q16 = (q: number): Rational => Rational.of(BigInt(q), 65536n);
  const r = await zxv.generate(session, prompt(), {
    maxTokens: s.max_tokens, temperature: q16(s.temperature_q16), topK: s.top_k, topP: q16(s.top_p_q16),
    repeatPenalty: q16(s.repeat_penalty_q16), repeatLastN: s.repeat_last_n, seed: BigInt(s.seed),
  });
  assert.deepEqual(r.tokens, s.tokens);
  await assert.rejects(zxv.generate(session, prompt(), { temperature: 0.7 }), { kind: 'FloatRejectedError' });
});

test('AbortSignal stops a running generation between tokens', async () => {
  const { session } = await tinySession(zxv, root);
  const ac = new AbortController();
  let n = 0;
  const p = zxv.generate(session, prompt(), {
    maxTokens: 64, signal: ac.signal, onToken: () => { if (++n === 3) ac.abort(new Error('user pressed stop')); },
  });
  await assert.rejects(p, /user pressed stop/);
  assert.equal(n, 3, 'no token is delivered after the abort');
  // the worker stopped too: far fewer than 64 tokens went into the context
  const pos = await zxv.sessionPosition(session);
  assert.ok(pos < V.generate.prompt_tokens.length + 10, `position ${pos}`);
  // and the session is usable at once
  await zxv.resetSession(session);
  const r = await zxv.generate(session, prompt(), { maxTokens: 4 });
  assert.deepEqual(r.tokens, V.generate.greedy.tokens.slice(0, 4));

  const done = new AbortController();
  done.abort();
  await assert.rejects(zxv.generate(session, prompt(), { signal: done.signal }), { name: 'AbortError' });
});

test('a budget bound to a session stops generation at its limit', async () => {
  const { session } = await tinySession(zxv, root);
  const budgets = await zxv.openBudgets(root);
  // the prompt is charged too, one unit per token
  const tokens = await zxv.createBudget(budgets, 'tokens', V.generate.prompt_tokens.length + 5);
  await zxv.bindBudget(session, tokens);
  const r = await zxv.generate(session, prompt(), { maxTokens: 32 });
  assert.equal(r.stop, 'BUDGET');
  assert.deepEqual(r.tokens, V.generate.greedy.tokens.slice(0, 5));
  assert.ok((await zxv.budgetStatus(tokens)).remaining.equals(Rational.ZERO));
  await zxv.resetSession(session);
  await rejectsWith(zxv.generate(session, prompt(), { maxTokens: 32 }), { kind: 'BudgetExhaustedError', status: -19 });
  await zxv.bindBudget(session, null);
  assert.equal((await zxv.generate(session, prompt(), { maxTokens: 2 })).tokens.length, 2);
});

test('handles: closing a context forgets everything made from it', async () => {
  const { context, root: r2 } = await zxv.createContext({ caps: Cap.LEDGER_READ | Cap.LEDGER_POST });
  assert.equal(r2.caps, Cap.LEDGER_READ | Cap.LEDGER_POST);
  const child = await zxv.attenuate(r2, Cap.LEDGER_READ);
  await zxv.close(context);
  await rejectsWith(zxv.capInfo(child), { status: -4, statusName: 'ZXV_E_NOT_FOUND' });
  await rejectsWith(zxv.capInfo(r2), { status: -4 });
  await assert.rejects(zxv.capInfo({ kind: 'budget', handle: 1, name: 'x' } as unknown as CapHandle), TypeError);
});

/** Raw protocol messages, as a buggy or hostile page might send them. */
async function raw(worker: WorkerLike, msg: Record<string, unknown>): Promise<Record<string, unknown>> {
  return new Promise((resolve) => {
    const on = (e: { data?: unknown }): void => {
      const d = e.data as Record<string, unknown>;
      if (d['id'] === msg['id'] && d['type'] !== 'token') {
        worker.removeEventListener('message', on);
        resolve(d);
      }
    };
    worker.addEventListener('message', on);
    worker.postMessage({ protocol: PROTOCOL, ...msg });
  });
}

test('the worker refuses floats, malformed exact values and wrong handles', async () => {
  const w = startNodeWorker();
  try {
    const notYet = await raw(w, { id: 1, op: 'context.create' });
    assert.equal((notYet['error'] as { statusName: string }).statusName, 'ZXV_E_STATE');
    const init = await raw(w, { id: 2, op: 'init', wasm: wasm.bytes.slice().buffer });
    assert.equal(init['type'], 'result');
    const ctx = (await raw(w, { id: 3, op: 'context.create' }))['result'] as { root: number; context: number };
    const b = (await raw(w, { id: 4, op: 'budgets.open', cap: ctx.root }))['result'] as { budgets: number };
    const bud = (await raw(w, { id: 5, op: 'budget.create', budgets: b.budgets, name: 'x', limit: '10/1' }))['result'] as { budget: number };
    const err = async (msg: Record<string, unknown>): Promise<Record<string, unknown>> => {
      const r = await raw(w, msg);
      assert.equal(r['type'], 'error', JSON.stringify(r));
      return r['error'] as Record<string, unknown>;
    };
    assert.equal((await err({ id: 6, op: 'budget.consume', budget: bud.budget, amount: 0.5 }))['statusName'], 'ZXV_E_FLOAT');
    assert.equal((await err({ id: 7, op: 'budget.consume', budget: bud.budget, amount: '0.5' }))['statusName'], 'ZXV_E_FORMAT');
    assert.equal((await err({ id: 8, op: 'budget.consume', budget: bud.budget, amount: '2/4' }))['statusName'], 'ZXV_E_FORMAT');
    assert.equal((await err({ id: 9, op: 'ledger.balance', cap: ctx.root, account: 0.5 }))['statusName'], 'ZXV_E_FLOAT');
    assert.equal((await err({ id: 10, op: 'ledger.post', cap: ctx.root, from: 0, to: 1, amountMinor: 5, currency: 'NGN', reference: 'r' }))['statusName'],
      'ZXV_E_FLOAT');
    assert.equal((await err({ id: 11, op: 'budget.consume', budget: ctx.root, amount: '1/1' }))['name'], 'ZxvWebUsageError');
    assert.equal((await err({ id: 12, op: 'budget.status', budget: 999 }))['statusName'], 'ZXV_E_NOT_FOUND');
    assert.match(String((await err({ id: 13, op: 'eval', code: '1' }))['message']), /unknown op/);
    assert.equal((await err({ id: 14, op: 'init', wasm: new ArrayBuffer(8) }))['statusName'], 'ZXV_E_STATE');
    // nothing was consumed by any of it
    const st = (await raw(w, { id: 15, op: 'budget.status', budget: bud.budget }))['result'] as { remaining: string; used: string };
    assert.deepEqual(st, { remaining: '10/1', used: '0/1', epoch: (st as { epoch?: string }).epoch });
  } finally {
    w.terminate();
  }
});

test('a dead worker rejects pending and later calls', async () => {
  const c = await connect();
  const { root: r } = await c.createContext();
  const pending = c.capInfo(r);
  c.terminate();
  await assert.rejects(pending, WorkerGoneError);
  await assert.rejects(c.capInfo(r), WorkerGoneError);
  assert.ok(c.closed);
  assert.ok(new ZxvWebError('x', { statusClass: 'FATAL' }).fatal);
});
