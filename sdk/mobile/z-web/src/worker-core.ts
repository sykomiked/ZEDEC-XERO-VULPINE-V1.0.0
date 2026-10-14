// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The worker side of zxv-web/1: owns the wasm instance of @zedec/zxv and
// every object made from it, and answers requests from one page. The SDK
// is passed in (worker.ts passes `@zedec/zxv/wasm`), so this file has no
// runtime imports beyond the wire checks and runs unchanged in a Web
// Worker, a worker_threads shim or a test.
//
// Every message is checked before it is used: unknown ops, unknown or
// wrongly typed handles, floats anywhere, malformed "num/den" or decimal
// strings are refused with a usage error and never reach libzxv.

import type * as Sdk from '@zedec/zxv/wasm';

import { type StatusClassName, type WireError, ZxvWebError } from './errors.js';
import { type Endpoint, type Handle, type Op, type Ops, PROTOCOL, type WireStop, type WireTruth } from './protocol.js';
import { assertNoFloats, decodeInt, decodeRational, encodeRational, wireFormat } from './wire.js';

/** The parts of @zedec/zxv the worker uses. */
export type ZxvSdk = Pick<
  typeof Sdk,
  | 'Zxv' | 'Rational' | 'Truth' | 'Cap' | 'Ledger' | 'AccountKind' | 'Budgets' | 'Model' | 'StopReason'
  | 'ZxvUsageError' | 'ZxvRefusedError' | 'ZxvSystemError' | 'ZxvFatalError'
>;

interface Kinds {
  context: Sdk.ZxvContext;
  cap: Sdk.Capability;
  budgets: Sdk.Budgets;
  budget: Sdk.Budget;
  model: Sdk.Model;
  session: Sdk.Session;
}
type Kind = keyof Kinds;
interface Entry {
  kind: Kind;
  obj: unknown;
  parent: Handle | null;
}

const usage = (message: string, status = -1, statusName = 'ZXV_E_ARG'): ZxvWebError =>
  new ZxvWebError(message, { name: 'ZxvWebUsageError', status, statusName, statusClass: 'USAGE' });

type Msg = Record<string, unknown>;

function field<T>(m: Msg, key: string, check: (v: unknown) => v is T, what: string): T {
  const v = m[key];
  if (!check(v)) throw usage(`${key}: expected ${what}`);
  return v;
}
const isString = (v: unknown): v is string => typeof v === 'string';
const isIndex = (v: unknown): v is number => typeof v === 'number' && Number.isSafeInteger(v) && v >= 0;
const isBuffer = (v: unknown): v is ArrayBuffer => v instanceof ArrayBuffer;
const str = (m: Msg, key: string, max = 4096): string => {
  const s = field(m, key, isString, 'a string');
  if (s.length > max) throw usage(`${key}: longer than ${max}`, -12, 'ZXV_E_TOO_LONG');
  return s;
};
const index = (m: Msg, key: string): number => field(m, key, isIndex, 'a non-negative safe integer');

/**
 * Serve zxv-web/1 on `endpoint` with `sdk`. One endpoint, one wasm
 * instance, one handle table.
 */
export function serve(endpoint: Endpoint, sdk: ZxvSdk): void {
  let zxv: Sdk.Zxv | null = null;
  let next = 1;
  const table = new Map<Handle, Entry>();
  const cancelled = new Set<number>();
  const active = new Set<number>();

  const STOP = new Map<number, WireStop>(Object.entries(sdk.StopReason).map(([k, v]) => [v, k as WireStop]));

  const put = (kind: Kind, obj: unknown, parent: Handle | null): Handle => {
    const h = next++;
    table.set(h, { kind, obj, parent });
    return h;
  };
  const get = <K extends Kind>(m: Msg, key: string, kind: K): Kinds[K] => {
    const h = m[key];
    const e = isIndex(h) ? table.get(h) : undefined;
    if (!e) throw usage(`${key}: no such handle ${String(h)}`, -4, 'ZXV_E_NOT_FOUND');
    if (e.kind !== kind) throw usage(`${key}: handle ${String(h)} is a ${e.kind}, not a ${kind}`);
    return e.obj as Kinds[K];
  };
  const forget = (h: Handle): void => {
    table.delete(h);
    for (const [k, e] of [...table]) if (e.parent === h) forget(k);
  };
  const rat = (m: Msg, key: string): Sdk.Rational => {
    const { num, den } = decodeRational(m[key], key);
    return sdk.Rational.of(num, den);
  };
  const wireRat = (r: Sdk.Rational): string => encodeRational(r.num, r.den);
  const lib = (): Sdk.Zxv => {
    if (!zxv) throw usage('the worker is not initialised (send init first)', -9, 'ZXV_E_STATE');
    return zxv;
  };
  const handleOf = (m: Msg, key: string): Handle => index(m, key);

  const toWire = (e: unknown): WireError => {
    if (e instanceof ZxvWebError) {
      const w: { -readonly [K in keyof WireError]: WireError[K] } = { name: e.kind, message: e.message };
      if (e.status !== undefined) w.status = e.status;
      if (e.statusName !== undefined) w.statusName = e.statusName;
      if (e.statusClass !== undefined) w.statusClass = e.statusClass;
      return w;
    }
    let cls: StatusClassName | undefined;
    if (e instanceof sdk.ZxvFatalError) cls = 'FATAL';
    else if (e instanceof sdk.ZxvUsageError) cls = 'USAGE';
    else if (e instanceof sdk.ZxvRefusedError) cls = 'REFUSED';
    else if (e instanceof sdk.ZxvSystemError) cls = 'SYSTEM';
    const err = e as { name?: unknown; message?: unknown; status?: unknown; statusName?: unknown; detail?: unknown };
    const w: { -readonly [K in keyof WireError]: WireError[K] } = {
      name: typeof err?.name === 'string' ? err.name : 'Error',
      message: typeof err?.message === 'string' ? err.message : String(e),
    };
    if (cls) {
      w.statusClass = cls;
      if (typeof err.status === 'number') w.status = err.status;
      if (typeof err.statusName === 'string') w.statusName = err.statusName;
      if (typeof err.detail === 'string') w.detail = err.detail;
    }
    return w;
  };

  const handlers: { [O in Op]: (m: Msg, id: number) => Ops[O]['res'] | Promise<Ops[O]['res']> } = {
    init: async (m) => {
      if (zxv) throw usage('already initialised', -9, 'ZXV_E_STATE');
      const bytes = field(m, 'wasm', isBuffer, 'an ArrayBuffer');
      zxv = await sdk.Zxv.load({ backend: 'wasm', wasm: new Uint8Array(bytes) });
      return { ...zxv.version, backend: zxv.backend };
    },
    'context.create': (m) => {
      const caps = m['caps'] === undefined ? sdk.Cap.ALL_1_4 : decodeInt(m['caps'], 'caps', true);
      const ctx = lib().createContext();
      const c = put('context', ctx, null);
      const root = ctx.rootCapability({ caps });
      return { context: c, root: put('cap', root, c), caps: root.info().caps.toString() };
    },
    'cap.attenuate': (m) => {
      const cap = get(m, 'cap', 'cap');
      const child = cap.attenuate(decodeInt(m['keep'], 'keep', true));
      return { cap: put('cap', child, handleOf(m, 'cap')), caps: child.info().caps.toString() };
    },
    'cap.revoke': (m) => {
      get(m, 'cap', 'cap').revoke();
      return null;
    },
    'cap.setAttestation': (m) => {
      const t = str(m, 'truth', 8);
      if (t !== 'NEITHER' && t !== 'TRUE' && t !== 'FALSE' && t !== 'BOTH') throw wireFormat('truth', t);
      get(m, 'cap', 'cap').setAttestation(sdk.Truth.fromName(t));
      return null;
    },
    'cap.info': (m) => {
      const i = get(m, 'cap', 'cap').info();
      return { caps: i.caps.toString(), attestation: i.attestation.name as WireTruth, revoked: i.revoked };
    },
    'handle.close': (m) => {
      const h = handleOf(m, 'handle');
      const e = table.get(h);
      if (!e) throw usage(`handle: no such handle ${h}`, -4, 'ZXV_E_NOT_FOUND');
      const o = e.obj as { close?: () => void };
      forget(h);
      o.close?.();
      return null;
    },
    'currency.enable': (m) => {
      const cap = get(m, 'cap', 'cap');
      cap.context.enableCurrency(cap, str(m, 'code', 15));
      return null;
    },
    'ledger.openAccount': (m) => {
      const kind = str(m, 'kind', 8);
      if (kind !== 'holder' && kind !== 'issuer') throw usage('kind: holder or issuer');
      const account = sdk.Ledger.of(get(m, 'cap', 'cap')).openAccount(
        str(m, 'name', 63), str(m, 'currency', 15), kind === 'issuer' ? sdk.AccountKind.ISSUER : sdk.AccountKind.HOLDER);
      return { account };
    },
    'ledger.post': (m) => {
      const entry = sdk.Ledger.of(get(m, 'cap', 'cap')).post(
        index(m, 'from'), index(m, 'to'), decodeInt(m['amountMinor'], 'amountMinor'), str(m, 'currency', 15), str(m, 'reference', 35));
      return { entryId: entry.toString() };
    },
    'ledger.balance': (m) => {
      const b = sdk.Ledger.of(get(m, 'cap', 'cap')).balance(index(m, 'account'));
      return { debit: b.debit.toString(), credit: b.credit.toString(), equity: b.equity.toString() };
    },
    'ledger.journal': (m) => ({
      entries: sdk.Ledger.of(get(m, 'cap', 'cap')).journal(index(m, 'account')).map((j) => ({
        entryId: j.entryId.toString(), amountMinor: j.amount.toString(), counterparty: j.counterparty, reference: j.reference,
      })),
    }),
    'budgets.open': (m) => ({ budgets: put('budgets', sdk.Budgets.open(get(m, 'cap', 'cap')), handleOf(m, 'cap')) }),
    'budget.create': (m) => {
      const budgets = get(m, 'budgets', 'budgets');
      const enforce = m['enforce'];
      if (enforce !== undefined && typeof enforce !== 'boolean') throw usage('enforce: expected a boolean');
      const b = budgets.create(str(m, 'name', 63), rat(m, 'limit'), enforce === undefined ? {} : { enforce });
      return { budget: put('budget', b, handleOf(m, 'budgets')) };
    },
    'budget.consume': (m) => {
      get(m, 'budget', 'budget').consume(rat(m, 'amount'));
      return null;
    },
    'budget.release': (m) => {
      get(m, 'budget', 'budget').release(rat(m, 'amount'));
      return null;
    },
    'budget.setLimit': (m) => {
      get(m, 'budget', 'budget').setLimit(rat(m, 'limit'));
      return null;
    },
    'budget.status': (m) => {
      const s = get(m, 'budget', 'budget').status();
      return { remaining: wireRat(s.remaining), used: wireRat(s.used), epoch: s.epoch.toString() };
    },
    'model.load': (m) => {
      const cap = get(m, 'cap', 'cap');
      const model = new Uint8Array(field(m, 'model', isBuffer, 'an ArrayBuffer'));
      const tok = m['tokenizer'] === undefined ? undefined : new Uint8Array(field(m, 'tokenizer', isBuffer, 'an ArrayBuffer'));
      const mdl = sdk.Model.fromBytes(cap, model, tok);
      const i = mdl.info();
      return { model: put('model', mdl, handleOf(m, 'cap')), nVocab: i.nVocab, nCtxTrain: i.nCtxTrain, nLayer: i.nLayer, eos: i.eos };
    },
    'session.create': (m) => {
      const model = get(m, 'model', 'model');
      const s = model.newSession(m['nCtx'] === undefined ? {} : { nCtx: index(m, 'nCtx') });
      return { session: put('session', s, handleOf(m, 'model')) };
    },
    'session.reset': (m) => {
      get(m, 'session', 'session').reset();
      return null;
    },
    'session.position': (m) => ({ position: get(m, 'session', 'session').position() }),
    'session.bindBudget': (m) => {
      const s = get(m, 'session', 'session');
      s.bindBudget(m['budget'] === null ? null : get(m, 'budget', 'budget'));
      return null;
    },
    'session.generate': async (m, id) => {
      const session = get(m, 'session', 'session');
      const prompt = str(m, 'prompt', 1 << 20);
      const o = m['options'];
      if (typeof o !== 'object' || o === null || Array.isArray(o)) throw usage('options: expected an object');
      const om = o as Msg;
      const flagBuf = m['cancelFlag'];
      const flag = typeof SharedArrayBuffer === 'function' && flagBuf instanceof SharedArrayBuffer ? new Int32Array(flagBuf) : null;
      const opts: Sdk.GenerateOptions = {
        onToken: (t) => {
          endpoint.postMessage({ protocol: PROTOCOL, id, type: 'token', token: { id: t.id, text: t.text } });
          if (flag && Atomics.load(flag, 0) !== 0) return false;
          return !cancelled.has(id);
        },
      };
      if (om['maxTokens'] !== undefined) opts.maxTokens = index(om, 'maxTokens');
      if (om['topK'] !== undefined) opts.topK = index(om, 'topK');
      if (om['repeatLastN'] !== undefined) opts.repeatLastN = index(om, 'repeatLastN');
      if (om['temperature'] !== undefined) opts.temperature = rat(om, 'temperature');
      if (om['topP'] !== undefined) opts.topP = rat(om, 'topP');
      if (om['repeatPenalty'] !== undefined) opts.repeatPenalty = rat(om, 'repeatPenalty');
      if (om['seed'] !== undefined) opts.seed = decodeInt(om['seed'], 'seed', true);
      for (const k of Object.keys(om)) {
        if (!['maxTokens', 'topK', 'repeatLastN', 'temperature', 'topP', 'repeatPenalty', 'seed'].includes(k)) throw usage(`options.${k}: unknown option`);
      }
      if (cancelled.has(id) || (flag && Atomics.load(flag, 0) !== 0)) {
        throw new ZxvWebError('cancelled before it started', { name: 'AbortError' });
      }
      const r = await session.generate(prompt, opts);
      return {
        text: r.text, tokens: [...r.tokens], stop: STOP.get(r.stop) ?? 'CANCELLED', promptTokens: r.promptTokens, generatedTokens: r.generatedTokens,
      };
    },
    'hash.sha3_256': (m) => {
      const d = lib().hashes.sha3_256(new Uint8Array(field(m, 'data', isBuffer, 'an ArrayBuffer')));
      let hex = '';
      for (const b of d) hex += b.toString(16).padStart(2, '0');
      return { hex };
    },
  };

  const reply = (id: number, body: { type: 'result'; result: unknown } | { type: 'error'; error: WireError }): void => {
    endpoint.postMessage({ protocol: PROTOCOL, id, ...body });
  };

  endpoint.addEventListener('message', (event) => {
    const m = event.data as Msg;
    if (typeof m !== 'object' || m === null || m['protocol'] !== PROTOCOL) return; // not ours
    const id = m['id'];
    if (!isIndex(id)) return; // nothing to answer to
    void (async () => {
      try {
        assertNoFloats(m);
        const op = m['op'];
        if (op === 'cancel') {
          const t = m['target'];
          if (isIndex(t) && active.has(t)) cancelled.add(t);
          return;
        }
        if (typeof op !== 'string' || !Object.hasOwn(handlers, op)) throw usage(`unknown op ${JSON.stringify(op)}`);
        if (op !== 'init') lib();
        active.add(id);
        const handler = handlers[op as Op] as (m: Msg, id: number) => unknown;
        const result = await handler(m, id);
        reply(id, { type: 'result', result: result ?? null });
      } catch (e) {
        reply(id, { type: 'error', error: toWire(e) });
      } finally {
        active.delete(id);
        cancelled.delete(id);
      }
    })();
  });
}
