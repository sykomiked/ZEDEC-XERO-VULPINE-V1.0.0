// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

import { usage } from './errors.js';

/** The four state names, numbered as the kernel numbers them. */
export type TruthName = 'NEITHER' | 'TRUE' | 'FALSE' | 'BOTH';

/** How {@link Truth.toBoolean} resolves the two uncertain states. */
export interface TruthPolicy {
  /** Result for BOTH (held contradiction). */
  onBoth: boolean;
  /** Result for NEITHER (unknown / unattested / pending). */
  onNeither: boolean;
}

const TOKEN = Symbol('Truth');
const NAMES: readonly TruthName[] = ['NEITHER', 'TRUE', 'FALSE', 'BOTH'];

// Belnap's FOUR, as the kernel's lpres tables (vectors.json "lpres").
const NOT = [0, 2, 1, 3] as const;
const AND = [
  [0, 0, 2, 0],
  [0, 1, 2, 3],
  [2, 2, 2, 2],
  [0, 3, 2, 3],
] as const;
const OR = [
  [0, 1, 0, 3],
  [1, 1, 1, 1],
  [0, 1, 2, 3],
  [3, 1, 3, 3],
] as const;

/** Thrown by {@link Truth.require} when the value is not TRUE. */
export class TruthNotCertainError extends Error {
  /** The value that was not TRUE. */
  readonly truth: Truth;
  constructor(truth: Truth, what: string) {
    super(`${what} is ${truth.name}, not TRUE`);
    this.name = 'TruthNotCertainError';
    this.truth = truth;
  }
}

/**
 * Paraconsistent four-valued logic (kernel lpres, K3): exactly four
 * frozen singleton values, `Truth.NEITHER` (0), `Truth.TRUE` (1),
 * `Truth.FALSE` (2), `Truth.BOTH` (3). Compare with `===`.
 *
 * BOTH is a held contradiction, not an error and not null; NEITHER is
 * "unknown". There is NO implicit conversion to boolean: every value is
 * an object, so `if (truth)` is always taken. Use
 * {@link Truth.isCertainlyTrue}, {@link Truth.toBoolean} with an explicit
 * policy, or {@link Truth.require}. Arithmetic and `==` with numbers throw.
 *
 * The class is a value type with room for an orthogonal 2-bit `mode` the
 * kernel is adding; code that uses `value`/`name` will keep working.
 */
export class Truth {
  /** The kernel number: 0 NEITHER, 1 TRUE, 2 FALSE, 3 BOTH. */
  readonly value: 0 | 1 | 2 | 3;
  /** The name ("BOTH"). */
  readonly name: TruthName;

  private constructor(value: 0 | 1 | 2 | 3, token: symbol) {
    if (token !== TOKEN) throw usage('Truth has exactly four values; use Truth.TRUE etc.');
    this.value = value;
    this.name = NAMES[value] as TruthName;
    Object.freeze(this);
  }

  /** S0: unknown, unattested, pending. */
  static readonly NEITHER: Truth = new Truth(0, TOKEN);
  /** S+: attested present and valid. */
  static readonly TRUE: Truth = new Truth(1, TOKEN);
  /** S-: attested absent or invalid. */
  static readonly FALSE: Truth = new Truth(2, TOKEN);
  /** Contradictory evidence, held. */
  static readonly BOTH: Truth = new Truth(3, TOKEN);

  /** All four, in kernel order. */
  static readonly values: readonly Truth[] = Object.freeze([Truth.NEITHER, Truth.TRUE, Truth.FALSE, Truth.BOTH]);

  /** The value for a kernel number (0..3); anything else is ZXV_E_ARG. */
  static of(value: number): Truth {
    const t = Number.isInteger(value) ? Truth.values[value] : undefined;
    if (!t) throw usage(`${String(value)} is not a Truth value (0..3)`);
    return t;
  }

  /** The value for a name ("BOTH"). */
  static fromName(name: TruthName): Truth {
    return Truth.of(NAMES.indexOf(name));
  }

  /** From evidence counts: both kinds -> BOTH, only positive -> TRUE, only negative -> FALSE, none -> NEITHER. */
  static fromEvidence(positive: number, negative: number): Truth {
    return positive > 0 ? (negative > 0 ? Truth.BOTH : Truth.TRUE) : negative > 0 ? Truth.FALSE : Truth.NEITHER;
  }

  /** Lift a boolean: true -> TRUE, false -> FALSE. */
  static fromBoolean(b: boolean): Truth {
    return b ? Truth.TRUE : Truth.FALSE;
  }

  /** Negation: swaps TRUE and FALSE; NEITHER and BOTH are fixed points. */
  not(): Truth {
    return Truth.of(NOT[this.value]);
  }

  /** Conjunction (kernel lpres_conjoin). BOTH and TRUE = BOTH: a contradiction survives. */
  and(b: Truth): Truth {
    return Truth.of(AND[this.value][b.value]);
  }

  /** Disjunction (kernel lpres_disjoin). */
  or(b: Truth): Truth {
    return Truth.of(OR[this.value][b.value]);
  }

  /** True only for TRUE. */
  isCertainlyTrue(): boolean {
    return this === Truth.TRUE;
  }

  /** True only for FALSE. */
  isCertainlyFalse(): boolean {
    return this === Truth.FALSE;
  }

  /** True for TRUE and FALSE (no contradiction, nothing unknown). */
  isCertain(): boolean {
    return this === Truth.TRUE || this === Truth.FALSE;
  }

  /** Explicit collapse to a boolean: the caller decides what BOTH and NEITHER mean. */
  toBoolean(policy: TruthPolicy): boolean {
    if (this === Truth.TRUE) return true;
    if (this === Truth.FALSE) return false;
    return this === Truth.BOTH ? policy.onBoth : policy.onNeither;
  }

  /** Return if TRUE, otherwise throw {@link TruthNotCertainError}. */
  require(what = 'value'): void {
    if (this !== Truth.TRUE) throw new TruthNotCertainError(this, what);
  }

  /** The name. */
  toString(): TruthName {
    return this.name;
  }

  /** JSON form: the name. */
  toJSON(): TruthName {
    return this.name;
  }

  /** No implicit numeric value: `truth == 1` and `+truth` throw. */
  valueOf(): never {
    throw new TypeError('Truth has no implicit primitive value: use .value, .name or toBoolean(policy)');
  }
}

/** The standalone form of {@link Truth.toBoolean}. */
export function toBoolean(truth: Truth, policy: TruthPolicy): boolean {
  return truth.toBoolean(policy);
}
