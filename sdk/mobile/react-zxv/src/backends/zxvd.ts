// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// A backend on the local zxvd daemon (api/zxvd.openrpc.json, zxvd-rpc/1):
// JSON-RPC 2.0, one request per line, params by name. 64-bit integers are
// decimal strings both ways; a libzxv failure is error -32000 with data
// {status, name, class}. zxvd serves a 0600 UNIX socket, which React
// Native cannot open by itself: give createLineTransport() a connection
// from a native module (or any duplex line stream) that reaches it.
//
// What zxvd offers maps to the hooks like this:
//   useGenerate       swarm.ask (the whole answer at once: no token stream,
//                     and an abort only stops waiting; the daemon finishes)
//   useLedgerBalance  ledger.balance; posts with ledger.post, the
//                     idempotency key as `reference`; a duplicate
//                     (ZXV_E_DUPLICATE) is resolved with ledger.entries
//   useBudget         not offered by zxvd (NotSupportedError)

import {
  abortReason, BackendUnavailableError, E_DUPLICATE, type Feature, IdempotencyConflictError, Reachability, type StatusClassName, ZxvCallError,
  throwIfAborted, type ZxvBackend,
} from '../backend.js';
import { intFromWire } from '../rational.js';

/** One JSON-RPC exchange. Throw BackendUnavailableError when zxvd cannot be reached. */
export interface JsonRpcTransport {
  request(message: { jsonrpc: '2.0'; id: number; method: string; params: Record<string, unknown> }, signal: AbortSignal): Promise<unknown>;
  /** Connected (or not yet tried). */
  isReachable(): boolean;
  subscribe(listener: (reachable: boolean) => void): () => void;
}

/** A duplex line connection to zxvd (a UNIX socket through a native module, a test double...). */
export interface LineConnection {
  write(line: string): void;
  onLine(listener: (line: string) => void): void;
  onClose(listener: (reason?: unknown) => void): void;
  close(): void;
}

/**
 * Turn arbitrary text chunks (as a socket delivers them) into whole lines
 * for {@link LineConnection.onLine}. Lines over `maxLine` characters (zxvd
 * itself closes a client at 16 KiB) are dropped.
 */
export function lineSplitter(onLine: (line: string) => void, maxLine = 1 << 20): (chunk: string) => void {
  let buf = '';
  let skipping = false;
  return (chunk) => {
    buf += chunk;
    for (let i = buf.indexOf('\n'); i >= 0; i = buf.indexOf('\n')) {
      const line = buf.slice(0, i).replace(/\r$/, '');
      buf = buf.slice(i + 1);
      if (!skipping && line.length > 0) onLine(line);
      skipping = false;
    }
    if (buf.length > maxLine) {
      buf = '';
      skipping = true;
    }
  };
}

/** Options of {@link createLineTransport}. */
export interface LineTransportOptions {
  /** Open a connection; reject when zxvd is not there. */
  connect(): Promise<LineConnection>;
  /** Per-request timeout in ms (default 30000); a timeout counts as "maybe delivered". */
  timeoutMs?: number;
}

/**
 * JSON-RPC over newline-delimited JSON, with replies matched by id. It
 * connects on first use and again after the connection drops.
 */
export function createLineTransport(options: LineTransportOptions): JsonRpcTransport {
  const reach = new Reachability(true);
  const pending = new Map<number, { resolve(v: unknown): void; reject(e: unknown): void }>();
  let conn: Promise<LineConnection> | null = null;
  const timeoutMs = options.timeoutMs ?? 30_000;

  const dropAll = (why: string): void => {
    conn = null;
    reach.set(false);
    const all = [...pending.values()];
    pending.clear();
    for (const p of all) p.reject(new BackendUnavailableError(`zxvd: ${why}`, 'maybe'));
  };

  const open = (): Promise<LineConnection> => {
    if (conn) return conn;
    conn = options.connect().then((c) => {
      c.onLine((line) => {
        let msg: { id?: unknown };
        try {
          msg = JSON.parse(line) as { id?: unknown };
        } catch {
          return; // not JSON: ignore the line
        }
        const p = typeof msg.id === 'number' ? pending.get(msg.id) : undefined;
        if (!p) return;
        pending.delete(msg.id as number);
        p.resolve(msg);
      });
      c.onClose((reason) => dropAll(`connection closed${reason ? ` (${String(reason)})` : ''}`));
      reach.set(true);
      return c;
    }, (e: unknown) => {
      conn = null;
      reach.set(false);
      throw new BackendUnavailableError(`zxvd: cannot connect: ${e instanceof Error ? e.message : String(e)}`, 'no');
    });
    return conn;
  };

  return {
    isReachable: () => reach.get(),
    subscribe: (l) => reach.subscribe(l),
    async request(message, signal) {
      throwIfAborted(signal);
      const c = await open();
      return new Promise((resolve, reject) => {
        const timer = setTimeout(() => {
          pending.delete(message.id);
          reject(new BackendUnavailableError(`zxvd: no answer to ${message.method} in ${timeoutMs} ms`, 'maybe'));
        }, timeoutMs);
        const onAbort = (): void => {
          clearTimeout(timer);
          pending.delete(message.id);
          reject(abortReason(signal));
        };
        signal.addEventListener('abort', onAbort, { once: true });
        pending.set(message.id, {
          resolve: (v) => { clearTimeout(timer); signal.removeEventListener('abort', onAbort); resolve(v); },
          reject: (e) => { clearTimeout(timer); signal.removeEventListener('abort', onAbort); reject(e); },
        });
        try {
          c.write(JSON.stringify(message) + '\n');
        } catch (e) {
          pending.delete(message.id);
          clearTimeout(timer);
          signal.removeEventListener('abort', onAbort);
          dropAll('write failed');
          reject(new BackendUnavailableError(`zxvd: write failed: ${e instanceof Error ? e.message : String(e)}`, 'no'));
        }
      });
    },
  };
}

/** Options of {@link createZxvdBackend}. */
export interface ZxvdBackendOptions {
  transport: JsonRpcTransport;
}

const CLASSES = new Set<StatusClassName>(['USAGE', 'REFUSED', 'FATAL', 'SYSTEM']);

/** The zxvd backend. */
export function createZxvdBackend(options: ZxvdBackendOptions): ZxvBackend {
  const t = options.transport;
  let id = 0;

  const call = async (method: string, params: Record<string, unknown>, signal: AbortSignal): Promise<Record<string, unknown>> => {
    const reqId = ++id;
    const raw = (await t.request({ jsonrpc: '2.0', id: reqId, method, params }, signal)) as Record<string, unknown>;
    if (typeof raw !== 'object' || raw === null || raw['jsonrpc'] !== '2.0' || raw['id'] !== reqId) {
      throw new ZxvCallError(`zxvd: malformed reply to ${method}`, { kind: 'ProtocolError' });
    }
    const err = raw['error'] as { code?: unknown; message?: unknown; data?: Record<string, unknown> } | undefined;
    if (err) {
      const init: ConstructorParameters<typeof ZxvCallError>[1] = { kind: 'JsonRpcError' };
      if (typeof err.code === 'number') init.code = err.code;
      const d = err.data;
      if (d && typeof d['status'] === 'number') init.status = d['status'];
      if (d && typeof d['name'] === 'string') init.statusName = init.kind = d['name'];
      if (d && CLASSES.has(d['class'] as StatusClassName)) init.statusClass = d['class'] as StatusClassName;
      throw new ZxvCallError(`zxvd ${method}: ${typeof err.message === 'string' ? err.message : 'error'}`, init);
    }
    const result = raw['result'];
    if (typeof result !== 'object' || result === null) throw new ZxvCallError(`zxvd: ${method} returned no object`, { kind: 'ProtocolError' });
    return result as Record<string, unknown>;
  };

  const dec = (v: unknown, what: string): string => intFromWire(v, what).toString();

  const features: ReadonlySet<Feature> = new Set<Feature>(['generate', 'ledger']);
  const unsupported = (): Promise<never> => Promise.reject(new ZxvCallError('zxvd has no budget methods', { kind: 'NotSupportedError' }));

  return {
    name: 'zxvd',
    features,
    isReachable: () => t.isReachable(),
    subscribe: (l) => t.subscribe(l),
    async generate(request, onChunk, signal) {
      const r = await call('swarm.ask', { prompt: request.prompt }, signal);
      const answer = typeof r['answer'] === 'string' ? r['answer'] : '';
      onChunk({ text: answer });
      return { text: answer, stop: 'COMPLETE' };
    },
    budgetStatus: unsupported,
    budgetConsume: unsupported,
    budgetRelease: unsupported,
    async ledgerBalance(account, signal) {
      const r = await call('ledger.balance', { account }, signal);
      return { debit: dec(r['debit_minor'], 'debit_minor'), credit: dec(r['credit_minor'], 'credit_minor'), equity: dec(r['equity_minor'], 'equity_minor') };
    },
    async ledgerPost(req, signal) {
      try {
        const r = await call('ledger.post', {
          from: req.from, to: req.to, amount_minor: req.amountMinor, currency: req.currency, reference: req.idempotencyKey,
        }, signal);
        return { entryId: dec(r['entry_id'], 'entry_id'), replayed: false };
      } catch (e) {
        if (!(e instanceof ZxvCallError) || e.status !== E_DUPLICATE) throw e;
        // already posted (an earlier attempt landed): find it, check it is the same transfer
        const r = await call('ledger.entries', { account: req.from }, signal);
        const entries = Array.isArray(r['entries']) ? (r['entries'] as Record<string, unknown>[]) : [];
        const hit = entries.find((x) => x['reference'] === req.idempotencyKey);
        if (!hit) throw e;
        if (hit['counterparty'] !== req.to || dec(hit['signed_minor'], 'signed_minor') !== `-${req.amountMinor}`) {
          throw new IdempotencyConflictError(req.idempotencyKey);
        }
        return { entryId: dec(hit['entry_id'], 'entry_id'), replayed: true };
      }
    },
  };
}
