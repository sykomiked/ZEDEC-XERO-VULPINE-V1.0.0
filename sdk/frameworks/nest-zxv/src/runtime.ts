// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

import { Cap, type Capability, type LoadOptions, Zxv, type ZxvContext, ZxvFatalError } from '@zedec/zxv';

import type { ResolvedZxvOptions } from './options.js';

/** What the runtime knows about its context. */
export type ZxvState = 'live' | 'halted' | 'closed' | 'unknown';

/**
 * Owns the one `ZxvContext` of an application, its root capability and the
 * named capabilities attenuated from it. Not part of the public API: the
 * root lives in a private field and is never handed out.
 *
 * Teardown follows ABI.md R6: named capabilities (children) first, in
 * reverse order of creation, then the root, then the context.
 */
export class ZxvRuntime {
  readonly zxv: Zxv;
  readonly context: ZxvContext;
  readonly #root: Capability;
  readonly #named: ReadonlyMap<string, Capability>;
  #fatalReported = false;
  readonly #onFatal: ((e: ZxvFatalError | undefined) => void) | undefined;

  private constructor(zxv: Zxv, context: ZxvContext, root: Capability, named: ReadonlyMap<string, Capability>, onFatal: ResolvedZxvOptions['onFatal']) {
    this.zxv = zxv;
    this.context = context;
    this.#root = root;
    this.#named = named;
    this.#onFatal = onFatal;
  }

  /** Loads libzxv, creates the context, enables currencies and attenuates the named capabilities. */
  static async create(o: ResolvedZxvOptions): Promise<ZxvRuntime> {
    const load: LoadOptions = o.libraryPath === undefined ? { backend: o.backend } : { backend: o.backend, libraryPath: o.libraryPath };
    const zxv = await Zxv.load(load);
    const context = zxv.createContext({ fatalPolicy: o.fatalPolicy });
    try {
      if (o.currencies.length > 0 || o.privateUnits.length > 0) {
        // Configuration needs LEDGER_POST whatever the root holds: a
        // short-lived capability of its own, closed before anything else exists.
        using setup = context.rootCapability({ caps: Cap.LEDGER_POST });
        for (const code of o.currencies) context.enableCurrency(setup, code);
        for (const p of o.privateUnits) context.registerPrivateUnit(setup, p.code, p.minorUnits);
      }

      const root = context.rootCapability({ caps: o.rootRights });
      const named = new Map<string, Capability>();
      for (const [name, bits] of o.capabilities) named.set(name, root.attenuate(bits));
      return new ZxvRuntime(zxv, context, root, named, o.onFatal);
    } catch (e) {
      context.close();
      throw e;
    }
  }

  /** The declared capability names, in declaration order. */
  get names(): readonly string[] {
    return [...this.#named.keys()];
  }

  /** A named capability; never the root. */
  capability(name: string): Capability {
    const c = this.#named.get(name);
    if (c === undefined) throw new Error(`No ZXV capability named '${name}' is declared`);
    return c;
  }

  /** The context's state, without throwing. Fail-closed: anything unexpected is 'unknown'. */
  state(): ZxvState {
    try {
      if (this.context.closed) return 'closed';
      return this.context.isPoisoned ? 'halted' : 'live';
    } catch {
      return 'unknown';
    }
  }

  /** Reports a halted context once to the `onFatal` hook. */
  reportFatal(error?: ZxvFatalError): void {
    if (this.#fatalReported) return;
    this.#fatalReported = true;
    try {
      this.#onFatal?.(error instanceof ZxvFatalError ? error : undefined);
    } catch {
      // The hook must not throw; a failure here must not mask the fatal path.
    }
  }

  /** Closes named capabilities (reverse order), then the root, then the context. Idempotent. */
  close(): void {
    for (const c of [...this.#named.values()].reverse()) c.close();
    this.#root.close();
    this.context.close();
  }
}
