// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

import { Inject, Injectable } from '@nestjs/common';
import type { ZxvFatalError } from '@zedec/zxv';

import type { ZxvRuntime } from './runtime.js';
import { ZXV_RUNTIME } from './tokens.js';

/**
 * Whether this application has seen its context halted. Tripped by the
 * exception filter and the health indicator; it never resets (a halted
 * context cannot recover; restart the process).
 */
@Injectable()
export class ZxvFatalState {
  readonly #runtime: ZxvRuntime;
  #status: string | undefined;

  constructor(@Inject(ZXV_RUNTIME) runtime: ZxvRuntime) {
    this.#runtime = runtime;
  }

  /** True once a halted context was seen. */
  get isTripped(): boolean {
    return this.#status !== undefined;
  }

  /** The status name that tripped it (`ZXV_E_POISONED`, `ZXV_E_INVARIANT`), if any. */
  get status(): string | undefined {
    return this.#status;
  }

  /** Records the halt and calls the `onFatal` option once. */
  trip(status: string, error?: ZxvFatalError): void {
    this.#status ??= status;
    this.#runtime.reportFatal(error);
  }
}
