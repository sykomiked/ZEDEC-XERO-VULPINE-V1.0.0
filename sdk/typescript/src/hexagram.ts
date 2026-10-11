// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The 64-state hexagram tag (ABI 1.3, kernel lpres_hex) and its quarantine
// log. Hexagram is pure TypeScript, like Truth and Rational: it works
// before (and without) loading a backend, and the test suite checks every
// one of its 64 tags and 4096 pairs against vectors.json "hex" (written by
// libzxv) and against both backends' zxv_hex_* calls. Its and/or reuse
// Truth's tables, so there is one copy of the four-valued logic here.

import type { Capability, ZxvContext } from './context.js';
import { Status, usage, ZxvRefusedError } from './errors.js';
import type { Ptr } from './internal/backend.js';
import { bytes, u32, u8 } from './internal/check.js';
import { newHandle, Resource } from './internal/core.js';
import { Truth } from './truth.js';

/** The base, bits 5-4 (`ZXV_HEX_BASE_*`). Numbered by line pattern, NOT like {@link Truth}. */
export const HexBase = Object.freeze({ FALSE: 0, NEITHER: 1, BOTH: 2, TRUE: 3 } as const);
/** A HexBase value. */
export type HexBase = (typeof HexBase)[keyof typeof HexBase];

/** How the evidence is moving, bits 3-2 (`ZXV_HEX_POL_*`). FORBIDDEN is absorbing. */
export const HexPolarity = Object.freeze({ ENTROPIC: 0, NEUTRAL: 1, FORBIDDEN: 2, NEGENTROPIC: 3 } as const);
/** A HexPolarity value. */
export type HexPolarity = (typeof HexPolarity)[keyof typeof HexPolarity];

/** The lifecycle, bits 1-0 (`ZXV_HEX_PHASE_*`): POTENTIAL -> EMERGING -> MANIFEST -> DISSOLVING -> POTENTIAL. */
export const HexPhase = Object.freeze({ POTENTIAL: 0, EMERGING: 1, DISSOLVING: 2, MANIFEST: 3 } as const);
/** A HexPhase value. */
export type HexPhase = (typeof HexPhase)[keyof typeof HexPhase];

/** What happens next (`ZXV_HEX_ACT_*`). ROLLBACK means roll back and quarantine. */
export const HexAction = Object.freeze({ PROCEED: 0, WAIT: 1, REFUSE: 2, ESCALATE: 3, ROLLBACK: 4 } as const);
/** A HexAction value. */
export type HexAction = (typeof HexAction)[keyof typeof HexAction];

const BASE_NAMES = ['false', 'neither', 'both', 'true'] as const;
const POL_NAMES = ['entropic', 'neutral', 'forbidden', 'negentropic'] as const;
const PHASE_NAMES = ['potential', 'emerging', 'dissolving', 'manifest'] as const;
/** Lower-case action names, indexed by HexAction. */
export const HEX_ACTION_NAMES = Object.freeze(['proceed', 'wait', 'refuse', 'escalate', 'rollback'] as const);

// The bridge to Truth (zxv_hex_from_lp / zxv_hex_to_lp), written out.
const LP_TO_BASE: readonly HexBase[] = [HexBase.NEITHER, HexBase.TRUE, HexBase.FALSE, HexBase.BOTH];
const BASE_TO_LP: readonly number[] = [2, 0, 3, 1]; // FALSE->2, NEITHER->0, BOTH->3, TRUE->1
const NEXT_PHASE: readonly HexPhase[] = [HexPhase.EMERGING, HexPhase.MANIFEST, HexPhase.POTENTIAL, HexPhase.DISSOLVING];
const POL_BADNESS = [2, 1, 3, 0] as const; // forbidden > entropic > neutral > negentropic
const PHASE_ORDER = [0, 1, 3, 2] as const; // potential < emerging < manifest < dissolving

const TOKEN = Symbol('Hexagram');

function field(name: string, v: unknown): 0 | 1 | 2 | 3 {
  const x = u8(name, v);
  if (x > 3) throw usage(`${name} = ${x} is not 0..3`);
  return x as 0 | 1 | 2 | 3;
}

function action(base: number, pol: number): HexAction {
  if (pol === HexPolarity.FORBIDDEN) return HexAction.ROLLBACK;
  switch (base) {
    case HexBase.TRUE: return pol === HexPolarity.ENTROPIC ? HexAction.WAIT : HexAction.PROCEED;
    case HexBase.FALSE: return HexAction.REFUSE;
    case HexBase.BOTH: return pol === HexPolarity.NEGENTROPIC ? HexAction.WAIT : HexAction.ESCALATE;
    default: return HexAction.WAIT;
  }
}

function refusedState(detail: string): ZxvRefusedError {
  return new ZxvRefusedError(Status.E_STATE, 'ZXV_E_STATE', detail);
}

/**
 * The 64-state hexagram tag: one unit of work tagged with a four-valued
 * {@link HexBase}, the {@link HexPolarity} of its evidence and the
 * {@link HexPhase} of its lifecycle, `tag = base << 4 | polarity << 2 | phase`.
 *
 * 64 frozen singletons: compare with `===`. No implicit primitive value.
 * {@link Hexagram.action} says what happens next; {@link Hexagram.step}
 * moves one phase at a time; a forbidden tag stays forbidden; and/or/not
 * combine tags (four-valued logic on the base, worst polarity, least
 * advanced phase). Convert to and from {@link Truth} only with
 * {@link Hexagram.toTruth} / {@link Hexagram.fromTruth}.
 */
export class Hexagram {
  /** The byte value, 0..63. */
  readonly tag: number;
  /** Bits 5-4. */
  readonly base: HexBase;
  /** Bits 3-2. */
  readonly polarity: HexPolarity;
  /** Bits 1-0. */
  readonly phase: HexPhase;
  /** What happens next. */
  readonly action: HexAction;
  /** "base/polarity/phase", e.g. "true/negentropic/manifest". */
  readonly name: string;

  private constructor(tag: number, token: symbol) {
    if (token !== TOKEN) throw usage('use Hexagram.of, Hexagram.fromTag or Hexagram.fromTruth');
    this.tag = tag;
    this.base = (tag >> 4) as HexBase;
    this.polarity = ((tag >> 2) & 3) as HexPolarity;
    this.phase = (tag & 3) as HexPhase;
    this.action = action(this.base, this.polarity);
    this.name = `${BASE_NAMES[this.base]}/${POL_NAMES[this.polarity]}/${PHASE_NAMES[this.phase]}`;
    Object.freeze(this);
  }

  /** All 64 tags, in tag order. */
  static get all(): readonly Hexagram[] {
    return (ALL ??= Object.freeze(Array.from({ length: 64 }, (_, t) => new Hexagram(t, TOKEN))));
  }

  /** The tag from its three fields (each 0..3). */
  static of(base: HexBase, polarity: HexPolarity, phase: HexPhase): Hexagram {
    return Hexagram.all[(field('base', base) << 4) | (field('polarity', polarity) << 2) | field('phase', phase)]!;
  }

  /** The tag with this byte value; anything but 0..63 is ZXV_E_ARG. */
  static fromTag(tag: number): Hexagram {
    const t = u8('tag', tag);
    if (t > 63) throw usage(`tag ${t} is not 0..63`);
    return Hexagram.all[t]!;
  }

  /** A tag whose base is `truth` (zxv_hex_from_lp). */
  static fromTruth(truth: Truth, polarity: HexPolarity, phase: HexPhase): Hexagram {
    if (!(truth instanceof Truth)) throw usage('truth must be a Truth value');
    return Hexagram.of(LP_TO_BASE[truth.value]!, polarity, phase);
  }

  /**
   * Polarity from evidence counts (uint32) before and after: stale ->
   * ENTROPIC; else the margin |pos - neg| growing -> NEGENTROPIC,
   * shrinking -> ENTROPIC, unchanged -> NEUTRAL.
   */
  static polarityFromEvidence(pos0: number, neg0: number, pos1: number, neg1: number, stale: boolean): HexPolarity {
    if (typeof stale !== 'boolean') throw usage('stale must be a boolean');
    const m0 = Math.abs(u32('pos0', pos0) - u32('neg0', neg0));
    const m1 = Math.abs(u32('pos1', pos1) - u32('neg1', neg1));
    if (stale) return HexPolarity.ENTROPIC;
    return m1 > m0 ? HexPolarity.NEGENTROPIC : m1 < m0 ? HexPolarity.ENTROPIC : HexPolarity.NEUTRAL;
  }

  /** The phase after `phase` on the Gray cycle. */
  static nextPhase(phase: HexPhase): HexPhase {
    return NEXT_PHASE[field('phase', phase)]!;
  }

  /** True if the polarity is FORBIDDEN. */
  get isForbidden(): boolean {
    return this.polarity === HexPolarity.FORBIDDEN;
  }

  /** The base as a {@link Truth} (zxv_hex_to_lp): an explicit conversion. */
  toTruth(): Truth {
    return Truth.of(BASE_TO_LP[this.base]!);
  }

  /** The same work in `phase`: the same phase or the next one only; a jump throws ZxvRefusedError (ZXV_E_STATE). */
  step(phase: HexPhase): Hexagram {
    const p = field('phase', phase);
    if (p !== this.phase && p !== NEXT_PHASE[this.phase]) {
      throw refusedState(`${PHASE_NAMES[this.phase]} -> ${PHASE_NAMES[p]} skips a phase (next is ${PHASE_NAMES[NEXT_PHASE[this.phase]!]})`);
    }
    return Hexagram.of(this.base, this.polarity, p);
  }

  /** One step along the cycle. */
  advance(): Hexagram {
    return this.step(NEXT_PHASE[this.phase]!);
  }

  /** New base and polarity, same phase; a forbidden tag asked for another polarity throws ZxvRefusedError (ZXV_E_STATE). */
  withState(base: HexBase, polarity: HexPolarity): Hexagram {
    const b = field('base', base);
    const p = field('polarity', polarity);
    if (this.isForbidden && p !== HexPolarity.FORBIDDEN) throw refusedState('a forbidden tag stays forbidden (absorbing)');
    return Hexagram.of(b, p, this.phase);
  }

  /** The same tag with polarity FORBIDDEN. */
  forbid(): Hexagram {
    return Hexagram.of(this.base, HexPolarity.FORBIDDEN, this.phase);
  }

  #combine(other: Hexagram, op: 'and' | 'or'): Hexagram {
    if (!(other instanceof Hexagram)) throw usage('operand must be a Hexagram');
    const t = this.toTruth()[op](other.toTruth());
    const pol = POL_BADNESS[this.polarity] >= POL_BADNESS[other.polarity] ? this.polarity : other.polarity;
    const ph = PHASE_ORDER[this.phase] <= PHASE_ORDER[other.phase] ? this.phase : other.phase;
    return Hexagram.of(LP_TO_BASE[t.value]!, pol, ph);
  }

  /** Conjunction: four-valued AND on the base, the worst polarity, the least advanced phase. */
  and(other: Hexagram): Hexagram {
    return this.#combine(other, 'and');
  }

  /** Disjunction: four-valued OR on the base, the worst polarity, the least advanced phase. */
  or(other: Hexagram): Hexagram {
    return this.#combine(other, 'or');
  }

  /** Negation of the base; polarity and phase are kept. */
  not(): Hexagram {
    return Hexagram.of(LP_TO_BASE[this.toTruth().not().value]!, this.polarity, this.phase);
  }

  /** The name. */
  toString(): string {
    return this.name;
  }

  /** JSON form: the tag number. */
  toJSON(): number {
    return this.tag;
  }

  /** No implicit primitive value: use `.tag` or `.name`. */
  valueOf(): never {
    throw new TypeError('Hexagram has no implicit primitive value: use .tag, .name or .action');
  }
}

let ALL: readonly Hexagram[] | undefined;

/** One record of a {@link QuarantineLog}. */
export interface QuarantineRecord {
  readonly seq: bigint;
  readonly tag: Hexagram;
  readonly reason: number;
  readonly submitter: number;
  readonly subject: Uint8Array;
  readonly prev: Uint8Array;
  readonly hash: Uint8Array;
}

/** Largest capacity of a {@link QuarantineLog} (`ZXV_HEXLOG_MAX_RECORDS`). */
export const QUARANTINE_MAX_RECORDS = 65536;

/**
 * An append-only, SHA-256 hash-chained log of forbidden work (zxv_hexlog),
 * read for audit and never replayed. Creating and appending need
 * {@link Cap.ATTEST}. A full log refuses the append (ZXV_E_CAPACITY,
 * {@link ZxvRefusedError}); the caller must then halt the context,
 * because forbidden work it cannot record must not continue. Signing
 * {@link QuarantineLog.head} is the caller's job.
 */
export class QuarantineLog extends Resource {
  /** The capability appends present. */
  readonly cap: Capability;
  readonly #ctx: ZxvContext;
  #ptr: Ptr;

  /** @internal Use {@link QuarantineLog.create}. */
  constructor(cap: Capability, ptr: Ptr) {
    super(cap.context);
    this.cap = cap;
    this.#ctx = cap.context;
    this.#ptr = ptr;
  }

  /** A log of at most `capacity` (1..65536) records in `cap`'s context. */
  static create(cap: Capability, capacity: number): QuarantineLog {
    const c = cap.context;
    return new QuarantineLog(cap, newHandle(c._core, c._state, 'zxv_hexlog_create', cap._ptr(), u32('capacity', capacity)));
  }

  #p(): Ptr {
    this.live();
    return this.#ptr;
  }

  /** Quarantine forbidden work; returns its sequence number. */
  append(tag: Hexagram, reason: number, submitter: number, subject: Uint8Array): bigint {
    if (!(tag instanceof Hexagram)) throw usage('tag must be a Hexagram');
    const seq = new BigUint64Array(1);
    this.#ctx._core.call(this.#ctx._state, 'zxv_hexlog_append', this.cap._ptr(), this.#p(), tag.tag, u8('reason', reason),
      u32('submitter', submitter), bytes('subject', subject, 32), seq);
    return seq[0] ?? 0n;
  }

  /** Number of records. */
  get count(): number {
    const n = new Uint32Array(1);
    this.#ctx._core.call(this.#ctx._state, 'zxv_hexlog_count', this.#p(), n);
    return n[0] ?? 0;
  }

  /** The chain head: the last record's hash (32 zero bytes when empty). */
  head(): Uint8Array {
    const h = new Uint8Array(32);
    this.#ctx._core.call(this.#ctx._state, 'zxv_hexlog_head', this.#p(), h);
    return h;
  }

  /** Record `index`, oldest first. */
  record(index: number): QuarantineRecord {
    const seq = new BigUint64Array(1);
    const tag = new Uint8Array(1);
    const reason = new Uint8Array(1);
    const who = new Uint32Array(1);
    const subject = new Uint8Array(32);
    const prev = new Uint8Array(32);
    const hash = new Uint8Array(32);
    this.#ctx._core.call(this.#ctx._state, 'zxv_hexlog_at', this.#p(), u32('index', index), seq, tag, reason, who, subject, prev, hash);
    return Object.freeze({ seq: seq[0] ?? 0n, tag: Hexagram.fromTag(tag[0] ?? 0), reason: reason[0] ?? 0, submitter: who[0] ?? 0, subject, prev, hash });
  }

  /** Recompute every link; a broken chain throws ZxvFatalError (the context halts). */
  verify(): void {
    this.#ctx._core.call(this.#ctx._state, 'zxv_hexlog_verify', this.#p());
  }

  protected destroy(): void {
    const p = this.#ptr;
    this.#ctx._state.release(() => this.#ctx._core.b.call('zxv_hexlog_destroy', p));
  }
}
