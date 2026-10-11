// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

import type { Capability, ZxvContext } from './context.js';
import { usage } from './errors.js';
import { type Ptr, cstr } from './internal/backend.js';
import { bytes, i32, str, u32 } from './internal/check.js';
import { newHandle, Resource } from './internal/core.js';
import { Truth } from './truth.js';

/** What an attestation is about (the kernel's lpres_subject_type_t). */
export const Subject = Object.freeze({
  SOURCE: 0, DIALECT: 1, DEPENDENCY: 2, RUNTIME: 3, DATA: 4, SYMBOL: 5, MODEL_PACK: 6, GEOMETRY: 7,
  PARAMETER: 8, SENSOR: 9, ACTUATOR: 10, CALIBRATION: 11, FORMAT: 12, KEY: 13, FIRMWARE: 14,
} as const);
/** A Subject value. */
export type Subject = (typeof Subject)[keyof typeof Subject];

/** The state of one attestation and its evidence counts. */
export interface AttestationState {
  readonly truth: Truth;
  readonly positive: number;
  readonly negative: number;
}

/** Options of {@link Attestations.attest}. */
export interface AttestOptions {
  /** Who attests (<= 31 bytes). */
  attester: string;
  /** What kind of thing (default DATA). */
  subject?: Subject;
  /** Initial state (default TRUE). Attesting TRUE counts as one positive piece of evidence. */
  state?: Truth;
  /** Optional digest of the subject, <= 32 bytes. */
  digest?: Uint8Array;
}

/**
 * An attestation registry (kernel lpres): named facts, each holding a
 * four-valued {@link Truth} computed from positive and negative evidence.
 * Contradictory evidence is held as BOTH, never collapsed. Writing needs
 * {@link Cap.ATTEST}. The actuator {@link Attestations.gate} is clear
 * only when every attestation is certain (TRUE or FALSE).
 */
export class Attestations extends Resource {
  /** The capability writes present. */
  readonly cap: Capability;
  readonly #ctx: ZxvContext;
  #ptr: Ptr;

  /** @internal Use {@link Attestations.create}. */
  constructor(cap: Capability, ptr: Ptr) {
    super(cap.context);
    this.cap = cap;
    this.#ctx = cap.context;
    this.#ptr = ptr;
  }

  /** A new registry in `cap`'s context, written through `cap`. */
  static create(cap: Capability): Attestations {
    const c = cap.context;
    return new Attestations(cap, newHandle(c._core, c._state, 'zxv_lpres_create_cap', cap._ptr()));
  }

  #p(): Ptr {
    this.live();
    return this.#ptr;
  }

  /** Record an attestation; returns its index. `name` <= 31 bytes. */
  attest(name: string, options: AttestOptions): number {
    const st = options.state ?? Truth.TRUE;
    if (!(st instanceof Truth)) throw usage('state must be a Truth value');
    const dg = options.digest === undefined ? null : bytes('digest', options.digest);
    if (dg && dg.length > 32) throw usage('digest is at most 32 bytes');
    const idx = new Uint32Array(1);
    this.#ctx._core.call(this.#ctx._state, 'zxv_lpres_attest', this.cap._ptr(), this.#p(), cstr(str('name', name)),
      i32('subject', options.subject ?? Subject.DATA), st.value, cstr(str('attester', options.attester)), dg, dg ? dg.length : 0, idx);
    return idx[0] ?? 0;
  }

  /** Add one piece of evidence (positive or negative); the state follows {@link Truth.fromEvidence}. */
  evidence(index: number, positive: boolean): Truth {
    if (typeof positive !== 'boolean') throw usage('positive must be a boolean');
    this.#ctx._core.call(this.#ctx._state, 'zxv_lpres_evidence', this.cap._ptr(), this.#p(), u32('index', index), positive ? 1 : 0);
    return this.state(index).truth;
  }

  /** Index of a named attestation, or undefined. */
  find(name: string): number | undefined {
    const idx = new Uint32Array(1);
    const s = this.#ctx._core.callAllowing(this.#ctx._state, [-4], 'zxv_lpres_find', this.#p(), cstr(str('name', name)), idx);
    return s === 0 ? idx[0] : undefined;
  }

  /** State and evidence counts of one attestation. */
  state(index: number): AttestationState {
    const st = new Int32Array(1);
    const p = new Uint32Array(1);
    const n = new Uint32Array(1);
    this.#ctx._core.call(this.#ctx._state, 'zxv_lpres_state', this.#p(), u32('index', index), st, p, n);
    return Object.freeze({ truth: Truth.of(st[0] ?? 0), positive: p[0] ?? 0, negative: n[0] ?? 0 });
  }

  /**
   * The actuator gate: TRUE when every attestation is certain (no BOTH, no
   * NEITHER), FALSE otherwise. Actuator authority requires a clear gate.
   */
  gate(): Truth {
    const clear = new Int32Array(1);
    this.#ctx._core.call(this.#ctx._state, 'zxv_lpres_gate', this.#p(), clear);
    return Truth.fromBoolean((clear[0] ?? 0) === 1);
  }

  protected destroy(): void {
    const p = this.#ptr;
    this.#ctx._state.release(() => this.#ctx._core.b.call('zxv_lpres_destroy', p));
  }
}
