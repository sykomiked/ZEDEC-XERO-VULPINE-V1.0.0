// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The zxvd backend against an in-memory daemon that follows
// api/zxvd.openrpc.json: method names, by-name params, decimal-string
// 64-bit integers, error data {status, name, class}, unique references.
// No socket and no network: the "connection" is a pair of callbacks.

import assert from 'node:assert/strict';
import { test } from 'node:test';

import {
  createLineTransport, createZxvdBackend, FloatRejectedError, lineSplitter, type LineConnection, NotSupportedError, ZxvCallError, ZxvClient,
} from '../src/index.js';
import { tick } from './fake.js';

type Req = { jsonrpc: string; id: number; method: string; params: Record<string, unknown> };

/** A small zxvd: ledger.*, swarm.ask, the OpenRPC conventions. */
class FakeZxvd {
  up = true;
  /** Close the connection right after committing the next post (its answer is lost). */
  dropAfterNextPost = false;
  /** Answer balances with JSON numbers (a broken daemon). */
  numbersInsteadOfStrings = false;
  readonly lines: string[] = [];
  readonly postings: { entry: bigint; from: number; to: number; amount: bigint; reference: string }[] = [];
  readonly opening = new Map<number, bigint>([[1, 9_007_199_254_740_993n]]); // above 2^53: exact or nothing
  #entry = 7n;
  #conns = new Set<{ deliver(l: string): void; close(): void }>();

  connect = async (): Promise<LineConnection> => {
    if (!this.up) throw new Error('ECONNREFUSED');
    let onLine: (l: string) => void = () => undefined;
    let onClose: (r?: unknown) => void = () => undefined;
    let open = true;
    const conn = {
      deliver: (l: string) => { if (open) setTimeout(() => onLine(l), 0); },
      close: () => { if (open) { open = false; this.#conns.delete(conn); setTimeout(() => onClose('reset'), 0); } },
    };
    this.#conns.add(conn);
    return {
      write: (line) => {
        if (!open) throw new Error('EPIPE');
        assert.ok(line.endsWith('\n') && !line.slice(0, -1).includes('\n'), 'one request per line');
        this.lines.push(line);
        const req = JSON.parse(line) as Req;
        const res = this.#handle(req);
        if (req.method === 'ledger.post' && this.dropAfterNextPost && !('error' in res)) {
          this.dropAfterNextPost = false;
          conn.close(); // committed; the answer never leaves
          return;
        }
        conn.deliver(JSON.stringify(res));
      },
      onLine: (l) => { onLine = l; },
      onClose: (l) => { onClose = l; },
      close: () => conn.close(),
    };
  };

  down(): void {
    this.up = false;
    for (const c of [...this.#conns]) c.close();
  }

  #bal(a: number): [bigint, bigint] {
    let d = this.opening.get(a) ?? 0n;
    let c = 0n;
    for (const p of this.postings) {
      if (p.to === a) d += p.amount;
      if (p.from === a) c += p.amount;
    }
    return [d, c];
  }

  #handle(r: Req): Record<string, unknown> {
    const ok = (result: unknown) => ({ jsonrpc: '2.0', id: r.id, result });
    const fail = (status: number, name: string, cls: string) =>
      ({ jsonrpc: '2.0', id: r.id, error: { code: -32000, message: name, data: { status, name, class: cls } } });
    const bad = () => ({ jsonrpc: '2.0', id: r.id, error: { code: -32602, message: 'bad params' } });
    const p = r.params;
    switch (r.method) {
      case 'swarm.ask':
        return typeof p['prompt'] === 'string' ? ok({ answer: `echo: ${p['prompt']}` }) : bad();
      case 'ledger.balance': {
        if (!Number.isInteger(p['account'])) return bad();
        const [d, c] = this.#bal(p['account'] as number);
        return this.numbersInsteadOfStrings
          ? ok({ debit_minor: Number(d), credit_minor: Number(c), equity_minor: Number(d - c) })
          : ok({ debit_minor: String(d), credit_minor: String(c), equity_minor: String(d - c) });
      }
      case 'ledger.post': {
        const amt = p['amount_minor'];
        if (!Number.isInteger(p['from']) || !Number.isInteger(p['to']) || typeof p['currency'] !== 'string' || typeof p['reference'] !== 'string'
          || (p['reference'] as string).length > 35) return bad();
        if (!(typeof amt === 'string' && /^[0-9]{1,19}$/.test(amt)) && !(Number.isSafeInteger(amt) && (amt as number) > 0)) return bad();
        if (this.postings.some((x) => x.reference === p['reference'])) return fail(-13, 'ZXV_E_DUPLICATE', 'USAGE');
        const amount = BigInt(amt as string);
        const [d, c] = this.#bal(p['from'] as number);
        if (d - c < amount) return fail(-6, 'ZXV_E_FUNDS', 'REFUSED');
        const entry = this.#entry++;
        this.postings.push({ entry, from: p['from'] as number, to: p['to'] as number, amount, reference: p['reference'] as string });
        return ok({ entry_id: String(entry) });
      }
      case 'ledger.entries': {
        const a = p['account'] as number;
        const entries = this.postings.filter((x) => x.from === a || x.to === a).map((x) => ({
          entry_id: String(x.entry), signed_minor: String(x.to === a ? x.amount : -x.amount), counterparty: x.to === a ? x.from : x.to,
          reference: x.reference,
        }));
        return ok({ entries });
      }
      default:
        return { jsonrpc: '2.0', id: r.id, error: { code: -32601, message: 'unknown method' } };
    }
  }
}

function setup() {
  const zxvd = new FakeZxvd();
  const backend = createZxvdBackend({ transport: createLineTransport({ connect: zxvd.connect, timeoutMs: 2000 }) });
  const client = new ZxvClient(backend, { retryIntervalMs: 0, newIdempotencyKey: () => 'zx-0123456789abcdef0123456789abcdef' });
  client.attach();
  return { zxvd, backend, client };
}

test('requests follow zxvd-rpc/1: by-name params, decimal-string amounts, the key as reference', async () => {
  const { zxvd, client } = setup();
  const p = await client.ledgerPost({ from: 1, to: 2, amount: 9_007_199_254_740_993n, currency: 'VFV' });
  assert.deepEqual(p, { entryId: 7n, replayed: false, idempotencyKey: 'zx-0123456789abcdef0123456789abcdef' });
  const req = JSON.parse(zxvd.lines[0]!) as Req;
  assert.deepEqual(req, {
    jsonrpc: '2.0', id: 1, method: 'ledger.post',
    params: { from: 1, to: 2, amount_minor: '9007199254740993', currency: 'VFV', reference: 'zx-0123456789abcdef0123456789abcdef' },
  });
  assert.deepEqual(await client.ledgerBalance(2), { debit: 9_007_199_254_740_993n, credit: 0n, equity: 9_007_199_254_740_993n });
  assert.equal((JSON.parse(zxvd.lines[1]!) as Req).method, 'ledger.balance');
});

test('libzxv errors arrive with status, name and class', async () => {
  const { client } = setup();
  await assert.rejects(client.ledgerPost({ from: 2, to: 1, amount: 5n, currency: 'VFV', idempotencyKey: 'r1' }), (e: unknown) => {
    assert.ok(e instanceof ZxvCallError);
    assert.equal(e.code, -32000);
    assert.equal(e.status, -6);
    assert.equal(e.statusName, 'ZXV_E_FUNDS');
    assert.equal(e.statusClass, 'REFUSED');
    return true;
  });
});

test('a post whose connection dropped after the commit is replayed once (duplicate resolved from ledger.entries)', async () => {
  const { zxvd, client } = setup();
  zxvd.dropAfterNextPost = true;
  const p = client.ledgerPost({ from: 1, to: 3, amount: 100n, currency: 'VFV', idempotencyKey: 'E2E-9' });
  for (let i = 0; i < 20 && client.getState().pending === 0; i++) await tick(2);
  assert.equal(client.getState().pending, 1);
  assert.equal(zxvd.postings.length, 1);
  await client.retry();
  assert.deepEqual(await p, { entryId: 7n, replayed: true, idempotencyKey: 'E2E-9' });
  assert.equal(zxvd.postings.length, 1);
  assert.deepEqual(zxvd.lines.map((l) => (JSON.parse(l) as Req).method), ['ledger.post', 'ledger.post', 'ledger.entries']);
});

test('daemon down: calls queue; back up: replayed in order', async () => {
  const { zxvd, backend, client } = setup();
  zxvd.down();
  const a = client.ledgerPost({ from: 1, to: 2, amount: 1n, currency: 'VFV', idempotencyKey: 'q1' });
  const b = client.ledgerBalance(2);
  for (let i = 0; i < 20 && client.getState().pending < 2; i++) await tick(2);
  assert.equal(client.getState().pending, 2);
  assert.equal(backend.isReachable(), false);
  zxvd.up = true;
  await client.retry();
  assert.equal((await a).replayed, false);
  assert.equal((await b).equity, 1n);
  assert.equal(backend.isReachable(), true);
});

test('generation is swarm.ask (one answer); budgets are not offered', async () => {
  const { client } = setup();
  const chunks: string[] = [];
  const out = await client.generate({ prompt: 'hello' }, (c) => chunks.push(c.text));
  assert.deepEqual(chunks, ['echo: hello']);
  assert.equal(out.text, 'echo: hello');
  await assert.rejects(client.budgetStatus('gpu'), NotSupportedError);
});

test('a 64-bit value sent as a JSON number is refused, not rounded', async () => {
  const { zxvd, client } = setup();
  zxvd.numbersInsteadOfStrings = true;
  await assert.rejects(client.ledgerBalance(1), FloatRejectedError);
});

test('lineSplitter reassembles socket chunks', () => {
  const got: string[] = [];
  const feed = lineSplitter((l) => got.push(l), 64);
  feed('{"a":');
  feed('1}\n{"b":2}\r\n{"c"');
  feed(':3}\n\n');
  feed('x'.repeat(100));
  feed('tail\n{"d":4}\n');
  assert.deepEqual(got, ['{"a":1}', '{"b":2}', '{"c":3}', '{"d":4}']);
});
