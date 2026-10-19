// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.mobile

import org.zedec.zxv.mobile.internal.E_ARG
import org.zedec.zxv.mobile.internal.Status

/**
 * Four-valued (Belnap) logic: the kernel's `lpres` states, numbered as the
 * kernel numbers them (`ZXV_LP_*`).
 *
 * There is no conversion to `Boolean` without a policy: [BOTH] is a held
 * contradiction (not an error, not null) and [NEITHER] is "not yet known".
 * Use [toBoolean] with both answers spelled out, [isCertainlyTrue], or
 * [requireTrue]. `and`, `or` and `!` follow libzxv's tables (conformance key
 * `lpres`; the tests check this copy against `zxv_lp_*`).
 *
 * The kernel plans an orthogonal 2-bit `mode` beside the four states; it will
 * arrive as a separate accessor, never by renumbering these values.
 */
enum class Truth(
    /** The kernel's numeric value (`ZXV_LP_*`). */
    val code: Int,
) {
    /** S0: unknown, unattested, pending. */
    NEITHER(0),

    /** S+: attested present and valid. */
    TRUE(1),

    /** S-: attested absent or invalid. */
    FALSE(2),

    /** Contradictory evidence, held. */
    BOTH(3),
    ;

    /** Four-valued negation: swaps TRUE and FALSE, keeps NEITHER and BOTH. */
    operator fun not(): Truth = entries[NOT[code]]

    /** Four-valued conjunction. */
    infix fun and(other: Truth): Truth = entries[AND[code][other.code]]

    /** Four-valued disjunction. */
    infix fun or(other: Truth): Truth = entries[OR[code][other.code]]

    /** True only for [TRUE]. */
    val isCertainlyTrue: Boolean get() = this == TRUE

    /** True only for [FALSE]. */
    val isCertainlyFalse: Boolean get() = this == FALSE

    /** True for TRUE or FALSE. */
    val isCertain: Boolean get() = this == TRUE || this == FALSE

    /**
     * Collapses to a boolean with an explicit policy for the two
     * non-classical states.
     */
    fun toBoolean(onBoth: Boolean, onNeither: Boolean): Boolean = when (this) {
        TRUE -> true
        FALSE -> false
        BOTH -> onBoth
        NEITHER -> onNeither
    }

    /** Returns only for [TRUE]; otherwise throws what `error` builds. */
    inline fun requireTrue(error: (Truth) -> Throwable = { IllegalStateException("required TRUE, was $it") }) {
        if (this != TRUE) throw error(this)
    }

    companion object {
        // Copied from libzxv (zxv_lp_not/and/or); TruthTest checks them against the C library.
        private val NOT = intArrayOf(0, 2, 1, 3)
        private val AND = arrayOf(intArrayOf(0, 0, 2, 0), intArrayOf(0, 1, 2, 3), intArrayOf(2, 2, 2, 2), intArrayOf(0, 3, 2, 3))
        private val OR = arrayOf(intArrayOf(0, 1, 0, 3), intArrayOf(1, 1, 1, 1), intArrayOf(0, 1, 2, 3), intArrayOf(3, 1, 3, 3))

        /** The value for a kernel code; `ZXV_E_ARG` for anything but 0..3. */
        @JvmStatic
        fun fromCode(code: Int): Truth {
            if (code !in 0..3) throw Status.usage(E_ARG, "not a four-valued logic state: $code")
            return entries[code]
        }

        /** Both kinds of evidence give BOTH, only positive TRUE, only negative FALSE, none NEITHER. */
        @JvmStatic
        fun fromEvidence(positive: Long, negative: Long): Truth {
            if (positive < 0 || negative < 0) throw Status.usage(E_ARG, "evidence counts must be >= 0")
            return if (positive > 0) (if (negative > 0) BOTH else TRUE) else (if (negative > 0) FALSE else NEITHER)
        }
    }
}
