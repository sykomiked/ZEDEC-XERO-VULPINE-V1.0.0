// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// Treaties (ABI 1.5, kernel/src/treaty): one batch of settlement work in
// two exact spellings, the fixed-width COBOL copybook record and compact
// "Sutra" text, plus COBOL PIC S9(18) arithmetic and a deterministic,
// side-effect-free run of the batch. Everything here is pure: no context
// and no capability.

import { Status, TreatyError } from './errors.js';
import { type Arg, cstr, type Ptr } from './internal/backend.js';
import { bytes, i64, str, u32 } from './internal/check.js';
import { type Core, newHandle, Resource } from './internal/core.js';

/** Something that owns a loaded library (a {@link Zxv}). */
interface LibraryOwner {
  /** @internal */
  readonly _core: Core;
}

/** Bytes in a treaty copybook record (`ZXV_TREATY_REC_LEN`, ZXVTRTY.cpy). */
export const TREATY_RECORD_BYTES = 2542;
/** Bytes in a treaty result record (`ZXV_TREATY_RESULT_LEN`, ZXVRSLT.cpy). */
export const TREATY_RESULT_BYTES = 2422;
/** Longest canonical Sutra text in bytes (`ZXV_TREATY_SUTRA_MAX`). */
export const TREATY_SUTRA_MAX = 3843;
/** Most parties in one treaty. */
export const TREATY_MAX_PARTIES = 16;
/** Most obligations ("OWE" lines) in one treaty. */
export const TREATY_MAX_OWES = 64;
/** Longest treaty name: 1..30 of A-Z, 0-9 and '-', starting with a letter. */
export const TREATY_NAME_MAX = 30;
/** Longest party id: 1..8 characters of the same alphabet as names. */
export const TREATY_ID_MAX = 8;
/** Largest magnitude a PIC S9(18) field holds: 999,999,999,999,999,999. */
export const TREATY_PIC18_MAX = 999_999_999_999_999_999n;
/** Largest dividend-weight numerator or denominator (PIC 9(9)). */
export const TREATY_WEIGHT_MAX = 999_999_999;

/** What a treaty does when run (`ZXV_TREATY_OP_*`). */
export const TreatyOp = Object.freeze({
  /** Net the obligations only. */
  NET: 1,
  /** Net, post the legs with the 0.08889% assurance fee, and pay each claim back. */
  SETL: 2,
} as const);
/** A TreatyOp value. */
export type TreatyOp = (typeof TreatyOp)[keyof typeof TreatyOp];

/** Why a treaty was refused (`ZXV_TREATY_FAULT_*`): the {@link TreatyError.fault} of a refusal. */
export const TreatyFault = Object.freeze({
  /** No fault. */
  NONE: 0,
  /** The record does not start with "ZXT1". */
  MAGIC: 2,
  /** Sutra text outside the grammar (including any non-ASCII byte). */
  SYNTAX: 3,
  /** The treaty name is not 1..30 of A-Z, 0-9 and '-'. */
  NAME: 4,
  /** The asset is not three capital letters. */
  ASSET: 5,
  /** The operation is not NET or SETL. */
  OP: 6,
  /** Party or obligation count out of range (1..16 parties, 0..64 obligations). */
  COUNT: 7,
  /** A malformed number, or one outside PIC S9(18). */
  NUMBER: 8,
  /** A bad, duplicate or unknown party id. */
  PARTY: 9,
  /** Weights not in lowest terms, above their maximum, or not adding up to exactly 1 (rule R1). */
  WEIGHT: 10,
  /** A party claims more than it deposits (rule R2). */
  CLAIM: 11,
  /** An obligation to self, or one that is not above zero. */
  OWE: 12,
  /** An unused record slot or field is not blank (COBOL INITIALIZE form). */
  PAD: 13,
} as const);

/** One party of a treaty, in treaty order. */
export interface TreatyParty {
  /** Party id, 1..8 characters of A-Z, 0-9 and '-'. */
  readonly id: string;
  /** What the party deposits, in minor units of the treaty's asset (>= 0). */
  readonly deposit: bigint;
  /** What the party may claim back after settlement (0 <= claim <= deposit). */
  readonly claim: bigint;
  /** Numerator of the party's dividend weight; all weights add up to exactly 1. */
  readonly weightNum: number;
  /** Denominator of the party's dividend weight (lowest terms). */
  readonly weightDen: number;
}

/** One obligation: party `from` owes party `to` `amount` (0-based party indices). */
export interface TreatyObligation {
  /** Index of the paying party. */
  readonly from: number;
  /** Index of the receiving party. */
  readonly to: number;
  /** Amount owed, in minor units (> 0). */
  readonly amount: bigint;
}

/** The totals of a {@link TreatyResult}. */
export interface TreatyRunSummary {
  /** Number of parties. */
  readonly parties: number;
  /** Number of netted legs (at most parties - 1). */
  readonly legs: number;
  /** Total assurance fee charged (0 for NET). */
  readonly fees: bigint;
}

/** What one party ended with after a run. */
export interface TreatyPartyOutcome {
  /** What it is owed minus what it owes (gross). */
  readonly net: bigint;
  /** NET: its deposit. SETL: what it holds after the legs, the fee and its claim. */
  readonly balance: bigint;
  /** Its reported share of the fee's V-Bill dividend bucket (0 for NET); reported, never paid. */
  readonly dividend: bigint;
}

/** One netted payment: party `from` pays party `to` `amount`. */
export interface TreatyLeg {
  /** Index of the paying party. */
  readonly from: number;
  /** Index of the receiving party. */
  readonly to: number;
  /** Amount paid, in minor units. */
  readonly amount: bigint;
}

/** The quotient and remainder of a COBOL `DIVIDE ... GIVING ... REMAINDER`. */
export interface PicDivision {
  /** Truncated toward zero. */
  readonly quotient: bigint;
  /** Has the dividend's sign. */
  readonly remainder: bigint;
}

/**
 * One treaty (`zxv_treaty`): a batch of settlement work, who takes part,
 * what each party deposits and may claim back, how the batch's dividend
 * is shared, and who owes whom.
 *
 * It has two canonical spellings that hold the same treaty: the
 * {@link TREATY_RECORD_BYTES}-byte COBOL copybook record (plain ASCII) and
 * Sutra text such as
 *
 * ```text
 * TREATY CORRIDOR-7 USD SETL
 * PARTY ACME 5000 4000 1/2
 * PARTY BETA 3000 3000 1/2
 * OWE ACME BETA 1200
 * END
 * ```
 *
 * Parsing then emitting gives the input back byte for byte. Every parse
 * and emit checks the rules: weights in lowest terms adding up to exactly
 * 1, claim <= deposit, amounts within PIC S9(18), 1..16 parties with
 * distinct ids, 0..64 obligations between two different known parties. A
 * refusal throws {@link TreatyError} carrying the {@link TreatyFault} and,
 * for a parse, the byte offset of the first fault.
 *
 * Holds no context and needs no capability. Not safe to share between
 * Workers. Release it with {@link Treaty.close} or `using`.
 *
 * HONEST LIMITS: a treaty is a batch description, not a contract in law;
 * signatures, consent and authority live elsewhere. One asset per treaty.
 */
export class Treaty extends Resource {
  readonly #core: Core;
  readonly #ptr: Ptr;

  /** @internal Use {@link Treaties.create}, {@link Treaties.parseSutra} or {@link Treaties.parseRecord}. */
  constructor(core: Core, ptr: Ptr) {
    super(null);
    this.#core = core;
    this.#ptr = ptr;
  }

  /** @internal The live handle (throws if closed). */
  _ptr(): Ptr {
    this.live();
    return this.#ptr;
  }

  #call(fn: string, ...args: Arg[]): void {
    this.#core.call(this.#core, fn, this._ptr(), ...args);
  }

  /** Throw a TreatyError with fault and offset for a refused parse or validation. */
  #parse(fn: string, ...args: Arg[]): void {
    const off = new Uint32Array(1);
    const fault = new Int32Array(1);
    const p = this._ptr();
    const s = this.#core.callAllowing(this.#core, [Status.E_TREATY], fn, p, ...args, off, fault);
    if (s === Status.E_TREATY) {
      throw new TreatyError(s, 'ZXV_E_TREATY', this.#core.b.lastError(), fault[0] ?? 0, off[0] ?? 0);
    }
  }

  /**
   * Replace this treaty with one parsed from Sutra text. On any failure the
   * treaty is unchanged.
   *
   * @param text - canonical Sutra text (at most {@link TREATY_SUTRA_MAX} bytes)
   * @throws {@link TreatyError} with `fault` and `offset` when the text breaks a rule
   */
  loadSutra(text: string): void {
    this.#parse('zxv_treaty_parse_sutra', cstr(str('text', text)));
  }

  /**
   * Replace this treaty with one parsed from a copybook record. Unused slots
   * must be exactly what COBOL's INITIALIZE leaves. On any failure the
   * treaty is unchanged.
   *
   * @param record - exactly {@link TREATY_RECORD_BYTES} bytes
   * @throws {@link TreatyError} with `fault` and `offset` (into the record) when it breaks a rule
   */
  loadRecord(record: Uint8Array): void {
    const r = bytes('record', record, TREATY_RECORD_BYTES);
    this.#parse('zxv_treaty_parse_record', r, r.length);
  }

  /** Empty the treaty again (it then fails validation until something is loaded). */
  clear(): void {
    this.#call('zxv_treaty_clear');
  }

  /**
   * The canonical Sutra text.
   *
   * @throws {@link TreatyError} if the treaty does not validate (an empty one, say)
   */
  toSutra(): string {
    const p = this._ptr();
    return this.#core.text(this.#core, 'zxv_treaty_emit_sutra', (b, c, l) => [p, b, c, l]);
  }

  /**
   * The canonical copybook record ({@link TREATY_RECORD_BYTES} bytes of ASCII, no NUL).
   *
   * @throws {@link TreatyError} if the treaty does not validate
   */
  toRecord(): Uint8Array {
    const p = this._ptr();
    return this.#core.bytes(this.#core, 'zxv_treaty_emit_record', (b, c, l) => [p, b, c, l]);
  }

  /**
   * The first rule this treaty breaks, or {@link TreatyFault.NONE} if it
   * validates. A parsed treaty always validates; an empty one reports
   * {@link TreatyFault.NAME}.
   */
  fault(): number {
    const f = new Int32Array(1);
    this.#core.callAllowing(this.#core, [Status.E_TREATY], 'zxv_treaty_validate', this._ptr(), f);
    return f[0] ?? 0;
  }

  /**
   * Check every rule.
   *
   * @throws {@link TreatyError} (with `fault`) when a rule is broken
   */
  validate(): void {
    const f = this.fault();
    if (f !== TreatyFault.NONE) throw new TreatyError(Status.E_TREATY, 'ZXV_E_TREATY', this.#core.b.lastError(), f);
  }

  /**
   * True if both hold the same treaty, field by field and slot by slot (the
   * record and Sutra forms of one treaty compare equal; two empty ones too).
   */
  equals(other: Treaty): boolean {
    const e = new Int32Array(1);
    this.#core.call(this.#core, 'zxv_treaty_equal', this._ptr(), other._ptr(), e);
    return (e[0] ?? 0) === 1;
  }

  /** The treaty name ("" when empty). */
  get name(): string {
    const p = this._ptr();
    return this.#core.text(this.#core, 'zxv_treaty_name', (b, c, l) => [p, b, c, l]);
  }

  /** The asset: three capital letters ("" when empty). */
  get asset(): string {
    const p = this._ptr();
    return this.#core.text(this.#core, 'zxv_treaty_asset', (b, c, l) => [p, b, c, l]);
  }

  #header(): [op: number, parties: number, owes: number] {
    const op = new Int32Array(1);
    const n = new Uint32Array(2);
    this.#call('zxv_treaty_header', op, n.subarray(0, 1), n.subarray(1, 2));
    return [op[0] ?? 0, n[0] ?? 0, n[1] ?? 0];
  }

  /** The {@link TreatyOp} (0 when empty). */
  get op(): number {
    return this.#header()[0];
  }

  /** Number of parties. */
  get partyCount(): number {
    return this.#header()[1];
  }

  /** Number of obligations. */
  get obligationCount(): number {
    return this.#header()[2];
  }

  /**
   * Party `index` (0-based, treaty order).
   *
   * @throws a ZxvError with status ZXV_E_NOT_FOUND past the last party
   */
  party(index: number): TreatyParty {
    const i = u32('index', index);
    const p = this._ptr();
    const id = this.#core.text(this.#core, 'zxv_treaty_party_id', (b, c, l) => [p, i, b, c, l]);
    const v = new BigInt64Array(2);
    const w = new Uint32Array(2);
    this.#call('zxv_treaty_party', i, v.subarray(0, 1), v.subarray(1, 2), w.subarray(0, 1), w.subarray(1, 2));
    return Object.freeze({ id, deposit: v[0] ?? 0n, claim: v[1] ?? 0n, weightNum: w[0] ?? 0, weightDen: w[1] ?? 0 });
  }

  /** Every party, in treaty order. */
  parties(): TreatyParty[] {
    return Array.from({ length: this.partyCount }, (_, k) => this.party(k));
  }

  /**
   * Obligation `index` (0-based).
   *
   * @throws a ZxvError with status ZXV_E_NOT_FOUND past the last obligation
   */
  obligation(index: number): TreatyObligation {
    const n = new Uint32Array(2);
    const a = new BigInt64Array(1);
    this.#call('zxv_treaty_owe', u32('index', index), n.subarray(0, 1), n.subarray(1, 2), a);
    return Object.freeze({ from: n[0] ?? 0, to: n[1] ?? 0, amount: a[0] ?? 0n });
  }

  /** Every obligation, in treaty order. */
  obligations(): TreatyObligation[] {
    return Array.from({ length: this.obligationCount }, (_, k) => this.obligation(k));
  }

  /**
   * Run the treaty in a fresh, private scratch engine (about 500 KB of heap,
   * freed before this returns): fund each deposit, net the obligations
   * (at most parties - 1 legs), and for SETL post the legs with the
   * 0.08889% assurance fee, pay each claim back and report each party's
   * dividend share. Nothing persists and nothing is paid: the numbers are a
   * deterministic report, identical to what CALL 'ZXV_ENGINE' returns.
   *
   * @returns the result, an independent handle (close it, or `using`)
   * @throws {@link TreatyError} (does not validate), {@link InsufficientFundsError}
   *   (a payer cannot cover its legs plus the fee, or a claim exceeds what is left),
   *   or a ZXV_E_RANGE usage error (a net position leaves PIC S9(18))
   */
  run(): TreatyResult {
    return new TreatyResult(this.#core, newHandle(this.#core, this.#core, 'zxv_treaty_run', this._ptr()));
  }

  protected destroy(): void {
    this.#core.b.call('zxv_treaty_destroy', this.#ptr);
  }
}

/**
 * What running a treaty returned (`zxv_treaty_result`). Immutable and
 * independent of the treaty it came from (either may be closed first).
 * Release it with {@link TreatyResult.close} or `using`.
 */
export class TreatyResult extends Resource {
  readonly #core: Core;
  readonly #ptr: Ptr;

  /** @internal Use {@link Treaty.run}. */
  constructor(core: Core, ptr: Ptr) {
    super(null);
    this.#core = core;
    this.#ptr = ptr;
  }

  #p(): Ptr {
    this.live();
    return this.#ptr;
  }

  /** Party and leg counts and the total fee charged (0 for NET). */
  summary(): TreatyRunSummary {
    const n = new Uint32Array(2);
    const f = new BigInt64Array(1);
    this.#core.call(this.#core, 'zxv_treaty_result_summary', this.#p(), n.subarray(0, 1), n.subarray(1, 2), f);
    return Object.freeze({ parties: n[0] ?? 0, legs: n[1] ?? 0, fees: f[0] ?? 0n });
  }

  /**
   * What party `index` (treaty order) ended with.
   *
   * @throws a ZxvError with status ZXV_E_NOT_FOUND past the last party
   */
  party(index: number): TreatyPartyOutcome {
    const v = new BigInt64Array(3);
    this.#core.call(this.#core, 'zxv_treaty_result_party', this.#p(), u32('index', index), v.subarray(0, 1), v.subarray(1, 2),
      v.subarray(2, 3));
    return Object.freeze({ net: v[0] ?? 0n, balance: v[1] ?? 0n, dividend: v[2] ?? 0n });
  }

  /** Every party's outcome, in treaty order. */
  parties(): TreatyPartyOutcome[] {
    return Array.from({ length: this.summary().parties }, (_, k) => this.party(k));
  }

  /**
   * Netted leg `index`.
   *
   * @throws a ZxvError with status ZXV_E_NOT_FOUND past the last leg
   */
  leg(index: number): TreatyLeg {
    const n = new Uint32Array(2);
    const a = new BigInt64Array(1);
    this.#core.call(this.#core, 'zxv_treaty_result_leg', this.#p(), u32('index', index), n.subarray(0, 1), n.subarray(1, 2), a);
    return Object.freeze({ from: n[0] ?? 0, to: n[1] ?? 0, amount: a[0] ?? 0n });
  }

  /** Every netted leg. */
  legs(): TreatyLeg[] {
    return Array.from({ length: this.summary().legs }, (_, k) => this.leg(k));
  }

  /** The ZXVRSLT.cpy result record ({@link TREATY_RESULT_BYTES} bytes; party numbers in it are 1-based). */
  toRecord(): Uint8Array {
    const p = this.#p();
    return this.#core.bytes(this.#core, 'zxv_treaty_result_record', (b, c, l) => [p, b, c, l]);
  }

  protected destroy(): void {
    this.#core.b.call('zxv_treaty_result_destroy', this.#ptr);
  }
}

/**
 * The treaty group of a loaded library, reached as `zxv.treaty`: treaty
 * factories and COBOL PIC S9(18) arithmetic. Pure: no context, no
 * capability.
 */
export class Treaties {
  readonly #core: Core;

  /** @internal */
  constructor(owner: LibraryOwner) {
    this.#core = owner._core;
  }

  /** A new, empty treaty (it fails validation until something is loaded into it). */
  create(): Treaty {
    return new Treaty(this.#core, newHandle(this.#core, this.#core, 'zxv_treaty_create'));
  }

  /**
   * Parse Sutra text into a new treaty.
   *
   * @throws {@link TreatyError} with `fault` and `offset` when the text breaks a rule
   */
  parseSutra(text: string): Treaty {
    const t = this.create();
    try {
      t.loadSutra(text);
    } catch (e) {
      t.close();
      throw e;
    }
    return t;
  }

  /**
   * Parse a copybook record into a new treaty.
   *
   * @param record - exactly {@link TREATY_RECORD_BYTES} bytes
   * @throws {@link TreatyError} with `fault` and `offset` when the record breaks a rule
   */
  parseRecord(record: Uint8Array): Treaty {
    const t = this.create();
    try {
      t.loadRecord(record);
    } catch (e) {
      t.close();
      throw e;
    }
    return t;
  }

  #pic(fn: string, a: bigint, b: bigint): bigint {
    const o = new BigInt64Array(1);
    this.#core.call(this.#core, fn, i64('a', a), i64('b', b), o);
    return o[0] ?? 0n;
  }

  /**
   * `a + b` as COBOL ADD with ON SIZE ERROR: exact on [-PIC18_MAX, PIC18_MAX].
   *
   * @throws a ZXV_E_RANGE error (a size error) when an operand or the result is outside that range
   */
  picAdd(a: bigint, b: bigint): bigint {
    return this.#pic('zxv_treaty_pic_add', a, b);
  }

  /** `a - b` with ON SIZE ERROR (ZXV_E_RANGE outside PIC S9(18)). */
  picSub(a: bigint, b: bigint): bigint {
    return this.#pic('zxv_treaty_pic_sub', a, b);
  }

  /** `a * b` with ON SIZE ERROR (ZXV_E_RANGE outside PIC S9(18)). */
  picMul(a: bigint, b: bigint): bigint {
    return this.#pic('zxv_treaty_pic_mul', a, b);
  }

  /**
   * COBOL `DIVIDE a BY b GIVING q REMAINDER r`: the quotient truncates
   * toward zero and the remainder has the dividend's sign
   * (-7 / 2 = -3 rem -1; 7 / -2 = -3 rem 1).
   *
   * @throws a ZXV_E_RANGE error on division by zero or outside PIC S9(18)
   */
  picDiv(a: bigint, b: bigint): PicDivision {
    const q = new BigInt64Array(1);
    const r = new BigInt64Array(1);
    this.#core.call(this.#core, 'zxv_treaty_pic_div', i64('a', a), i64('b', b), q, r);
    return Object.freeze({ quotient: q[0] ?? 0n, remainder: r[0] ?? 0n });
  }
}
