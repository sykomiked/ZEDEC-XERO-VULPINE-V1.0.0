// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// ZxvClient: the offline-first layer between the hooks and a backend.
//
// - While the backend is reachable and nothing is queued, calls go straight
//   through (concurrently).
// - When it is not, calls wait in a queue ordered by the moment they were
//   made, and are replayed one at a time, in that order, when it comes back
//   (on a reachability event, on retry(), or on a timer).
// - Ledger posts carry an idempotency key that becomes the ledger
//   reference. The ledger refuses a second posting with the same reference,
//   and the backend reports it as `replayed`, so a post that was sent, lost
//   its answer and is replayed is still posted once. The client also
//   remembers keys it has seen: the same key again returns the first
//   outcome without a call, and the same key for a different transfer is
//   refused. Pending posts are persisted (if a storage is given) before they
//   are sent, so a post interrupted by an app restart is replayed too.
// - A call that is not idempotent (budget consume/release, generation) is
//   never replayed once it may have been delivered: it fails with
//   OutcomeUnknownError instead.

import {
  abortReason, BackendUnavailableError, type BalanceSnapshot, type BudgetSnapshot, type Feature, type GenerateChunk, type GenerateOutcome,
  type GenerateRequest, IdempotencyConflictError, isIdempotencyKey, NotSupportedError, OutcomeUnknownError, type PostOutcome,
  type PostRequest, type ZxvBackend,
} from './backend.js';
import { ExactRational, intFromWire, intToWire, type RationalInput } from './rational.js';

/** Where pending posts and remembered keys survive a restart (AsyncStorage, localStorage, a file...). */
export interface QueueStorage {
  load(): Promise<string | null>;
  save(data: string): Promise<void>;
}

/** Options of {@link ZxvClient}. */
export interface ClientOptions {
  storage?: QueueStorage;
  /** Make an idempotency key (default: 32 hex digits from crypto.getRandomValues, prefixed "zx-"). */
  newIdempotencyKey?: () => string;
  /** Replay attempt interval while calls are queued, in ms (default 5000; 0 = only on events and retry()). */
  retryIntervalMs?: number;
  /** How many completed post keys to remember (default 1000). */
  rememberKeys?: number;
}

/** Per-call options. */
export interface CallOptions {
  signal?: AbortSignal;
  /** Called when the call is queued because the backend is unreachable. */
  onQueued?: () => void;
}

/** What {@link ZxvClient.getState} reports. */
export interface ClientState {
  /** The backend is reachable as far as we know. */
  readonly online: boolean;
  /** Calls waiting in the queue. */
  readonly pending: number;
  /** A replay is running. */
  readonly replaying: boolean;
}

/** A transfer for {@link ZxvClient.ledgerPost}. */
export interface Transfer {
  from: number;
  to: number;
  /** Minor units: bigint, safe integer, or decimal string. */
  amount: bigint | number | string;
  currency: string;
  /** Default: a new key. Pass your own (e.g. an EndToEndId) to make a retry from your side safe too. */
  idempotencyKey?: string;
}

/** The outcome of a post. */
export interface Posted {
  readonly entryId: bigint;
  /** True when nothing new was posted (the key had been posted before). */
  readonly replayed: boolean;
  readonly idempotencyKey: string;
}

/** Exact balances. */
export interface Balance {
  readonly debit: bigint;
  readonly credit: bigint;
  readonly equity: bigint;
}

/** Exact budget state. */
export interface BudgetState {
  readonly remaining: ExactRational;
  readonly used: ExactRational;
}

/** A post from storage replayed after a restart, as reported to {@link ZxvClient.onReplay}. */
export interface ReplayEvent {
  readonly idempotencyKey: string;
  readonly outcome?: Posted;
  readonly error?: unknown;
}

type Kind = 'generate' | 'budgetStatus' | 'budgetConsume' | 'budgetRelease' | 'ledgerBalance' | 'ledgerPost';

interface Item {
  readonly seq: number;
  readonly kind: Kind;
  /** Replay is safe even if an earlier attempt may have been delivered. */
  readonly idempotent: boolean;
  readonly run: (signal: AbortSignal) => Promise<unknown>;
  readonly ctl: AbortController;
  readonly resolve: (v: unknown) => void;
  readonly reject: (e: unknown) => void;
  onQueued?: (() => void) | undefined;
  detach?: (() => void) | undefined;
}

interface Persisted {
  v: 1;
  posts: PostRequest[];
  done: [key: string, fingerprint: string, entryId: string][];
}

const FEATURE: Record<Kind, Feature> = {
  generate: 'generate', budgetStatus: 'budget', budgetConsume: 'budget', budgetRelease: 'budget', ledgerBalance: 'ledger', ledgerPost: 'ledger',
};

function defaultKey(): string {
  const c = (globalThis as { crypto?: { getRandomValues?: (a: Uint8Array) => Uint8Array } }).crypto;
  if (!c?.getRandomValues) {
    throw new Error('no crypto.getRandomValues: pass ClientOptions.newIdempotencyKey (React Native: react-native-get-random-values or expo-crypto)');
  }
  const b = c.getRandomValues(new Uint8Array(16));
  let h = 'zx-';
  for (const x of b) h += x.toString(16).padStart(2, '0');
  return h; // 35 characters, the ledger reference limit
}

const fingerprint = (r: Omit<PostRequest, 'idempotencyKey'>): string => `${r.from}>${r.to}:${r.amountMinor}:${r.currency}`;


/** The offline-first client. One per backend; the provider makes it. */
export class ZxvClient {
  readonly backend: ZxvBackend;
  /** Resolves once persisted posts are loaded (and queued for replay). */
  readonly ready: Promise<void>;
  readonly #storage: QueueStorage | undefined;
  readonly #newKey: () => string;
  readonly #retryMs: number;
  readonly #remember: number;
  #seq = 0;
  #queue: Item[] = [];
  #pumping = false;
  #suspect = false;
  #timer: ReturnType<typeof setTimeout> | null = null;
  #attached = 0;
  #unsubscribe: (() => void) | null = null;
  #state: ClientState;
  readonly #listeners = new Set<() => void>();
  readonly #replayListeners = new Set<(e: ReplayEvent) => void>();
  /** Posts not yet answered: persisted before sending. */
  readonly #open = new Map<string, { request: PostRequest; fp: string; promise: Promise<Posted> }>();
  /** Completed keys -> fingerprint and entry id (most recent last). */
  readonly #done = new Map<string, { fp: string; entryId: string }>();
  #saving: Promise<void> = Promise.resolve();

  constructor(backend: ZxvBackend, options: ClientOptions = {}) {
    this.backend = backend;
    this.#storage = options.storage;
    this.#newKey = options.newIdempotencyKey ?? defaultKey;
    this.#retryMs = options.retryIntervalMs ?? 5000;
    this.#remember = options.rememberKeys ?? 1000;
    this.#state = Object.freeze({ online: backend.isReachable(), pending: 0, replaying: false });
    this.ready = this.#load();
    this.ready.catch(() => undefined);
  }

  // ---- state ------------------------------------------------------------

  /** A stable snapshot (for useSyncExternalStore). */
  getState = (): ClientState => this.#state;

  /** State changes. */
  subscribe = (listener: () => void): (() => void) => {
    this.#listeners.add(listener);
    return () => this.#listeners.delete(listener);
  };

  /** Outcomes of posts replayed from storage (their original callers are gone). */
  onReplay(listener: (e: ReplayEvent) => void): () => void {
    this.#replayListeners.add(listener);
    return () => this.#replayListeners.delete(listener);
  }

  /**
   * Start following the backend's reachability (the provider calls this on
   * mount; it is reference counted, so StrictMode's double mount is fine).
   */
  attach(): () => void {
    if (this.#attached++ === 0) {
      this.#unsubscribe = this.backend.subscribe((reachable) => {
        if (reachable) this.#suspect = false;
        this.#emit();
        if (reachable) void this.#pump();
      });
      this.#emit();
      if (this.#queue.length) this.#schedule();
    }
    let done = false;
    return () => {
      if (done) return;
      done = true;
      if (--this.#attached === 0) {
        this.#unsubscribe?.();
        this.#unsubscribe = null;
        if (this.#timer) clearTimeout(this.#timer);
        this.#timer = null;
      }
    };
  }

  /** Replay the queue now. */
  retry(): Promise<void> {
    this.#suspect = false;
    return this.#pump();
  }

  /** A fresh idempotency key. */
  newIdempotencyKey(): string {
    const k = this.#newKey();
    if (!isIdempotencyKey(k)) throw new Error(`newIdempotencyKey returned ${JSON.stringify(k)}: need 1..35 of [A-Za-z0-9._:-]`);
    return k;
  }

  // ---- calls ------------------------------------------------------------

  /** Generate; `onChunk` sees the output as it streams (if the backend streams). Not replayed once delivered. */
  generate(request: GenerateRequest, onChunk: (c: GenerateChunk) => void, options: CallOptions = {}): Promise<GenerateOutcome> {
    return this.#submit('generate', false, (s) => this.backend.generate(request, onChunk, s), options) as Promise<GenerateOutcome>;
  }

  /** Exact budget state. */
  async budgetStatus(budget: string, options: CallOptions = {}): Promise<BudgetState> {
    const s = (await this.#submit('budgetStatus', true, (sig) => this.backend.budgetStatus(budget, sig), options)) as BudgetSnapshot;
    return Object.freeze({ remaining: ExactRational.fromWire(s.remaining), used: ExactRational.fromWire(s.used) });
  }

  /** Consume exactly `amount` (a float throws before anything is sent). */
  async budgetConsume(budget: string, amount: RationalInput, options: CallOptions = {}): Promise<void> {
    const a = ExactRational.from(amount, 'amount').toWire();
    await this.#submit('budgetConsume', false, (s) => this.backend.budgetConsume(budget, a, s), options);
  }

  /** Give back exactly `amount`. */
  async budgetRelease(budget: string, amount: RationalInput, options: CallOptions = {}): Promise<void> {
    const a = ExactRational.from(amount, 'amount').toWire();
    await this.#submit('budgetRelease', false, (s) => this.backend.budgetRelease(budget, a, s), options);
  }

  /** Exact balances in minor units. */
  async ledgerBalance(account: number, options: CallOptions = {}): Promise<Balance> {
    const b = (await this.#submit('ledgerBalance', true, (s) => this.backend.ledgerBalance(account, s), options)) as BalanceSnapshot;
    return Object.freeze({ debit: intFromWire(b.debit, 'debit'), credit: intFromWire(b.credit, 'credit'), equity: intFromWire(b.equity, 'equity') });
  }

  /** Post a transfer exactly once, online or not (see the file comment). */
  async ledgerPost(transfer: Transfer, options: CallOptions = {}): Promise<Posted> {
    const amountMinor = intToWire(transfer.amount, 'amount');
    if (amountMinor.startsWith('-') || amountMinor === '0') throw new RangeError('amount must be > 0 minor units');
    for (const k of ['from', 'to'] as const) {
      if (!Number.isSafeInteger(transfer[k]) || transfer[k] < 0) throw new TypeError(`${k}: an account id`);
    }
    const idempotencyKey = transfer.idempotencyKey ?? this.newIdempotencyKey();
    if (!isIdempotencyKey(idempotencyKey)) throw new TypeError(`idempotencyKey: 1..35 of [A-Za-z0-9._:-], got ${JSON.stringify(idempotencyKey)}`);
    const request: PostRequest = { from: transfer.from, to: transfer.to, amountMinor, currency: transfer.currency, idempotencyKey };
    const seq = ++this.#seq; // its place in line is now, not after the awaits below
    await this.ready;
    return this.#post(request, options, null, seq);
  }

  // ---- internals --------------------------------------------------------

  #post(request: PostRequest, options: CallOptions, fromStorage: ((e: ReplayEvent) => void) | null, seq: number): Promise<Posted> {
    const key = request.idempotencyKey;
    const fp = fingerprint(request);
    const done = this.#done.get(key);
    if (done) {
      if (done.fp !== fp) return Promise.reject(new IdempotencyConflictError(key));
      return Promise.resolve(Object.freeze({ entryId: intFromWire(done.entryId, 'entryId'), replayed: true, idempotencyKey: key }));
    }
    const open = this.#open.get(key);
    if (open) return open.fp === fp ? open.promise : Promise.reject(new IdempotencyConflictError(key));

    // registered before anything is awaited, so the persist below includes it
    const entry: { request: PostRequest; fp: string; promise: Promise<Posted> } = { request, fp, promise: Promise.resolve() as never };
    this.#open.set(key, entry);
    entry.promise = (async (): Promise<Posted> => {
      await this.#persist(); // the post is on disk before it can reach the ledger
      try {
        const out = (await this.#submit('ledgerPost', true, (s) => this.backend.ledgerPost(request, s), options, seq)) as PostOutcome;
        const entryId = intFromWire(out.entryId, 'entryId');
        this.#done.set(key, { fp, entryId: out.entryId });
        while (this.#done.size > this.#remember) this.#done.delete(this.#done.keys().next().value as string);
        const posted = Object.freeze({ entryId, replayed: out.replayed === true, idempotencyKey: key });
        fromStorage?.({ idempotencyKey: key, outcome: posted });
        return posted;
      } catch (e) {
        fromStorage?.({ idempotencyKey: key, error: e });
        throw e;
      } finally {
        this.#open.delete(key);
        await this.#persist();
      }
    })();
    return entry.promise;
  }

  #submit(kind: Kind, idempotent: boolean, run: (s: AbortSignal) => Promise<unknown>, options: CallOptions, seq = ++this.#seq): Promise<unknown> {
    if (!this.backend.features.has(FEATURE[kind])) return Promise.reject(new NotSupportedError(this.backend.name, FEATURE[kind]));
    const signal = options.signal;
    if (signal?.aborted) return Promise.reject(abortReason(signal));
    return new Promise((resolve, reject) => {
      const ctl = new AbortController();
      const item: Item = { seq, kind, idempotent, run, ctl, resolve, reject, onQueued: options.onQueued };
      if (signal) {
        const onAbort = (): void => {
          ctl.abort(abortReason(signal));
          const i = this.#queue.indexOf(item);
          if (i >= 0) {
            this.#queue.splice(i, 1);
            this.#emit();
          }
          reject(abortReason(signal));
        };
        signal.addEventListener('abort', onAbort, { once: true });
        item.detach = () => signal.removeEventListener('abort', onAbort);
      }
      if (this.#queue.length === 0 && !this.#pumping && this.#online()) void this.#attempt(item);
      else this.#enqueue(item);
    });
  }

  #online(): boolean {
    return this.backend.isReachable() && !this.#suspect;
  }

  async #attempt(item: Item): Promise<void> {
    try {
      const v = await item.run(item.ctl.signal);
      this.#settle(item, () => item.resolve(v));
      if (this.#suspect) {
        this.#suspect = false;
        this.#emit();
      }
    } catch (e) {
      if (item.ctl.signal.aborted) return this.#settle(item, () => item.reject(abortReason(item.ctl.signal)));
      if (e instanceof BackendUnavailableError) {
        this.#suspect = true;
        if (e.delivered === 'maybe' && !item.idempotent) return this.#settle(item, () => item.reject(new OutcomeUnknownError(item.kind)));
        return this.#enqueue(item);
      }
      this.#settle(item, () => item.reject(e));
    }
  }

  #settle(item: Item, f: () => void): void {
    item.detach?.();
    f();
  }

  #enqueue(item: Item): void {
    if (item.ctl.signal.aborted) return;
    let i = this.#queue.length;
    while (i > 0 && this.#queue[i - 1]!.seq > item.seq) i--;
    this.#queue.splice(i, 0, item);
    item.onQueued?.();
    item.onQueued = undefined;
    this.#emit();
    this.#schedule();
  }

  #remove(item: Item): void {
    const i = this.#queue.indexOf(item);
    if (i >= 0) this.#queue.splice(i, 1);
  }

  #schedule(): void {
    if (this.#timer || this.#retryMs <= 0 || this.#attached === 0 || this.#queue.length === 0) return;
    this.#timer = setTimeout(() => {
      this.#timer = null;
      void this.#pump();
    }, this.#retryMs);
  }

  async #pump(): Promise<void> {
    if (this.#pumping) return;
    this.#pumping = true;
    this.#emit();
    try {
      while (this.#queue.length) {
        const item = this.#queue[0]!;
        try {
          const v = await item.run(item.ctl.signal);
          this.#remove(item);
          this.#suspect = false;
          this.#settle(item, () => item.resolve(v));
        } catch (e) {
          if (item.ctl.signal.aborted) continue; // its abort handler already removed and rejected it
          if (e instanceof BackendUnavailableError) {
            this.#suspect = true;
            if (e.delivered === 'maybe' && !item.idempotent) {
              this.#remove(item);
              this.#settle(item, () => item.reject(new OutcomeUnknownError(item.kind)));
              continue;
            }
            break; // still unreachable: it stays first in line
          }
          this.#remove(item);
          this.#settle(item, () => item.reject(e));
        } finally {
          this.#emit();
        }
      }
    } finally {
      this.#pumping = false;
      this.#emit();
      this.#schedule();
    }
  }

  #emit(): void {
    const next = { online: this.#online(), pending: this.#queue.length, replaying: this.#pumping };
    const s = this.#state;
    if (s.online === next.online && s.pending === next.pending && s.replaying === next.replaying) return;
    this.#state = Object.freeze(next);
    for (const l of [...this.#listeners]) l();
  }

  async #load(): Promise<void> {
    if (!this.#storage) return;
    const text = await this.#storage.load();
    if (!text) return;
    const data = JSON.parse(text) as Persisted;
    if (data.v !== 1) throw new Error(`unknown queue storage version ${String(data.v)}`);
    for (const [key, fp, entryId] of data.done) this.#done.set(key, { fp, entryId });
    const report = (e: ReplayEvent): void => {
      for (const l of [...this.#replayListeners]) l(e);
    };
    for (const p of data.posts) {
      if (!isIdempotencyKey(p.idempotencyKey) || this.#done.has(p.idempotencyKey)) continue;
      this.#post(p, {}, report, ++this.#seq).catch(() => undefined);
    }
  }

  #persist(): Promise<void> {
    const storage = this.#storage;
    if (!storage) return Promise.resolve();
    const data: Persisted = {
      v: 1,
      posts: [...this.#open.values()].map((o) => o.request),
      done: [...this.#done].map(([k, d]) => [k, d.fp, d.entryId]),
    };
    this.#saving = this.#saving.then(() => storage.save(JSON.stringify(data)));
    return this.#saving;
  }
}
