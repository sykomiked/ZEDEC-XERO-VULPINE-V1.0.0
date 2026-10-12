// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The 0.08889% assurance fee and its 50/25/15/10 split (ABI 1.5,
// kernel/src/pay/pay_assure). Exact integer arithmetic; pure: no context,
// no capability.

import { u64 } from './internal/check.js';
import type { Core } from './internal/core.js';
import { Rational } from './rational.js';

/** Something that owns a loaded library (a {@link Zxv}). */
interface LibraryOwner {
  /** @internal */
  readonly _core: Core;
}

/** Numerator of the fee rate: 0.08889% = 8889 / 10^7. */
export const FEE_NUM = 8889n;
/** Denominator of the fee rate; a carry is always below it. */
export const FEE_DEN = 10_000_000n;

/** The four buckets a fee is split into (`ZXV_FEE_*`), as indices of {@link FeeSplit.parts}. */
export const FeeBucket = Object.freeze({
  /** 50%, plus every remainder unit: backs Vino. */
  RESERVE_FLOOR: 0,
  /** 25%: the V-Bill dividend pool. */
  VBILL_DIVIDEND: 1,
  /** 15%: infrastructure and node bounties. */
  INFRA_BOUNTY: 2,
  /** 10%: regenerative capital. */
  REGEN_CAPITAL: 3,
} as const);

/** What a gross amount is measured in (`ZXV_FEE_UNIT_*`), as indices of {@link FeePolicy.creditRates}. */
export const FeeUnit = Object.freeze({
  /** Minor units of a ledger asset. */
  MONEY: 0,
  /** Token-cycles. */
  COMPUTE: 1,
  /** Bytes. */
  BANDWIDTH: 2,
  /** Byte-hours. */
  STORAGE: 3,
} as const);

/** A fee with the sub-unit remainder to carry to the account's next charge. */
export interface FeeWithCarry {
  /** The fee charged now. */
  readonly fee: bigint;
  /** The remainder (0 <= rem < 10^7) to store for the account's next charge. */
  readonly remainder: bigint;
}

/** A fee split into its four buckets. */
export interface FeeSplit {
  /** Indexed by {@link FeeBucket}; they always add up to the fee exactly. */
  readonly parts: readonly [reserveFloor: bigint, vbillDividend: bigint, infraBounty: bigint, regenCapital: bigint];
}

/** The default fee policy ({@link Fees.policyDefault}). */
export interface FeePolicy {
  /** The bucket shares in percent, indexed by {@link FeeBucket}: 50, 25, 15, 10. */
  readonly percent: readonly number[];
  /** False: a contribution below the fee is raised to the fee. */
  readonly allowBelow: boolean;
  /**
   * The posted VFV store-credit rate per unit kind ({@link FeeUnit}) for a
   * voluntary contribution above the fee: money 1/1, the others 0/1 (no
   * credit until a rate is posted).
   */
  readonly creditRates: readonly Rational[];
  /** The operator's credit multiplier, 1/1. */
  readonly creditMultiplier: Rational;
  /** The no-monopoly cap on one recipient's share of the non-money commons pool, 8/21. */
  readonly commonsCap: Rational;
}

/**
 * The assurance fee, reached as `zxv.fees`:
 * `fee(g) = floor(g * 8889 / 10,000,000)` (0.08889% of the gross), exact for
 * every gross up to 2^64 - 1 (fee(1124) = 0, fee(1125) = 1). It is charged
 * once per transaction on its gross: never on a fee, never compounded.
 * Pure: no context, no capability.
 *
 * HONEST LIMITS: these are the arithmetic rules only. How a fee, a reserve
 * or a dividend pool is treated for tax, securities or consumer law is a
 * question for counsel; nothing here is a licence or a certification.
 */
export class Fees {
  readonly #core: Core;

  /** @internal */
  constructor(owner: LibraryOwner) {
    this.#core = owner._core;
  }

  /** The fee on `gross` (in the unit's own minor measure), with no carry. */
  assure(gross: bigint): bigint {
    const f = new BigUint64Array(1);
    this.#core.call(this.#core, 'zxv_fee_assure', u64('gross', gross), f);
    return f[0] ?? 0n;
  }

  /**
   * The fee on `gross` for an account whose stored remainder is
   * `remainder`: `fee = floor((gross * 8889 + remainder) / 10^7)`. Store the
   * returned remainder for the account's next charge; starting from 0, many
   * small charges then pay exactly what one charge of their sum pays.
   *
   * @throws a ZXV_E_RANGE error when `remainder` >= 10^7 (a corrupt carry)
   */
  assureWithCarry(remainder: bigint, gross: bigint): FeeWithCarry {
    const o = new BigUint64Array(2);
    this.#core.call(this.#core, 'zxv_fee_assure_carry', u64('remainder', remainder), u64('gross', gross), o.subarray(0, 1),
      o.subarray(1, 2));
    return Object.freeze({ fee: o[0] ?? 0n, remainder: o[1] ?? 0n });
  }

  /**
   * Split `fee` into its four buckets: buckets 1-3 get floor(fee * pct /
   * 100) and the reserve floor gets the rest, so the parts always add up
   * to the fee exactly.
   */
  split(fee: bigint): FeeSplit {
    const o = new BigUint64Array(4);
    this.#core.call(this.#core, 'zxv_fee_split', u64('fee', fee), o);
    return Object.freeze({ parts: Object.freeze([o[0] ?? 0n, o[1] ?? 0n, o[2] ?? 0n, o[3] ?? 0n] as const) });
  }

  /** The default policy: bucket shares, below-fee rule, credit rates, multiplier and commons cap. */
  policyDefault(): FeePolicy {
    const pct = new Uint32Array(4);
    const below = new Int32Array(1);
    const rn = new BigUint64Array(4);
    const rd = new BigUint64Array(4);
    const m = new BigUint64Array(4);
    this.#core.call(this.#core, 'zxv_fee_policy_default', pct, below, rn, rd, m.subarray(0, 1), m.subarray(1, 2), m.subarray(2, 3),
      m.subarray(3, 4));
    return Object.freeze({
      percent: Object.freeze(Array.from(pct)),
      allowBelow: (below[0] ?? 0) !== 0,
      creditRates: Object.freeze(Array.from({ length: 4 }, (_, k) => Rational.of(rn[k] ?? 0n, rd[k] ?? 1n))),
      creditMultiplier: Rational.of(m[0] ?? 0n, m[1] ?? 1n),
      commonsCap: Rational.of(m[2] ?? 0n, m[3] ?? 1n),
    });
  }
}
