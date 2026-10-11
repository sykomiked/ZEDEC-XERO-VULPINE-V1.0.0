// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

import { Inject, Injectable } from '@nestjs/common';
import { type HealthIndicatorResult, HealthIndicatorService } from '@nestjs/terminus';

import { ZxvFatalState } from './fatal-state.js';
import { rightNames } from './options.js';
import type { ZxvRuntime } from './runtime.js';
import { ZXV_RUNTIME } from './tokens.js';

/**
 * A `@nestjs/terminus` health indicator for the ZXV context, fail-closed:
 * `up` only when libzxv answers that the context is live; `down` when it
 * is halted, closed, or does not answer. Finding it halted trips
 * {@link ZxvFatalState}.
 *
 * Data: state, ABI version, backend, and per named capability its rights,
 * attestation and revocation. No key material, ledger data or halt reason.
 *
 * ```ts
 * @Get('health') @HealthCheck()
 * check() { return this.health.check([() => this.zxv.isHealthy()]); }
 * ```
 */
@Injectable()
export class ZxvHealthIndicator {
  readonly #indicators: HealthIndicatorService;
  readonly #runtime: ZxvRuntime;
  readonly #fatal: ZxvFatalState;

  constructor(indicators: HealthIndicatorService, @Inject(ZXV_RUNTIME) runtime: ZxvRuntime, fatal: ZxvFatalState) {
    this.#indicators = indicators;
    this.#runtime = runtime;
    this.#fatal = fatal;
  }

  /** The indicator result under `key` (default `zxv`). */
  isHealthy<const Key extends string = 'zxv'>(key?: Key): HealthIndicatorResult<Key> {
    const session = this.#indicators.check((key ?? 'zxv') as Key);
    const state = this.#runtime.state();
    if (state === 'halted') this.#fatal.trip('ZXV_E_POISONED');
    if (state !== 'live') return session.down({ state });

    const capabilities: Record<string, string> = {};
    for (const name of this.#runtime.names) {
      capabilities[name] = this.#describe(name);
    }

    const v = this.#runtime.zxv.version;
    return session.up({
      state,
      abi: `${v.major}.${v.minor}.${v.patch}`,
      backend: this.#runtime.zxv.backend,
      capabilities,
    });
  }

  #describe(name: string): string {
    try {
      const info = this.#runtime.capability(name).info();
      return `rights=${rightNames(info.caps).join('|')}; attestation=${info.attestation.name}; revoked=${info.revoked}`;
    } catch {
      return 'unavailable';
    }
  }
}
