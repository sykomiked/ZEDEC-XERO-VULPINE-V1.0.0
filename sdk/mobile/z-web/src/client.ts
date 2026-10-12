// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The page side of zxv-web/1. Generation, budgets and the ledger run in
// the worker; the page holds handles and exact values only.
//
// Exact values: amounts and other 64-bit integers are bigint (a safe
// integer number is accepted); rationals are @zedec/zxv `Rational`s (or
// exact text such as "3/4" or "12.75"). A fractional, NaN, infinite or
// unsafe-integer number throws before anything is sent.

import { Rational } from '@zedec/zxv/wasm';

import { VerifiedBytes, verifyBytes } from './cache.js';
import { WorkerGoneError, ZxvWebError } from './errors.js';
import {
  type Handle, type Op, type Ops, PROTOCOL, type Reply, type WireGenerateOptions, type WireStop, type WireToken, type WireTruth,
  type WorkerLike,
} from './protocol.js';
import { decodeInt, decodeRational, encodeInt, encodeRational, floatRejected, type WireRational } from './wire.js';

/** Anything that converts exactly to a Rational. */
export type RationalLike = Rational | string | bigint | number;
/** An integer: bigint, or a safe-integer number. */
export type IntLike = bigint | number;

/** Convert to a Rational exactly; floats are refused. */
export function toRational(x: RationalLike, what = 'value'): Rational {
  if (x instanceof Rational) return x;
  if (typeof x === 'bigint') return Rational.of(x);
  if (typeof x === 'number') {
    if (!Number.isSafeInteger(x)) throw floatRejected(what, x);
    return Rational.of(x);
  }
  if (typeof x === 'string') return Rational.parse(x);
  throw new TypeError(`${what}: expected a Rational, exact text, a bigint or a safe integer`);
}

/** A RationalLike as a wire "num/den" string. */
export function toWireRational(x: RationalLike, what = 'value'): WireRational {
  const r = toRational(x, what);
  return encodeRational(r.num, r.den);
}

/** A wire "num/den" string as a Rational. */
export function fromWireRational(text: string, what = 'value'): Rational {
  const { num, den } = decodeRational(text, what);
  return Rational.of(num, den);
}

/** A worker object named by a handle. Pass it back explicitly; it grants nothing by itself. */
interface HandleBase<K extends string> {
  readonly kind: K;
  readonly handle: Handle;
}
/** A context. */
export type ContextHandle = HandleBase<'context'>;
/** A capability. Every gated call names the one it presents. */
export interface CapHandle extends HandleBase<'cap'> {
  /** The bits it held when made (Cap.* values). */
  readonly caps: bigint;
}
/** A set of budgets (needs Cap.BUDGET). */
export type BudgetsHandle = HandleBase<'budgets'>;
/** One budget. */
export interface BudgetHandle extends HandleBase<'budget'> {
  readonly name: string;
}
/** A loaded model. */
export interface ModelHandle extends HandleBase<'model'> {
  readonly nVocab: number;
  readonly nCtxTrain: number;
  readonly nLayer: number;
  readonly eos: number;
}
/** A generation session. */
export type SessionHandle = HandleBase<'session'>;
/** Any handle. */
export type AnyHandle = ContextHandle | CapHandle | BudgetsHandle | BudgetHandle | ModelHandle | SessionHandle;

/** Options of a generation. */
export interface GenerateOptions {
  maxTokens?: number;
  temperature?: RationalLike;
  topK?: number;
  topP?: RationalLike;
  repeatPenalty?: RationalLike;
  repeatLastN?: number;
  seed?: IntLike;
  /**
   * Stop the generation. With shared memory (always in Node; in a browser
   * only when the page is cross-origin isolated) the worker stops after the
   * token it is producing; without it the promise rejects at once and the
   * worker finishes the run, its tokens discarded.
   */
  signal?: AbortSignal;
  /** Every token, as it is produced. */
  onToken?: (token: WireToken) => void;
}

/** A finished generation. */
export interface GenerateResult {
  readonly text: string;
  readonly tokens: readonly number[];
  readonly stop: WireStop;
  readonly promptTokens: number;
  readonly generatedTokens: number;
}

/** Exact budget state. */
export interface BudgetStatus {
  readonly remaining: Rational;
  readonly used: Rational;
  readonly epoch: bigint;
}

/** Ledger balances in minor units. */
export interface Balance {
  readonly debit: bigint;
  readonly credit: bigint;
  readonly equity: bigint;
}

/** One journal line. */
export interface JournalEntry {
  readonly entryId: bigint;
  readonly amount: bigint;
  readonly counterparty: number;
  readonly reference: string;
}

/** A streaming generation. */
export interface TokenStream extends AsyncIterable<WireToken> {
  readonly result: Promise<GenerateResult>;
}

/** Per-call options. */
export interface CallOptions {
  signal?: AbortSignal;
}

/** Wasm module bytes to start the worker with: checked unless already {@link VerifiedBytes}. */
export type WasmInput = VerifiedBytes | { bytes: Uint8Array; sha3_256: string };

/** Model or tokenizer bytes: checked unless already {@link VerifiedBytes}. */
export type FileInput = VerifiedBytes | { bytes: Uint8Array; sha3_256: string };

interface Pending {
  resolve(v: unknown): void;
  reject(e: unknown): void;
  onToken?: ((t: WireToken) => void) | undefined;
}

const canShare = (): boolean =>
  typeof SharedArrayBuffer === 'function' && (globalThis as { crossOriginIsolated?: boolean }).crossOriginIsolated !== false;

const verified = (x: FileInput, what: string): Uint8Array =>
  x instanceof VerifiedBytes ? x.bytes : verifyBytes(x.bytes, x.sha3_256, what).bytes;

const freeze = <T extends object>(o: T): T => Object.freeze(o);

/** Version facts of the loaded library. */
export interface WorkerVersion {
  readonly major: number;
  readonly minor: number;
  readonly patch: number;
  readonly backend: string;
}

/** A connection to one zxv worker. */
export class ZxvWebClient {
  readonly #worker: WorkerLike;
  readonly #pending = new Map<number, Pending>();
  #next = 1;
  #gone: Error | null = null;
  #version: WorkerVersion | null = null;
  readonly #onMessage = (event: { data?: unknown }): void => this.#receive(event.data);
  readonly #onError = (event: { message?: string }): void => this.#fail(new WorkerGoneError(event.message ?? 'worker error'));

  private constructor(worker: WorkerLike) {
    this.#worker = worker;
    worker.addEventListener('message', this.#onMessage);
    worker.addEventListener('error', this.#onError);
    worker.addEventListener('messageerror', this.#onError);
  }

  /**
   * Start a client on `worker` and load the wasm module into it. The
   * module's SHA3-256 is checked here, before the worker sees it. On any
   * failure (a digest mismatch included) the worker is terminated.
   */
  static async connect(worker: WorkerLike, wasm: WasmInput, options: CallOptions = {}): Promise<ZxvWebClient> {
    const c = new ZxvWebClient(worker);
    try {
      const bytes = verified(wasm, 'zxv.wasm');
      const v = await c.#call('init', { wasm: bytes.slice().buffer }, { ...options, transfer: true });
      c.#version = freeze({ ...v });
    } catch (e) {
      c.terminate();
      throw e;
    }
    return c;
  }

  /** ABI version and backend ("wasm"). */
  get version(): WorkerVersion {
    if (!this.#version) throw new Error('not connected');
    return this.#version;
  }

  /** True after the worker died or was terminated. */
  get closed(): boolean {
    return this.#gone !== null;
  }

  /** Stop the worker; every pending call rejects with {@link WorkerGoneError}. */
  terminate(): void {
    this.#fail(new WorkerGoneError('terminated'));
    this.#worker.terminate();
  }

  // ---- contexts and capabilities ----------------------------------------

  /** A new context and its root capability (default: every ABI 1.4 bit; narrow it with `caps`). */
  async createContext(options: { caps?: bigint } & CallOptions = {}): Promise<{ context: ContextHandle; root: CapHandle }> {
    const r = await this.#call('context.create', options.caps === undefined ? {} : { caps: encodeInt(options.caps, 'caps') }, options);
    return {
      context: freeze({ kind: 'context', handle: r.context }),
      root: freeze({ kind: 'cap', handle: r.root, caps: decodeInt(r.caps, 'caps', true) }),
    };
  }

  /** A child capability holding `cap.caps & keep`. */
  async attenuate(cap: CapHandle, keep: bigint, options: CallOptions = {}): Promise<CapHandle> {
    const r = await this.#call('cap.attenuate', { cap: h(cap, 'cap'), keep: encodeInt(keep, 'keep') }, options);
    return freeze({ kind: 'cap', handle: r.cap, caps: decodeInt(r.caps, 'caps', true) });
  }

  /** Revoke a capability and every descendant. */
  async revoke(cap: CapHandle, options: CallOptions = {}): Promise<void> {
    await this.#call('cap.revoke', { cap: h(cap, 'cap') }, options);
  }

  /** Set a capability's attestation; anything but TRUE blocks it. */
  async setAttestation(cap: CapHandle, truth: WireTruth, options: CallOptions = {}): Promise<void> {
    await this.#call('cap.setAttestation', { cap: h(cap, 'cap'), truth }, options);
  }

  /** Bits, attestation and revocation of a capability. */
  async capInfo(cap: CapHandle, options: CallOptions = {}): Promise<{ caps: bigint; attestation: WireTruth; revoked: boolean }> {
    const r = await this.#call('cap.info', { cap: h(cap, 'cap') }, options);
    return freeze({ caps: decodeInt(r.caps, 'caps', true), attestation: r.attestation, revoked: r.revoked });
  }

  /** Close a handle (a context closes everything made from it). */
  async close(handle: AnyHandle, options: CallOptions = {}): Promise<void> {
    await this.#call('handle.close', { handle: h(handle, handle.kind) }, options);
  }

  // ---- ledger -----------------------------------------------------------

  /** Enable an ISO 4217 currency (needs Cap.LEDGER_POST). */
  async enableCurrency(cap: CapHandle, code: string, options: CallOptions = {}): Promise<void> {
    await this.#call('currency.enable', { cap: h(cap, 'cap'), code }, options);
  }

  /** Open an account (needs Cap.LEDGER_POST). */
  async openAccount(cap: CapHandle, name: string, currency: string, kind: 'holder' | 'issuer' = 'holder', options: CallOptions = {}): Promise<number> {
    return (await this.#call('ledger.openAccount', { cap: h(cap, 'cap'), name, currency, kind }, options)).account;
  }

  /**
   * Post a balanced transfer of `amount` minor units (needs Cap.LEDGER_POST).
   * `reference` is unique per ledger: a second post with the same one is
   * refused (ZXV_E_DUPLICATE), which is what makes a retry safe.
   */
  async post(
    cap: CapHandle, transfer: { from: number; to: number; amount: IntLike; currency: string; reference: string }, options: CallOptions = {},
  ): Promise<bigint> {
    const r = await this.#call('ledger.post', {
      cap: h(cap, 'cap'), from: transfer.from, to: transfer.to, amountMinor: encodeInt(transfer.amount, 'amount'),
      currency: transfer.currency, reference: transfer.reference,
    }, options);
    return decodeInt(r.entryId, 'entryId');
  }

  /** Balances of an account (needs Cap.LEDGER_READ). */
  async balance(cap: CapHandle, account: number, options: CallOptions = {}): Promise<Balance> {
    const r = await this.#call('ledger.balance', { cap: h(cap, 'cap'), account }, options);
    return freeze({ debit: decodeInt(r.debit, 'debit'), credit: decodeInt(r.credit, 'credit'), equity: decodeInt(r.equity, 'equity') });
  }

  /** An account's journal, oldest first (needs Cap.LEDGER_READ). */
  async journal(cap: CapHandle, account: number, options: CallOptions = {}): Promise<JournalEntry[]> {
    const r = await this.#call('ledger.journal', { cap: h(cap, 'cap'), account }, options);
    return r.entries.map((e) => freeze({
      entryId: decodeInt(e.entryId, 'entryId'), amount: decodeInt(e.amountMinor, 'amount'), counterparty: e.counterparty, reference: e.reference,
    }));
  }

  // ---- budgets ----------------------------------------------------------

  /** A budget set (needs Cap.BUDGET). */
  async openBudgets(cap: CapHandle, options: CallOptions = {}): Promise<BudgetsHandle> {
    return freeze({ kind: 'budgets', handle: (await this.#call('budgets.open', { cap: h(cap, 'cap') }, options)).budgets });
  }

  /** A budget with an exact limit. */
  async createBudget(budgets: BudgetsHandle, name: string, limit: RationalLike, options: { enforce?: boolean } & CallOptions = {}): Promise<BudgetHandle> {
    const req: Ops['budget.create']['req'] = { budgets: h(budgets, 'budgets'), name, limit: toWireRational(limit, 'limit') };
    if (options.enforce !== undefined) req.enforce = options.enforce;
    return freeze({ kind: 'budget', handle: (await this.#call('budget.create', req, options)).budget, name });
  }

  /** Consume exactly `amount`; throws a REFUSED ZxvWebError (BudgetExhaustedError) when it does not fit. */
  async consume(budget: BudgetHandle, amount: RationalLike, options: CallOptions = {}): Promise<void> {
    await this.#call('budget.consume', { budget: h(budget, 'budget'), amount: toWireRational(amount, 'amount') }, options);
  }

  /** Give back exactly `amount`. */
  async release(budget: BudgetHandle, amount: RationalLike, options: CallOptions = {}): Promise<void> {
    await this.#call('budget.release', { budget: h(budget, 'budget'), amount: toWireRational(amount, 'amount') }, options);
  }

  /** Change the limit. */
  async setLimit(budget: BudgetHandle, limit: RationalLike, options: CallOptions = {}): Promise<void> {
    await this.#call('budget.setLimit', { budget: h(budget, 'budget'), limit: toWireRational(limit, 'limit') }, options);
  }

  /** Exact remaining and used amounts. */
  async budgetStatus(budget: BudgetHandle, options: CallOptions = {}): Promise<BudgetStatus> {
    const r = await this.#call('budget.status', { budget: h(budget, 'budget') }, options);
    return freeze({ remaining: fromWireRational(r.remaining, 'remaining'), used: fromWireRational(r.used, 'used'), epoch: decodeInt(r.epoch, 'epoch') });
  }

  // ---- engine -----------------------------------------------------------

  /** Load a GGUF model (and tokenizer) from checked bytes (needs Cap.AI_INFERENCE). */
  async loadModel(cap: CapHandle, model: FileInput, tokenizer?: FileInput, options: CallOptions = {}): Promise<ModelHandle> {
    const m = verified(model, 'model').slice().buffer;
    const req: Ops['model.load']['req'] = { cap: h(cap, 'cap'), model: m };
    if (tokenizer) req.tokenizer = verified(tokenizer, 'tokenizer').slice().buffer;
    const r = await this.#call('model.load', req, { ...options, transfer: true });
    return freeze({ kind: 'model', handle: r.model, nVocab: r.nVocab, nCtxTrain: r.nCtxTrain, nLayer: r.nLayer, eos: r.eos });
  }

  /** A session on a model. */
  async newSession(model: ModelHandle, options: { nCtx?: number } & CallOptions = {}): Promise<SessionHandle> {
    const req: Ops['session.create']['req'] = { model: h(model, 'model') };
    if (options.nCtx !== undefined) req.nCtx = options.nCtx;
    return freeze({ kind: 'session', handle: (await this.#call('session.create', req, options)).session });
  }

  /** Forget the session's tokens. */
  async resetSession(session: SessionHandle, options: CallOptions = {}): Promise<void> {
    await this.#call('session.reset', { session: h(session, 'session') }, options);
  }

  /** Tokens in the session's context (prompt plus generated). */
  async sessionPosition(session: SessionHandle, options: CallOptions = {}): Promise<number> {
    return (await this.#call('session.position', { session: h(session, 'session') }, options)).position;
  }

  /** Charge one unit of `budget` per generated token (null unbinds). */
  async bindBudget(session: SessionHandle, budget: BudgetHandle | null, options: CallOptions = {}): Promise<void> {
    await this.#call('session.bindBudget', { session: h(session, 'session'), budget: budget === null ? null : h(budget, 'budget') }, options);
  }

  /** Generate; tokens arrive through `onToken` while the worker runs. */
  async generate(session: SessionHandle, prompt: string, options: GenerateOptions = {}): Promise<GenerateResult> {
    const wire: WireGenerateOptions = {};
    if (options.maxTokens !== undefined) wire.maxTokens = options.maxTokens;
    if (options.topK !== undefined) wire.topK = options.topK;
    if (options.repeatLastN !== undefined) wire.repeatLastN = options.repeatLastN;
    if (options.temperature !== undefined) wire.temperature = toWireRational(options.temperature, 'temperature');
    if (options.topP !== undefined) wire.topP = toWireRational(options.topP, 'topP');
    if (options.repeatPenalty !== undefined) wire.repeatPenalty = toWireRational(options.repeatPenalty, 'repeatPenalty');
    if (options.seed !== undefined) wire.seed = encodeInt(options.seed, 'seed');
    const req: Ops['session.generate']['req'] = { session: h(session, 'session'), prompt, options: wire };
    let flag: Int32Array | undefined;
    if (canShare()) {
      req.cancelFlag = new SharedArrayBuffer(4);
      flag = new Int32Array(req.cancelFlag);
    }
    const r = await this.#call('session.generate', req, { signal: options.signal, onToken: options.onToken, flag });
    return freeze({ ...r, tokens: freeze([...r.tokens]) });
  }

  /** Generate as an async stream of tokens; `result` settles at the end. */
  stream(session: SessionHandle, prompt: string, options: Omit<GenerateOptions, 'onToken'> = {}): TokenStream {
    const queue: WireToken[] = [];
    let done = false;
    let failure: { e: unknown } | null = null;
    let wake: (() => void) | null = null;
    const poke = (): void => {
      const w = wake;
      wake = null;
      w?.();
    };
    const result = this.generate(session, prompt, { ...options, onToken: (t) => { queue.push(t); poke(); } });
    result.then(() => { done = true; poke(); }, (e: unknown) => { failure = { e }; done = true; poke(); });
    result.catch(() => undefined);
    return {
      result,
      async *[Symbol.asyncIterator]() {
        for (;;) {
          const t = queue.shift();
          if (t) {
            yield t;
            continue;
          }
          if (done) {
            if (failure) throw (failure as { e: unknown }).e;
            return;
          }
          await new Promise<void>((ok) => { wake = ok; });
        }
      },
    };
  }

  /** libzxv's SHA3-256 of `data`, computed in the worker (lower-case hex). */
  async sha3_256(data: Uint8Array, options: CallOptions = {}): Promise<string> {
    return (await this.#call('hash.sha3_256', { data: data.slice().buffer }, { ...options, transfer: true })).hex;
  }

  // ---- transport --------------------------------------------------------

  #call<O extends Op>(
    op: O, fields: Ops[O]['req'],
    options: { signal?: AbortSignal | undefined; transfer?: boolean; onToken?: ((t: WireToken) => void) | undefined; flag?: Int32Array | undefined },
  ): Promise<Ops[O]['res']> {
    if (this.#gone) return Promise.reject(this.#gone);
    const signal = options.signal;
    if (signal?.aborted) return Promise.reject(signal.reason);
    const id = this.#next++;
    const msg = { ...fields, protocol: PROTOCOL, id, op };
    const transfer: Transferable[] = [];
    if (options.transfer) {
      for (const v of Object.values(fields as object)) if (v instanceof ArrayBuffer) transfer.push(v);
    }
    return new Promise<Ops[O]['res']>((resolve, reject) => {
      const onAbort = (): void => {
        if (!this.#pending.delete(id)) return;
        if (options.flag) Atomics.store(options.flag, 0, 1);
        if (!this.#gone) this.#worker.postMessage({ protocol: PROTOCOL, id: this.#next++, op: 'cancel', target: id });
        reject(signal?.reason);
      };
      const cleanup = (): void => signal?.removeEventListener('abort', onAbort);
      this.#pending.set(id, {
        resolve: (v) => { cleanup(); resolve(v as Ops[O]['res']); },
        reject: (e) => { cleanup(); reject(e); },
        onToken: options.onToken,
      });
      signal?.addEventListener('abort', onAbort, { once: true });
      try {
        this.#worker.postMessage(msg, transfer);
      } catch (e) {
        this.#pending.delete(id);
        cleanup();
        reject(e);
      }
    });
  }

  #receive(data: unknown): void {
    const r = data as Reply;
    if (typeof r !== 'object' || r === null || r.protocol !== PROTOCOL) return;
    const p = this.#pending.get(r.id);
    if (!p) return; // cancelled, or not ours
    if (r.type === 'token') {
      try {
        p.onToken?.(r.token);
      } catch (e) {
        this.#pending.delete(r.id);
        p.reject(e);
      }
      return;
    }
    this.#pending.delete(r.id);
    if (r.type === 'result') p.resolve(r.result);
    else p.reject(ZxvWebError.fromWire(r.error));
  }

  #fail(e: Error): void {
    if (this.#gone) return;
    this.#gone = e;
    this.#worker.removeEventListener('message', this.#onMessage);
    this.#worker.removeEventListener('error', this.#onError);
    this.#worker.removeEventListener('messageerror', this.#onError);
    const all = [...this.#pending.values()];
    this.#pending.clear();
    for (const p of all) p.reject(e);
  }
}

function h(x: AnyHandle, kind: AnyHandle['kind']): Handle {
  if (typeof x !== 'object' || x === null || x.kind !== kind || !Number.isSafeInteger(x.handle)) {
    throw new TypeError(`expected a ${kind} handle`);
  }
  return x.handle;
}
