// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// Status mapping, the R4 two-call pattern, and handle lifetimes, shared by
// every public class.

import { errorFor, fatal, type FatalPolicy, Status, StatusClass, usage, ZxvFatalError, ZxvSystemError } from '../errors.js';
import { type Arg, type Backend, E_BUFFER, fromUtf8, type Ptr } from './backend.js';

/** Where a failing call happened: a context (FATAL poisons it) or nowhere. */
export interface Scope {
  readonly core: Core;
  /** Throw if this scope can no longer be used. */
  ensureAlive(): void;
  /** Record a FATAL status; returns the error to throw (or aborts). */
  onFatal(err: ZxvFatalError): ZxvFatalError;
}

/** The backend plus status mapping. One per loaded library. */
export class Core implements Scope {
  readonly core: Core = this;

  constructor(readonly b: Backend) {}

  ensureAlive(): void {}

  onFatal(err: ZxvFatalError): ZxvFatalError {
    return err;
  }

  /** Throw the mapped error for a non-zero status. */
  fail(status: number, scope: Scope, detailFromWorker?: string): never {
    const detail = detailFromWorker ?? this.b.lastError();
    const name = this.b.statusName(status);
    const cls = this.b.call('zxv_status_class', status);
    const err = errorFor(status, cls, name, detail);
    if (err instanceof ZxvFatalError) throw scope.onFatal(err);
    throw err;
  }

  /** Call in a scope; throw on any non-zero status. */
  call(scope: Scope, fn: string, ...args: Arg[]): void {
    scope.ensureAlive();
    const s = this.b.call(fn, ...args);
    if (s !== 0) this.fail(s, scope);
  }

  /** Call in a scope; return the status if it is one of `allowed`, else throw. */
  callAllowing(scope: Scope, allowed: readonly number[], fn: string, ...args: Arg[]): number {
    scope.ensureAlive();
    const s = this.b.call(fn, ...args);
    if (s !== 0 && !allowed.includes(s)) this.fail(s, scope);
    return s;
  }

  /**
   * The R4 two-call pattern for a text result: `build(buf, cap, lenSlot)`
   * returns the argument list with the buffer, capacity and length slot in
   * place.
   */
  text(scope: Scope, fn: string, build: (buf: Uint8Array | null, cap: number, len: BigUint64Array | Uint32Array) => Arg[]): string {
    const out = this.bytes(scope, fn, (buf, cap, len) => build(buf, cap, len), 1);
    return fromUtf8(out);
  }

  /** The two-call pattern for a byte result (`extra` = 1 for a NUL). */
  bytes(scope: Scope, fn: string, build: (buf: Uint8Array | null, cap: number, len: BigUint64Array | Uint32Array) => Arg[], extra = 0): Uint8Array {
    scope.ensureAlive();
    const len = this.b.sizeSlot();
    let s = this.b.call(fn, ...build(null, 0, len));
    if (s === 0) return new Uint8Array(0);
    if (s !== E_BUFFER) this.fail(s, scope);
    for (let attempt = 0; attempt < 3; attempt++) {
      const n = this.b.readSize(len);
      const buf = new Uint8Array(n + Math.max(extra, 1));
      s = this.b.call(fn, ...build(buf, buf.length, len));
      if (s === 0) return buf.subarray(0, this.b.readSize(len));
      if (s !== E_BUFFER) this.fail(s, scope);
    }
    throw new ZxvSystemError(Status.E_INTERNAL, 'ZXV_E_INTERNAL', `${fn}: result kept growing`);
  }
}

/**
 * A C handle with an owner. Closing is idempotent, closes dependents
 * first (R6: a child is destroyed before its parent), and wipes nothing
 * by itself; subclasses release their handle in `destroy`.
 */
export abstract class Resource implements Disposable {
  #closed = false;
  #parent: Resource | null;
  readonly #dependents = new Set<Resource>();

  protected constructor(parent: Resource | null) {
    this.#parent = parent;
    if (parent) parent.#dependents.add(this);
  }

  /** Release the C handle (called once, after every dependent closed). */
  protected abstract destroy(): void;

  /** True once {@link Resource.close} ran. */
  get closed(): boolean {
    return this.#closed;
  }

  /** Throw a usage error if closed. */
  protected live(): void {
    if (this.#closed) throw usage(`${this.constructor.name} is closed`);
  }

  /** Release the handle (and every dependent first). Idempotent. */
  close(): void {
    if (this.#closed) return;
    for (const d of [...this.#dependents].reverse()) d.close();
    this.#closed = true;
    if (this.#parent) this.#parent.#dependents.delete(this);
    this.#parent = null;
    this.destroy();
  }

  /** `using` support: same as {@link Resource.close}. */
  [Symbol.dispose](): void {
    this.close();
  }
}

/** Policy plus FATAL state of a context; shared by everything in it. */
export class ContextState implements Scope {
  dead: ZxvFatalError | null = null;
  closed = false;
  /** Generations running on worker threads that use handles of this context. */
  #pending = 0;
  #queue: (() => void)[] = [];

  constructor(readonly core: Core, readonly policy: FatalPolicy) {}

  /** A worker-thread job starts using this context's handles. */
  begin(): void {
    this.#pending++;
  }

  /** It ended: run the C destroys that waited for it. */
  end(): void {
    if (--this.#pending > 0) return;
    const q = this.#queue;
    this.#queue = [];
    for (const f of q) f();
  }

  /** Run a C destroy now, or after the running jobs if any (in order). */
  release(destroy: () => void): void {
    if (this.#pending > 0) this.#queue.push(destroy);
    else destroy();
  }

  ensureAlive(): void {
    if (this.closed) throw usage('the ZxvContext is closed');
    if (this.dead) throw fatal(this.policy, new ZxvFatalError(Status.E_POISONED, 'ZXV_E_POISONED', this.dead.detail));
  }

  onFatal(err: ZxvFatalError): ZxvFatalError {
    this.dead ??= err;
    return fatal(this.policy, err);
  }
}

/** A handle out-slot helper: call `fn(...args, slot)` and return the handle. */
export function newHandle(core: Core, scope: Scope, fn: string, ...args: Arg[]): Ptr {
  const slot = core.b.handleSlot();
  core.call(scope, fn, ...args, slot);
  return core.b.readHandle(slot);
}

export { StatusClass };
