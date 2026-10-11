// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

import { Inject, Injectable } from '@nestjs/common';
import type { Capability } from '@zedec/zxv';

import type { ZxvRuntime } from './runtime.js';
import { ZXV_RUNTIME } from './tokens.js';

/**
 * The named capabilities, for code that picks one at run time. Never
 * returns the root: there is no name for it.
 */
@Injectable()
export class ZxvCapabilities {
  readonly #runtime: ZxvRuntime;

  constructor(@Inject(ZXV_RUNTIME) runtime: ZxvRuntime) {
    this.#runtime = runtime;
  }

  /** The declared names, in declaration order. */
  get names(): readonly string[] {
    return this.#runtime.names;
  }

  /** The capability declared as `name`; throws for an unknown name. */
  get(name: string): Capability {
    return this.#runtime.capability(name);
  }
}
