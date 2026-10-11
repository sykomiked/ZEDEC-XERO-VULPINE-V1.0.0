// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.mobile

import org.zedec.zxv.mobile.internal.E_ARG
import org.zedec.zxv.mobile.internal.Jni
import org.zedec.zxv.mobile.internal.Status
import org.zedec.zxv.mobile.internal.requireU32

/**
 * Base of a [Hexagram], numbered by its line pattern (`ZXV_HEX_BASE_*`).
 * NOT numbered like [Truth]: TRUE is base 3 but Truth 1. Convert only with
 * [Hexagram.toTruth] and [Hexagram.fromTruth].
 */
enum class HexBase(val code: Int) {
    FALSE(0), NEITHER(1), BOTH(2), TRUE(3);

    companion object {
        @JvmStatic
        fun fromCode(code: Int): HexBase = entries.getOrNull(code) ?: throw Status.usage(E_ARG, "base $code is not 0..3")
    }
}

/** Polarity of the evidence (`ZXV_HEX_POL_*`). FORBIDDEN is absorbing. */
enum class HexPolarity(val code: Int) {
    ENTROPIC(0), NEUTRAL(1), FORBIDDEN(2), NEGENTROPIC(3);

    companion object {
        @JvmStatic
        fun fromCode(code: Int): HexPolarity =
            entries.getOrNull(code) ?: throw Status.usage(E_ARG, "polarity $code is not 0..3")
    }
}

/** Lifecycle phase (`ZXV_HEX_PHASE_*`). The cycle is POTENTIAL, EMERGING, MANIFEST, DISSOLVING. */
enum class HexPhase(val code: Int) {
    POTENTIAL(0), EMERGING(1), DISSOLVING(2), MANIFEST(3);

    /** The next phase on the cycle (`zxv_hex_phase_next`). */
    fun next(): HexPhase {
        val out = IntArray(1)
        Status.check(Jni.hexPhaseNext(code, out), null)
        return fromCode(out[0])
    }

    companion object {
        @JvmStatic
        fun fromCode(code: Int): HexPhase = entries.getOrNull(code) ?: throw Status.usage(E_ARG, "phase $code is not 0..3")
    }
}

/** What happens next to tagged work (`ZXV_HEX_ACT_*`). */
enum class HexAction(val code: Int) {
    PROCEED(0), WAIT(1), REFUSE(2), ESCALATE(3), ROLLBACK(4);

    /** libzxv's name, e.g. "rollback". */
    val actionName: String get() = Jni.hexActionName(code)

    companion object {
        @JvmStatic
        fun fromCode(code: Int): HexAction = entries.getOrNull(code) ?: throw Status.usage(E_ARG, "action $code is not 0..4")
    }
}

/**
 * The 64-state hexagram tag of ABI 1.3: an immutable value packing a
 * [HexBase], a [HexPolarity] and a [HexPhase] as
 * `tag = base shl 4 or polarity shl 2 or phase` (0..63). Every rule is
 * computed by libzxv.
 *
 * ```
 * var h = Hexagram.of(HexBase.TRUE, HexPolarity.NEGENTROPIC, HexPhase.POTENTIAL)
 * h.action               // PROCEED
 * h = h.advance()        // EMERGING
 * h.step(HexPhase.DISSOLVING)   // ZxvRefusedException (ZXV_E_STATE): a jump
 * ```
 */
class Hexagram private constructor(
    /** The byte value, 0..63. */
    val tag: Int,
) {
    companion object {
        /** A tag from its three fields. */
        @JvmStatic
        fun of(base: HexBase, polarity: HexPolarity, phase: HexPhase): Hexagram =
            Hexagram(out { Jni.hexPack(base.code, polarity.code, phase.code, it) })

        /** The tag with this value; `ZXV_E_ARG` (USAGE) outside 0..63. */
        @JvmStatic
        fun fromTag(tag: Int): Hexagram {
            if (tag !in 0..255) throw Status.usage(E_ARG, "tag $tag is not 0..63")
            Status.check(Jni.hexUnpack(tag, null), null)
            return Hexagram(tag)
        }

        /** A tag whose base is `truth`, mapped by `zxv_hex_from_lp`; an explicit conversion. */
        @JvmStatic
        fun fromTruth(truth: Truth, polarity: HexPolarity, phase: HexPhase): Hexagram =
            of(HexBase.fromCode(out { Jni.hexFromLp(truth.code, it) }), polarity, phase)

        /**
         * Polarity from evidence counts before and after (uint32 each): stale
         * gives ENTROPIC; else the margin |pos - neg| growing NEGENTROPIC,
         * shrinking ENTROPIC, unchanged NEUTRAL. Never FORBIDDEN.
         */
        @JvmStatic
        fun polarityFromEvidence(pos0: Long, neg0: Long, pos1: Long, neg1: Long, stale: Boolean): HexPolarity {
            requireU32(pos0, "pos0"); requireU32(neg0, "neg0"); requireU32(pos1, "pos1"); requireU32(neg1, "neg1")
            return HexPolarity.fromCode(out { Jni.hexPolarity(pos0, neg0, pos1, neg1, if (stale) 1 else 0, it) })
        }

        private inline fun out(f: (IntArray) -> Int): Int {
            val o = IntArray(1)
            Status.check(f(o), null)
            return o[0]
        }
    }

    /** Bits 5-4. */
    val base: HexBase get() = HexBase.fromCode(tag shr 4)

    /** Bits 3-2. */
    val polarity: HexPolarity get() = HexPolarity.fromCode((tag shr 2) and 3)

    /** Bits 1-0. */
    val phase: HexPhase get() = HexPhase.fromCode(tag and 3)

    /** True if the polarity is FORBIDDEN. */
    val isForbidden: Boolean get() = polarity == HexPolarity.FORBIDDEN

    /** What happens next: the kernel's 64-entry table. */
    val action: HexAction get() = HexAction.fromCode(out { Jni.hexAction(tag, it) })

    /** "base/polarity/phase", e.g. "true/negentropic/manifest". */
    val name: String get() = Jni.hexName(tag)

    /** The base as a [Truth] (`zxv_hex_to_lp`); an explicit conversion. */
    fun toTruth(): Truth = Truth.fromCode(out { Jni.hexToLp(tag shr 4, it) })

    /** The same work in `phase`: the same or the next phase only; a jump is REFUSED (`ZXV_E_STATE`). */
    fun step(phase: HexPhase): Hexagram = Hexagram(out { Jni.hexStep(tag, phase.code, it) })

    /** One step along the cycle; cannot fail. */
    fun advance(): Hexagram = step(phase.next())

    /** New base and polarity, same phase; REFUSED (`ZXV_E_STATE`) to un-forbid a forbidden tag. */
    fun withState(base: HexBase, polarity: HexPolarity): Hexagram =
        Hexagram(out { Jni.hexSet(tag, base.code, polarity.code, it) })

    /** The same tag with polarity FORBIDDEN (absorbing). */
    fun forbid(): Hexagram = Hexagram(out { Jni.hexForbid(tag, it) })

    /** Four-valued AND on the base, worst polarity, least advanced phase. */
    infix fun and(other: Hexagram): Hexagram = Hexagram(out { Jni.hexAnd(tag, other.tag, it) })

    /** Four-valued OR on the base, worst polarity, least advanced phase. */
    infix fun or(other: Hexagram): Hexagram = Hexagram(out { Jni.hexOr(tag, other.tag, it) })

    /** Negation of the base; polarity and phase kept. */
    operator fun not(): Hexagram = Hexagram(out { Jni.hexNot(tag, it) })

    override fun equals(other: Any?): Boolean = other is Hexagram && other.tag == tag
    override fun hashCode(): Int = tag
    override fun toString(): String = "Hexagram($tag $name)"
}
