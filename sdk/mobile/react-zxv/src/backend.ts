// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The backend interface the hooks run on. A backend is injected: z-web in a
// browser, the @zedec/zxv addon (or wasm) in-process, or the local zxvd
// daemon over JSON-RPC from React Native. Everything a backend takes and
// returns is a string where exactness matters, using zxvd's conventions:
// rationals as "num/den", 64-bit integers as decimal strings. That keeps
// backends transport-neutral and lets the offline queue persist calls as
// JSON without losing a digit.
//
// Capabilities are bound when a backend is constructed (the adapter is
// handed the capability it presents); the hooks never widen them.

/** What a backend can do. */
export type Feature = 'generate' | 'stream' | 'cancel' | 'budget' | 'ledger';

/** A generation request (exact sampling parameters). */
export interface GenerateRequest {
  readonly prompt: string;
  readonly maxTokens?: number;
  /** "num/den". */
  readonly temperature?: string;
  readonly topK?: number;
  /** "num/den". */
  readonly topP?: string;
  /** Decimal string. */
  readonly seed?: string;
}

/** A piece of streamed output. */
export interface GenerateChunk {
  readonly text: string;
  /** Token id, when the backend streams tokens. */
  readonly id?: number;
}

/** A finished generation. */
export interface GenerateOutcome {
  readonly text: string;
  /** EOS, MAX_TOKENS, CANCELLED, BUDGET, ... (backend's names). */
  readonly stop: string;
  readonly tokens?: readonly number[];
}

/** Exact budget state, "num/den". */
export interface BudgetSnapshot {
  readonly remaining: string;
  readonly used: string;
}

/** Balances in minor units, decimal strings. */
export interface BalanceSnapshot {
  readonly debit: string;
  readonly credit: string;
  readonly equity: string;
}

/** A transfer to post exactly once. */
export interface PostRequest {
  readonly from: number;
  readonly to: number;
  /** Minor units, decimal string, > 0. */
  readonly amountMinor: string;
  readonly currency: string;
  /**
   * Unique per ledger, 1..35 characters of [A-Za-z0-9._:-]. The backend uses
   * it as the posting's reference, so the ledger itself refuses a second
   * posting with it.
   */
  readonly idempotencyKey: string;
}

/** The posting. `replayed` is true when the key had already been posted (nothing new was posted). */
export interface PostOutcome {
  readonly entryId: string;
  readonly replayed: boolean;
}

/**
 * A backend. Implementations must:
 * - throw {@link BackendUnavailableError} when the backend cannot be reached,
 *   saying whether the request may have been delivered;
 * - make `ledgerPost` idempotent on `idempotencyKey`: a key already posted
 *   returns the original entry with `replayed: true` and posts nothing.
 */
export interface ZxvBackend {
  readonly name: string;
  readonly features: ReadonlySet<Feature>;
  /** Reachable right now, as far as the backend knows. */
  isReachable(): boolean;
  /** Reachability changes. Returns an unsubscribe function. */
  subscribe(listener: (reachable: boolean) => void): () => void;
  generate(request: GenerateRequest, onChunk: (chunk: GenerateChunk) => void, signal: AbortSignal): Promise<GenerateOutcome>;
  budgetStatus(budget: string, signal: AbortSignal): Promise<BudgetSnapshot>;
  budgetConsume(budget: string, amount: string, signal: AbortSignal): Promise<void>;
  budgetRelease(budget: string, amount: string, signal: AbortSignal): Promise<void>;
  ledgerBalance(account: number, signal: AbortSignal): Promise<BalanceSnapshot>;
  ledgerPost(request: PostRequest, signal: AbortSignal): Promise<PostOutcome>;
}

/**
 * The backend cannot be reached. `delivered` says whether the request may
 * have arrived: "no" (it was never sent) or "maybe" (sent, no answer).
 */
export class BackendUnavailableError extends Error {
  readonly delivered: 'no' | 'maybe';
  constructor(message: string, delivered: 'no' | 'maybe' = 'no') {
    super(message);
    this.name = 'BackendUnavailableError';
    this.delivered = delivered;
  }
}

/** libzxv status class names (zxv_status_class). */
export type StatusClassName = 'USAGE' | 'REFUSED' | 'FATAL' | 'SYSTEM';

/** A call the backend answered with an error: libzxv status, or a protocol error. */
export class ZxvCallError extends Error {
  readonly kind: string;
  readonly status: number | undefined;
  readonly statusName: string | undefined;
  readonly statusClass: StatusClassName | undefined;
  /** JSON-RPC error code, for zxvd. */
  readonly code: number | undefined;

  constructor(message: string, init: { kind?: string; status?: number; statusName?: string; statusClass?: StatusClassName; code?: number } = {}) {
    super(message);
    this.name = 'ZxvCallError';
    this.kind = init.kind ?? 'Error';
    this.status = init.status;
    this.statusName = init.statusName;
    this.statusClass = init.statusClass;
    this.code = init.code;
  }

  /** The context is poisoned (FATAL): never retry. */
  get fatal(): boolean {
    return this.statusClass === 'FATAL';
  }
}

/** The backend does not do this (e.g. budgets over zxvd). */
export class NotSupportedError extends Error {
  constructor(backend: string, feature: Feature) {
    super(`${backend} does not support ${feature}`);
    this.name = 'NotSupportedError';
  }
}

/**
 * A non-idempotent call (budget consume/release, generation) was sent and
 * the backend became unreachable before answering. It is NOT replayed: it
 * may or may not have taken effect. Check the state, then decide.
 */
export class OutcomeUnknownError extends Error {
  constructor(what: string) {
    super(`${what}: sent, but the backend went away before answering; not replayed (it may have taken effect)`);
    this.name = 'OutcomeUnknownError';
  }
}

/** The same idempotency key was used for a different transfer. */
export class IdempotencyConflictError extends Error {
  constructor(key: string) {
    super(`idempotency key ${key} was already used for a different transfer`);
    this.name = 'IdempotencyConflictError';
  }
}

const KEY = /^[A-Za-z0-9._:-]{1,35}$/;

/** True for a usable idempotency key (it becomes a ledger reference, at most 35 bytes). */
export function isIdempotencyKey(key: unknown): key is string {
  return typeof key === 'string' && KEY.test(key);
}

/**
 * The reason of an aborted signal, or an AbortError. React Native's
 * AbortController polyfill has no `reason` and no `throwIfAborted()`, so
 * the package uses these instead.
 */
export function abortReason(signal: AbortSignal): unknown {
  return (signal as { reason?: unknown }).reason ?? Object.assign(new Error('aborted'), { name: 'AbortError' });
}

/** Throw the abort reason if `signal` is aborted. */
export function throwIfAborted(signal: AbortSignal | undefined): void {
  if (signal?.aborted) throw abortReason(signal);
}

/** ZXV_E_DUPLICATE: a reference already posted. */
export const E_DUPLICATE = -13;

/** Simple reachability state for adapters. */
export class Reachability {
  #reachable: boolean;
  readonly #listeners = new Set<(r: boolean) => void>();
  constructor(initial = true) {
    this.#reachable = initial;
  }
  get(): boolean {
    return this.#reachable;
  }
  set(r: boolean): void {
    if (r === this.#reachable) return;
    this.#reachable = r;
    for (const l of [...this.#listeners]) l(r);
  }
  subscribe(l: (r: boolean) => void): () => void {
    this.#listeners.add(l);
    return () => this.#listeners.delete(l);
  }
}
