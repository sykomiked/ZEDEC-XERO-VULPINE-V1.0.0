// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

import type { Capability, ZxvContext } from './context.js';
import type { Ptr } from './internal/backend.js';
import { type IntLike, u32, u64 } from './internal/check.js';
import { type Core, newHandle, Resource } from './internal/core.js';

/** Options of {@link Swarm.create}. */
export interface SwarmOptions {
  /** Fibonacci levels (level d holds at most F(d+2) models). */
  levels: number;
  /** Tokens shared out each cycle. */
  tokensPerCycle: IntLike;
}

/** One model's share of the current cycle. */
export interface Allotment {
  /** Real + imaginary tokens allotted this cycle. */
  readonly allotted: bigint;
  /** The imaginary (emotional) part. */
  readonly imaginary: bigint;
  /** Tokens consumed so far. */
  readonly used: bigint;
}

/** The result of {@link Swarm.consume}. */
export interface SwarmGrant {
  /** Tokens granted (all requested, or what was left). */
  readonly granted: bigint;
  /** True if the request could not be met in full (ZXV_E_BUDGET). */
  readonly exhausted: boolean;
}

/** An emotion's behaviour profile (all exact integers). */
export interface EmotionProfile {
  readonly valence: number;
  /** Sampling temperature in thousandths. */
  readonly temperatureMilli: number;
  /** Exploration in per-mille. */
  readonly explorePermille: number;
  readonly verifyPasses: number;
  readonly priority: number;
}

/**
 * The swarm token economy (kernel swarm: budget + emotion). Each cycle,
 * `tokensPerCycle` tokens are split across the Fibonacci levels (the real
 * axis) and, by the mood's charge, an imaginary (emotional) share is split
 * by each model's own feeling. Feelings are emoji code points of the kernel
 * palette with intensity 0..5 (charge 0, 1, 2, 3, 5, 8). Gated calls need
 * {@link Cap.SWARM}. Unused tokens expire at the end of a cycle.
 */
export class Swarm extends Resource {
  /** The capability every gated call presents. */
  readonly cap: Capability;
  readonly #ctx: ZxvContext;
  #ptr: Ptr;

  /** @internal Use {@link Swarm.create}. */
  constructor(cap: Capability, ptr: Ptr) {
    super(cap.context);
    this.cap = cap;
    this.#ctx = cap.context;
    this.#ptr = ptr;
  }

  /** A new swarm in `cap`'s context. */
  static create(cap: Capability, options: SwarmOptions): Swarm {
    const c = cap.context;
    return new Swarm(cap, newHandle(c._core, c._state, 'zxv_swarm_create_cap', cap._ptr(), u32('levels', options.levels),
      u64('tokensPerCycle', options.tokensPerCycle)));
  }

  #p(): Ptr {
    this.live();
    return this.#ptr;
  }

  #call(fn: string, ...args: (number | bigint | ArrayBufferView | null)[]): void {
    this.#ctx._core.call(this.#ctx._state, fn, this.cap._ptr(), this.#p(), ...args);
  }

  /** Register a model at a level; a full level is CAPACITY (refused). */
  register(modelId: number, level: number): void {
    this.#call('zxv_swarm_register', u32('modelId', modelId), u32('level', level));
  }

  /** Include or exclude a model from allotments. */
  setActive(modelId: number, active: boolean): void {
    this.#call('zxv_swarm_set_active', u32('modelId', modelId), active ? 1 : 0);
  }

  /** Change the tokens per cycle. */
  setRate(tokensPerCycle: IntLike): void {
    this.#call('zxv_swarm_set_rate', u64('tokensPerCycle', tokensPerCycle));
  }

  /** The swarm's mood: an emoji code point of the palette (e.g. 0x1F914) and intensity 0..5. */
  setMood(codepoint: number, intensity: number): void {
    this.#call('zxv_swarm_set_mood', u32('codepoint', codepoint), u32('intensity', intensity));
  }

  /** One model's feeling. */
  setFeeling(modelId: number, codepoint: number, intensity: number): void {
    this.#call('zxv_swarm_set_feeling', u32('modelId', modelId), u32('codepoint', codepoint), u32('intensity', intensity));
  }

  /** Start a cycle and allot its tokens; returns the cycle number. */
  beginCycle(): bigint {
    const c = new BigUint64Array(1);
    this.#call('zxv_swarm_begin_cycle', c);
    return c[0] ?? 0n;
  }

  /**
   * Ask for tokens. A request beyond the allotment grants what was left and
   * reports `exhausted: true` (a REFUSED condition returned as data,
   * because the partial grant is still valid).
   */
  consume(modelId: number, requested: IntLike): SwarmGrant {
    const g = new BigUint64Array(1);
    const s = this.#ctx._core.callAllowing(this.#ctx._state, [-19], 'zxv_swarm_consume', this.cap._ptr(), this.#p(),
      u32('modelId', modelId), u64('requested', requested), g);
    return Object.freeze({ granted: g[0] ?? 0n, exhausted: s !== 0 });
  }

  /** A model's allotment in the current cycle. */
  allotment(modelId: number): Allotment {
    const a = new BigUint64Array(1);
    const i = new BigUint64Array(1);
    const u = new BigUint64Array(1);
    this.#ctx._core.call(this.#ctx._state, 'zxv_swarm_allotment', this.#p(), u32('modelId', modelId), a, i, u);
    return Object.freeze({ allotted: a[0] ?? 0n, imaginary: i[0] ?? 0n, used: u[0] ?? 0n });
  }

  /** Close the cycle; returns the tokens that expired unused. */
  endCycle(): bigint {
    const e = new BigUint64Array(1);
    this.#call('zxv_swarm_end_cycle', e);
    return e[0] ?? 0n;
  }

  protected destroy(): void {
    const p = this.#ptr;
    this.#ctx._state.release(() => this.#ctx._core.b.call('zxv_swarm_destroy', p));
  }
}

/** Pure swarm facts (no context needed). Reached as `zxv.swarm`. */
export class SwarmInfo {
  readonly #core: Core;

  /** @internal */
  constructor(core: Core) {
    this.#core = core;
  }

  /** Capacity of level d: F(d+2) (1, 2, 3, 5, 8, ...). */
  levelCapacity(level: number): number {
    const n = new Uint32Array(1);
    this.#core.call(this.#core, 'zxv_swarm_level_capacity', u32('level', level), n);
    return n[0] ?? 0;
  }

  /** The behaviour profile of a palette emotion (NotFound usage error otherwise). */
  emotionProfile(codepoint: number): EmotionProfile {
    const v = new Int32Array(1);
    const t = new Uint32Array(1);
    const x = new Uint32Array(1);
    const p = new Uint32Array(1);
    const pr = new Uint32Array(1);
    this.#core.call(this.#core, 'zxv_swarm_emotion_profile', u32('codepoint', codepoint), v, t, x, p, pr);
    return Object.freeze({
      valence: v[0] ?? 0, temperatureMilli: t[0] ?? 0, explorePermille: x[0] ?? 0, verifyPasses: p[0] ?? 0, priority: pr[0] ?? 0,
    });
  }
}
