// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.mobile

import org.zedec.zxv.mobile.internal.Jni15
import org.zedec.zxv.mobile.internal.Status

/** The four buckets the assurance fee splits into. */
enum class FeeBucket { RESERVE_FLOOR, VBILL_DIVIDEND, INFRA_BOUNTY, REGEN_CAPITAL }

/** The units a fee policy prices. */
enum class FeeUnit { MONEY, COMPUTE, BANDWIDTH, STORAGE }

/**
 * The default fee policy: bucket percentages, whether fees below the minimum
 * are allowed, credit rates per unit, the multiplier and the commons cap, all
 * exact ratios of uint64 values (no floats).
 */
data class FeePolicy(
    val percent: List<Int>,
    val allowBelow: Boolean,
    val creditRates: List<Ratio>,
    val multiplier: Ratio,
    val commonsCap: Ratio,
) {
    /** An exact ratio of two uint64 values (bits in `Long`). */
    data class Ratio(val num: Long, val den: Long) {
        override fun toString(): String = "${num.toULong()}/${den.toULong()}"
    }

    /** The credit rate for `unit`. */
    fun creditRate(unit: FeeUnit): Ratio = creditRates[unit.ordinal]
}

/**
 * The 0.08889% assurance fee (8889 / 10,000,000) on a gross amount, rounded
 * exactly, with a carried remainder for long runs, and its four-way split.
 * Amounts are uint64 values in a `Long`. Pure.
 */
object AssuranceFee {
    /** The fee rate numerator. */
    const val NUM: Long = 8889

    /** The fee rate denominator. */
    const val DEN: Long = 10_000_000

    /** A fee and the remainder to carry into the next charge. */
    data class Charge(val fee: Long, val remainder: Long)

    /** The four parts of a fee. */
    data class FeeSplit(val reserveFloor: Long, val vbillDividend: Long, val infraBounty: Long, val regenCapital: Long) {
        /** The part for `bucket`. */
        fun part(bucket: FeeBucket): Long = when (bucket) {
            FeeBucket.RESERVE_FLOOR -> reserveFloor
            FeeBucket.VBILL_DIVIDEND -> vbillDividend
            FeeBucket.INFRA_BOUNTY -> infraBounty
            FeeBucket.REGEN_CAPITAL -> regenCapital
        }
    }

    /** The fee on `gross`, rounded down. */
    @JvmStatic
    fun fee(gross: Long): Long = LongArray(1).also { Status.check(Jni15.feeAssure(gross, it), null) }[0]

    /** The fee on `gross` with `remainderIn` carried from earlier charges. */
    @JvmStatic
    fun charge(remainderIn: Long, gross: Long): Charge {
        val o = LongArray(2)
        Status.check(Jni15.feeAssureCarry(remainderIn, gross, o), null)
        return Charge(o[0], o[1])
    }

    /** The four-way split of `fee` (the parts sum to it). */
    @JvmStatic
    fun split(fee: Long): FeeSplit {
        val o = LongArray(4)
        Status.check(Jni15.feeSplit(fee, o), null)
        return FeeSplit(o[0], o[1], o[2], o[3])
    }

    /** libzxv's default policy. */
    @JvmStatic
    fun defaultPolicy(): FeePolicy {
        val v = LongArray(17)
        Status.check(Jni15.feePolicyDefault(v), null)
        return FeePolicy(
            List(4) { v[it].toInt() },
            v[4] != 0L,
            List(4) { FeePolicy.Ratio(v[5 + it], v[9 + it]) },
            FeePolicy.Ratio(v[13], v[14]),
            FeePolicy.Ratio(v[15], v[16]),
        )
    }
}
