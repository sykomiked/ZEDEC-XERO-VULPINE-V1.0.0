// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

import type { Capability, ZxvContext } from './context.js';
import { BudgetExhaustedError, usage } from './errors.js';
import { type Ptr, cstr } from './internal/backend.js';
import { i32, type IntLike, str, u32 } from './internal/check.js';
import { newHandle, Resource } from './internal/core.js';
import { Rational, toRational } from './rational.js';

/** Resource kinds of a budget (the kernel's rmag_resource_t). */
export const BudgetResource = Object.freeze({
  COMPUTE_CYCLES: 0,
  MEMORY: 1,
  BANDWIDTH: 2,
  ENERGY: 3,
  STORAGE: 4,
  /** One model token = one event (RMAG_RES_EVENT_COUNT). */
  TOKENS: 12,
} as const);
/** A BudgetResource value. */
export type BudgetResource = (typeof BudgetResource)[keyof typeof BudgetResource];

/** An exact amount: a Rational, an integer (bigint or integer number), or exact decimal text. */
export type Amount = Rational | IntLike | string;

/** Exact state of one budget. */
export interface BudgetStatus {
  /** limit - consumed. */
  readonly remaining: Rational;
  readonly used: Rational;
  /** Counts replenishments (release / larger limit), so a waiter can tell something changed. */
  readonly epoch: bigint;
}

/** Options of {@link Budget.consumeAsync}. */
export interface ConsumeAsyncOptions {
  /** Cancel the wait; the promise rejects with the signal's reason. */
  signal?: AbortSignal;
  /** Give up after this many milliseconds (integer); rejects with BudgetExhaustedError. */
  timeoutMs?: number;
}

/** A denied consume, as reported to {@link Budget.onExhausted} listeners. */
export interface ExhaustedEvent {
  readonly budget: Budget;
  readonly amount: Rational;
}

/** Options of {@link Budgets.create}. */
export interface BudgetOptions {
  /** What the budget meters (default TOKENS). */
  resource?: BudgetResource;
  /** false = only count, never deny (default true). */
  enforce?: boolean;
}

/**
 * A set of rational resource budgets (kernel rmag, K2) in one context,
 * used through one capability holding {@link Cap.BUDGET}.
 */
export class Budgets extends Resource {
  /** The capability every gated call presents. */
  readonly cap: Capability;
  /** @internal */
  readonly _ctx: ZxvContext;
  #ptr: Ptr;
  /** @internal */
  readonly _waiters = new Set<() => void>();

  /** @internal Use {@link Budgets.create}. */
  constructor(cap: Capability, ptr: Ptr) {
    super(cap.context);
    this.cap = cap;
    this._ctx = cap.context;
    this.#ptr = ptr;
  }

  /** A new budget set in `cap`'s context. */
  static open(cap: Capability): Budgets {
    const c = cap.context;
    return new Budgets(cap, newHandle(c._core, c._state, 'zxv_rmag_create_cap', cap._ptr()));
  }

  /** @internal */
  _p(): Ptr {
    this.live();
    return this.#ptr;
  }

  /** Create a budget with an exact limit (>= 0). */
  create(name: string, limit: Amount, options: BudgetOptions = {}): Budget {
    const l = toRational('limit', limit);
    const id = new Uint32Array(1);
    this._ctx._core.call(this._ctx._state, 'zxv_rmag_budget', this.cap._ptr(), this._p(), cstr(str('name', name)),
      i32('resource', options.resource ?? BudgetResource.TOKENS), l.num, l.den, options.enforce === false ? 0 : 1, id);
    return new Budget(this, id[0] ?? 0, name);
  }

  /** @internal Wake every consumeAsync waiter (after a replenishment through this SDK). */
  _notify(): void {
    for (const w of [...this._waiters]) w();
  }

  protected destroy(): void {
    const p = this.#ptr;
    this._notify();
    this._ctx._state.release(() => this._ctx._core.b.call('zxv_rmag_destroy', p));
  }
}

/**
 * One rational budget. `consume` throws {@link BudgetExhaustedError}
 * (REFUSED: retry after a release) when an enforced limit would be passed
 * (nothing is consumed), and {@link ZxvUsageError} with ZXV_E_RMAG_INVALID
 * for a negative or overflowing amount (never retry the same value).
 */
export class Budget {
  /** The owning set. */
  readonly budgets: Budgets;
  /** Budget id inside the set. */
  readonly id: number;
  /** Name given at creation. */
  readonly name: string;
  readonly #listeners = new Set<(e: ExhaustedEvent) => void>();

  /** @internal */
  constructor(budgets: Budgets, id: number, name: string) {
    this.budgets = budgets;
    this.id = id;
    this.name = name;
  }

  #call(fn: string, amount: Rational): void {
    const b = this.budgets;
    b._ctx._core.call(b._ctx._state, fn, b.cap._ptr(), b._p(), this.id, amount.num, amount.den);
  }

  /** Consume an exact amount (> 0). */
  consume(amount: Amount): void {
    const a = toRational('amount', amount);
    try {
      this.#call('zxv_rmag_consume', a);
    } catch (e) {
      if (e instanceof BudgetExhaustedError) for (const l of [...this.#listeners]) l({ budget: this, amount: a });
      throw e;
    }
  }

  /**
   * Consume, waiting while the budget is exhausted: resolves when the
   * consume succeeds; on exhaustion it waits until a {@link Budget.release}
   * or a larger {@link Budget.setLimit} through this SDK replenishes the
   * set, then retries. Rejects at once on a usage error (INVALID), on
   * `signal` abort, or with BudgetExhaustedError after `timeoutMs`.
   */
  async consumeAsync(amount: Amount, options: ConsumeAsyncOptions = {}): Promise<void> {
    const a = toRational('amount', amount);
    const deadline = options.timeoutMs === undefined ? undefined : u32('timeoutMs', options.timeoutMs);
    const signal = options.signal;
    let timer: ReturnType<typeof setTimeout> | undefined;
    let timedOut = false;
    let wake: (() => void) | undefined;
    const kick = () => wake?.();
    if (deadline !== undefined) timer = setTimeout(() => { timedOut = true; kick(); }, deadline);
    signal?.addEventListener('abort', kick);
    try {
      for (;;) {
        signal?.throwIfAborted();
        const epoch = this.status().epoch;
        try {
          this.#call('zxv_rmag_consume', a);
          return;
        } catch (e) {
          if (!(e instanceof BudgetExhaustedError)) throw e;
          for (const l of [...this.#listeners]) l({ budget: this, amount: a });
          if (timedOut) throw e;
          if (this.status().epoch !== epoch) continue; // replenished meanwhile
          await new Promise<void>((ok) => {
            wake = ok;
            this.budgets._waiters.add(ok);
          });
          if (wake) this.budgets._waiters.delete(wake);
          wake = undefined;
          if (timedOut) {
            throw new BudgetExhaustedError(-19, 'ZXV_E_BUDGET', `budget "${this.name}" still exhausted after ${deadline} ms`);
          }
          if (this.budgets.closed) throw usage('the Budgets set was closed while waiting');
        }
      }
    } finally {
      if (timer !== undefined) clearTimeout(timer);
      signal?.removeEventListener('abort', kick);
      if (wake) this.budgets._waiters.delete(wake);
    }
  }

  /** Give back an amount (clamped at zero consumed); wakes waiters. */
  release(amount: Amount): void {
    this.#call('zxv_rmag_release', toRational('amount', amount));
    this.budgets._notify();
  }

  /** Replace the limit; growing it wakes waiters. */
  setLimit(limit: Amount): void {
    this.#call('zxv_rmag_set_limit', toRational('limit', limit));
    this.budgets._notify();
  }

  /** Remaining, used and epoch, exactly. */
  status(): BudgetStatus {
    const b = this.budgets;
    const rn = new BigInt64Array(1);
    const rd = new BigInt64Array(1);
    const un = new BigInt64Array(1);
    const ud = new BigInt64Array(1);
    const ep = new BigUint64Array(1);
    b._ctx._core.call(b._ctx._state, 'zxv_rmag_status', b._p(), this.id, rn, rd, un, ud, ep);
    return Object.freeze({
      remaining: Rational.of(rn[0] ?? 0n, rd[0] ?? 1n),
      used: Rational.of(un[0] ?? 0n, ud[0] ?? 1n),
      epoch: ep[0] ?? 0n,
    });
  }

  /** Call `listener` for every denied consume through this SDK. Returns an unsubscribe function. */
  onExhausted(listener: (e: ExhaustedEvent) => void): () => void {
    this.#listeners.add(listener);
    return () => this.#listeners.delete(listener);
  }
}
