// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

import type { Capability, ZxvContext } from './context.js';
import { usage } from './errors.js';
import type { Ptr } from './internal/backend.js';
import { i32, type IntLike, u32, u64 } from './internal/check.js';
import { newHandle, Resource } from './internal/core.js';

/** Ticks in one harmonic fundamental: 27,720 = lcm(1..11). */
export const PHASE_CYCLE = 27720n;

/** K6 coordinator decisions. */
export const Decision = Object.freeze({ ADMIT: 0, VETO: 1, DEFER: 2, RETRY: 3 } as const);
/** A Decision value. */
export type Decision = (typeof Decision)[keyof typeof Decision];

/** Step flags for {@link PhaseClock.admit}. */
export const StepFlag = Object.freeze({ REQUIRES_HEALTH: 0x1, HARDWARE: 0x2, S0_PENDING: 0x4 } as const);

/** Phase health. */
export const Health = Object.freeze({ UNKNOWN: 0, HEALTHY: 1, DEGRADED: 2, UNHEALTHY: 3 } as const);
/** A Health value. */
export type Health = (typeof Health)[keyof typeof Health];

/** Phase ids 1..13 (pc_phase_id_t). */
export const PhaseId = Object.freeze({
  K1_OSEQ: 1, K2_RMAG: 2, K3_LPRES: 3, K4_IPHASE: 4, K5_CHOICE: 5, K6_COORD: 6, O1_CRIT168: 7,
  O2_FS: 8, O3_AGP: 9, O4_MESH: 10, O5_CHIGLET: 11, O6_VINO: 12, O7_KIKAN: 13,
} as const);

/** Deferred-event states. */
export const EventState = Object.freeze({ PENDING: 0, CLEARED: 1, CANCELLED: 2 } as const);
/** An EventState value. */
export type EventState = (typeof EventState)[keyof typeof EventState];

/** The result of {@link PhaseClock.admit}. */
export interface Admission {
  readonly decision: Decision;
  /** Non-zero only on ADMIT. */
  readonly token: bigint;
}

/** The result of {@link PhaseClock.advance}. */
export interface Advance {
  /** The tick after advancing. */
  readonly now: bigint;
  /** How many deferred events cleared during this advance. */
  readonly cleared: number;
}

/** Options of {@link DeferredEvent.awaitCleared}. */
export interface AwaitOptions {
  signal?: AbortSignal;
}

/**
 * The phase coordinator (kernel phase_coord, K6) and its logical clock.
 * ZXV time is a tick count, not wall time; one fundamental is
 * {@link PHASE_CYCLE} ticks. Drive it manually with {@link PhaseClock.advance}
 * (deterministic tests, simulations) or from a timer with
 * {@link PhaseClock.startRealtime}. Gated calls need {@link Cap.PHASE}.
 */
export class PhaseClock extends Resource {
  /** The capability every gated call presents. */
  readonly cap: Capability;
  /** @internal */
  readonly _ctx: ZxvContext;
  #ptr: Ptr;
  /** @internal */
  readonly _waiters = new Map<bigint, Set<() => void>>();
  #timer: ReturnType<typeof setInterval> | undefined;

  /** @internal Use {@link PhaseClock.open}. */
  constructor(cap: Capability, ptr: Ptr) {
    super(cap.context);
    this.cap = cap;
    this._ctx = cap.context;
    this.#ptr = ptr;
  }

  /** A new clock (tick 0) in `cap`'s context. */
  static open(cap: Capability): PhaseClock {
    const c = cap.context;
    return new PhaseClock(cap, newHandle(c._core, c._state, 'zxv_phase_create_cap', cap._ptr()));
  }

  /** @internal */
  _p(): Ptr {
    this.live();
    return this.#ptr;
  }

  /** The current tick. */
  now(): bigint {
    const t = new BigUint64Array(1);
    this._ctx._core.call(this._ctx._state, 'zxv_phase_now', this._p(), t);
    return t[0] ?? 0n;
  }

  /** Advance by `delta` ticks; events due at or before the new tick clear and their waiters resolve. */
  advance(delta: IntLike): Advance {
    const now = new BigUint64Array(1);
    const cleared = new Uint32Array(1);
    this._ctx._core.call(this._ctx._state, 'zxv_phase_advance', this.cap._ptr(), this._p(), u64('delta', delta), now, cleared);
    if ((cleared[0] ?? 0) > 0) this.#wakeCleared();
    return Object.freeze({ now: now[0] ?? 0n, cleared: cleared[0] ?? 0 });
  }

  #wakeCleared(): void {
    for (const [id, set] of [...this._waiters]) {
      if (this.eventState(id) !== EventState.PENDING) {
        this._waiters.delete(id);
        for (const w of set) w();
      }
    }
  }

  /** Ask the coordinator to admit step `stepId` of phase `phaseId` (1..13). */
  admit(phaseId: number, stepId: number, flags = 0): Admission {
    const d = new Int32Array(1);
    const t = new BigUint64Array(1);
    this._ctx._core.call(this._ctx._state, 'zxv_phase_admit', this.cap._ptr(), this._p(), u32('phaseId', phaseId), u32('stepId', stepId),
      u32('flags', flags), d, t);
    return Object.freeze({ decision: (d[0] ?? 0) as Decision, token: t[0] ?? 0n });
  }

  /** Report a phase's health. */
  setHealth(phaseId: number, health: Health): void {
    this._ctx._core.call(this._ctx._state, 'zxv_phase_set_health', this.cap._ptr(), this._p(), u32('phaseId', phaseId), i32('health', health));
  }

  /** Park an S0 event until absolute tick `dueTick` (> now). */
  defer(dueTick: IntLike): DeferredEvent {
    const ev = new BigUint64Array(1);
    const due = u64('dueTick', dueTick);
    this._ctx._core.call(this._ctx._state, 'zxv_phase_defer', this.cap._ptr(), this._p(), due, ev);
    return new DeferredEvent(this, ev[0] ?? 0n, due);
  }

  /**
   * Resolves once the clock has reached `tick` (at once if it already
   * has): a deferred event plus {@link DeferredEvent.awaitCleared}.
   */
  async untilTick(tick: IntLike, options: AwaitOptions = {}): Promise<void> {
    const t = u64('tick', tick);
    if (t <= this.now()) return;
    await this.defer(t).awaitCleared(options);
  }

  /** @internal */
  eventState(id: bigint): EventState {
    const st = new Int32Array(1);
    this._ctx._core.call(this._ctx._state, 'zxv_phase_event', this._p(), id, st, null);
    return (st[0] ?? 0) as EventState;
  }

  /** Earliest due tick of a pending event, or undefined if none. */
  nextDue(): bigint | undefined {
    const t = new BigUint64Array(1);
    const s = this._ctx._core.callAllowing(this._ctx._state, [-4], 'zxv_phase_next_due', this._p(), t);
    return s === 0 ? t[0] : undefined;
  }

  /**
   * Drive the clock from a host timer: every `intervalMs` (integer, default
   * 10) it advances by the ticks that elapsed at `ticksPerSecond` (integer),
   * resolves cleared waiters, and yields back to the event loop. Returns a
   * function that stops it. Tick counts are computed in integers from a
   * monotonic clock, so no drift accumulates.
   */
  startRealtime(ticksPerSecond: IntLike, intervalMs = 10): () => void {
    const rate = u64('ticksPerSecond', ticksPerSecond);
    const every = u32('intervalMs', intervalMs);
    if (rate === 0n || every === 0) throw usage('ticksPerSecond and intervalMs must be > 0');
    if (this.#timer !== undefined) throw usage('the realtime driver is already running');
    const start = BigInt(Math.floor(performance.now()));
    let applied = 0n;
    this.#timer = setInterval(() => {
      if (this.closed) return this.#stop();
      const elapsedMs = BigInt(Math.floor(performance.now())) - start;
      const due = (elapsedMs * rate) / 1000n;
      if (due > applied) {
        this.advance(due - applied);
        applied = due;
      }
    }, every);
    return () => this.#stop();
  }

  #stop(): void {
    if (this.#timer !== undefined) clearInterval(this.#timer);
    this.#timer = undefined;
  }

  protected destroy(): void {
    this.#stop();
    for (const set of this._waiters.values()) for (const w of set) w();
    this._waiters.clear();
    const p = this.#ptr;
    this._ctx._state.release(() => this._ctx._core.b.call('zxv_phase_destroy', p));
  }
}

/** An S0 event parked until a tick (see {@link PhaseClock.defer}). */
export class DeferredEvent {
  /** The clock it belongs to. */
  readonly clock: PhaseClock;
  /** Event id. */
  readonly id: bigint;
  /** Absolute tick at which it clears. */
  readonly dueTick: bigint;

  /** @internal */
  constructor(clock: PhaseClock, id: bigint, dueTick: bigint) {
    this.clock = clock;
    this.id = id;
    this.dueTick = dueTick;
  }

  /** PENDING, CLEARED or CANCELLED. */
  state(): EventState {
    return this.clock.eventState(this.id);
  }

  /** Cancel it (waiters reject). */
  cancel(): void {
    const c = this.clock;
    c._ctx._core.call(c._ctx._state, 'zxv_phase_cancel', c.cap._ptr(), c._p(), this.id);
    const set = c._waiters.get(this.id);
    c._waiters.delete(this.id);
    for (const w of set ?? []) w();
  }

  /** Resolves when the clock passes the due tick; rejects if cancelled, aborted, or the clock closes. */
  async awaitCleared(options: AwaitOptions = {}): Promise<void> {
    const c = this.clock;
    for (;;) {
      options.signal?.throwIfAborted();
      if (c.closed) throw usage('the PhaseClock was closed');
      const st = this.state();
      if (st === EventState.CLEARED) return;
      if (st === EventState.CANCELLED) throw usage(`deferred event ${this.id} was cancelled`);
      await new Promise<void>((ok) => {
        let set = c._waiters.get(this.id);
        if (!set) c._waiters.set(this.id, (set = new Set()));
        const wake = () => {
          set.delete(wake);
          options.signal?.removeEventListener('abort', wake);
          ok();
        };
        set.add(wake);
        options.signal?.addEventListener('abort', wake);
      });
    }
  }
}
