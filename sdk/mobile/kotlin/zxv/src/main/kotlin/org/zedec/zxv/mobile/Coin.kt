// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.mobile

import java.io.Closeable
import org.zedec.zxv.mobile.internal.Backstop
import org.zedec.zxv.mobile.internal.E_ARG
import org.zedec.zxv.mobile.internal.E_BUFFER
import org.zedec.zxv.mobile.internal.E_RANGE
import org.zedec.zxv.mobile.internal.Jni15
import org.zedec.zxv.mobile.internal.NativeResource
import org.zedec.zxv.mobile.internal.Status
import org.zedec.zxv.mobile.internal.requireIndex
import org.zedec.zxv.mobile.internal.requireU32

/**
 * Fibonacci coins: 45 rungs, faces 1, 2, 3, 5, 8, ... 1,836,311,903. Every
 * amount splits into the fewest coins (Zeckendorf). Amounts and faces are
 * uint64 values carried in a `Long` (bits kept). Pure.
 */
object Coins {
    /** The number of rungs. */
    const val RUNGS = 45

    /** The face of the top rung. */
    const val TOP_FACE: Long = 1_836_311_903L

    /** The longest array [split] returns. */
    const val SPLIT_ARRAY_MAX = Int.MAX_VALUE - 8

    /** The face of rung `rung` (0..44). */
    @JvmStatic
    fun face(rung: Int): Long {
        val r = requireIndex(rung, "rung")
        return LongArray(1).also { Status.check(Jni15.coinFace(r, it), null) }[0]
    }

    /** The rung of a face; [NotADenominationException] if it is not a face. */
    @JvmStatic
    fun rung(face: Long): Int = LongArray(1).also { Status.check(Jni15.coinRung(face, it), null) }[0].toInt()

    /** How many coins [split] would give (counted without building the array). */
    @JvmStatic
    fun coinCount(amount: Long): Long {
        val n = LongArray(1)
        val st = Jni15.coinSplit(amount, null, n)
        if (st != E_BUFFER) Status.check(st, null)
        return n[0]
    }

    /** The rungs of the fewest coins making `amount`, highest first. */
    @JvmStatic
    fun split(amount: Long): IntArray {
        val count = coinCount(amount)
        if (count > SPLIT_ARRAY_MAX) {
            throw Status.usage(E_RANGE, "split of ${amount.toULong()} has $count coins, too many for an array; use coinCount")
        }
        if (count == 0L) return IntArray(0)
        val rungs = IntArray(count.toInt())
        val n = LongArray(1)
        Status.check(Jni15.coinSplit(amount, rungs, n), null)
        return if (n[0] == count) rungs else rungs.copyOf(n[0].toInt())
    }
}

/** Token octane levels: how far a holding is compressed into super tiers. */
object Octane {
    const val PLAIN = 0
    const val REGULAR = 4
    const val MIDGRADE = 9
    const val PREMIUM = 14
    const val RACING = 23
    const val MAX = 63
}

/** Token denominations: 64 tiers of Fibonacci faces, tiers from [SUPER_TIER] up are super tokens. Pure. */
object Tokens {
    /** The number of tiers. */
    const val TIERS = 64

    /** The first super tier. */
    const val SUPER_TIER = 10

    /** The face of `tier` (uint64 bits). */
    @JvmStatic
    fun face(tier: Int): Long = faceInfo(tier)[0]

    /** True if `tier` is a super tier. */
    @JvmStatic
    fun isSuper(tier: Int): Boolean = faceInfo(tier)[1] != 0L

    private fun faceInfo(tier: Int): LongArray {
        val t = requireIndex(tier, "tier")
        return LongArray(2).also { Status.check(Jni15.tokenFace(t, it), null) }
    }

    /** The tier of a face; [NotADenominationException] if it is not one. */
    @JvmStatic
    fun tier(face: Long): Int = LongArray(1).also { Status.check(Jni15.tokenTier(face, it), null) }[0].toInt()
}

/** Fibonacci (Zeckendorf) universal codes: self-delimiting bit strings for uint64 values. Pure. */
object FibonacciCode {
    /** The longest code. */
    const val MAX_BITS = 93

    /** The longest buffer. */
    const val MAX_BYTES = 536_870_911

    /** Encoded bytes and how many bits are used. */
    class Encoded(bytes: ByteArray, val bits: Long) {
        private val b = bytes.copyOf()

        /** A copy of the bytes. */
        val bytes: ByteArray get() = b.copyOf()
    }

    /** A decoded value and the bit after it. */
    data class Decoded(val value: Long, val nextBit: Long)

    /** The code length of `value` in bits (`value` >= 1). */
    @JvmStatic
    fun bits(value: Long): Int = LongArray(1).also { Status.check(Jni15.tokenCodeBits(value, it), null) }[0].toInt()

    /** The codes of `values`, back to back. */
    @JvmStatic
    fun encode(vararg values: Long): Encoded {
        var total = 0L
        for (v in values) total += bits(v)
        if ((total + 7) / 8 > MAX_BYTES) throw Status.usage(-12, "codes too long")
        val buf = ByteArray(((total + 7) / 8).toInt())
        var bit = 0L
        for (v in values) bit = put(buf, bit, v)
        return Encoded(buf, bit)
    }

    /** Writes the code of `value` into `buf` at `bit` (in place); returns the bit after it. */
    @JvmStatic
    fun put(buf: ByteArray, bit: Long, value: Long): Long {
        val pos = longArrayOf(requireU32(bit, "bit"))
        Status.check(Jni15.tokenCodePut(buf, pos, value), null)
        return pos[0]
    }

    /** Reads one code from `buf` at `bit`. */
    @JvmStatic
    fun get(buf: ByteArray, bit: Long): Decoded {
        val pos = longArrayOf(requireU32(bit, "bit"))
        val v = LongArray(1)
        Status.check(Jni15.tokenCodeGet(buf, pos, v), null)
        return Decoded(v[0], pos[0])
    }

    /** Reads `count` codes from the start of `buf`. */
    @JvmStatic
    fun decode(buf: ByteArray, count: Int): LongArray {
        requireIndex(count, "count")
        val out = LongArray(count)
        var bit = 0L
        for (i in 0 until count) {
            val d = get(buf, bit)
            out[i] = d.value
            bit = d.nextBit
        }
        return out
    }
}

/**
 * A token holding: a count (uint64 bits) for each of the 64 tiers. An
 * immutable value; the operations return a new holding. Pure.
 */
class TokenHolding private constructor(private val c: LongArray) {
    companion object {
        /** No tokens. */
        @JvmStatic
        fun empty(): TokenHolding = TokenHolding(LongArray(Tokens.TIERS))

        /** A holding from 64 counts. */
        @JvmStatic
        fun of(counts: LongArray): TokenHolding {
            if (counts.size != Tokens.TIERS) throw Status.usage(E_ARG, "counts must have 64 entries")
            return TokenHolding(counts.copyOf())
        }

        /** The canonical holding of `value` at `octane`. */
        @JvmStatic
        fun canonical(value: Long, octane: Int): TokenHolding {
            val o = requireIndex(octane, "octane")
            val out = LongArray(Tokens.TIERS)
            Status.check(Jni15.tokenCanonical(value, o, out), null)
            return TokenHolding(out)
        }
    }

    /** The count at `tier`. */
    fun count(tier: Int): Long = c[tier]

    /** A copy of the 64 counts. */
    val counts: LongArray get() = c.copyOf()

    /** This holding with `count` at `tier`. */
    fun with(tier: Int, count: Long): TokenHolding {
        if (tier !in 0 until Tokens.TIERS) throw Status.usage(E_RANGE, "tier $tier")
        return TokenHolding(c.copyOf().also { it[tier] = count })
    }

    /** The total value. */
    fun value(): Long = valueAndPieces()[0]

    /** The number of pieces (tokens). */
    fun pieces(): Long = valueAndPieces()[1]

    private fun valueAndPieces(): LongArray = LongArray(2).also { Status.check(Jni15.tokenValue(c, it), null) }

    /** True if this is the canonical holding of its value at `octane`. */
    fun isCanonical(octane: Int): Boolean {
        val o = requireIndex(octane, "octane")
        return IntArray(1).also { Status.check(Jni15.tokenIsCanonical(c, o, it), null) }[0] != 0
    }

    /** Compressed to canonical form at `octane`. */
    fun compress(octane: Int): TokenHolding {
        val o = requireIndex(octane, "octane")
        return mutate { Jni15.tokenCompress(it, o) }
    }

    /** With tokens at `tier` fused upward (at `octane`). */
    fun fuse(octane: Int, tier: Int): TokenHolding {
        val o = requireIndex(octane, "octane")
        val t = requireIndex(tier, "tier")
        return mutate { Jni15.tokenFuse(it, o, t) }
    }

    /** With one token at `tier` split into the two below it. */
    fun split(tier: Int): TokenHolding {
        val t = requireIndex(tier, "tier")
        return mutate { Jni15.tokenSplit(it, t) }
    }

    private inline fun mutate(f: (LongArray) -> Int): TokenHolding {
        val x = c.copyOf()
        Status.check(f(x), null)
        return TokenHolding(x)
    }

    override fun equals(other: Any?): Boolean = other is TokenHolding && c.contentEquals(other.c)

    override fun hashCode(): Int = c.contentHashCode()

    override fun toString(): String =
        c.withIndex().filter { it.value != 0L }.joinToString(", ", "TokenHolding[", "]") { "${it.index}:${it.value.toULong()}" }
}

/**
 * A token wallet for one model: a holding kept canonical at its octane.
 * Every change (mint, spend, transfer, octane) needs `MINT` and passes
 * [capability]. Reads need none but fail closed once the context halted.
 * The wallet belongs to the context and is internally serialized.
 */
class TokenWallet private constructor(
    /** The capability gated calls pass. */
    val capability: Capability,
    handle: Long,
) : Closeable {
    private val st get() = capability.context.state
    internal val res =
        NativeResource("token wallet", handle, Jni15::tokenWalletDestroy, capability.context.state, capability.context.res)

    init {
        val r = res
        Backstop.register(this) { r.close() }
    }

    /** Model id (uint32), octane, value and pieces. */
    data class Info(val modelId: Long, val octane: Int, val value: Long, val pieces: Long)

    companion object {
        /** An empty wallet for `modelId` (uint32) at `octane`. */
        @JvmStatic
        fun create(cap: Capability, modelId: Long, octane: Int): TokenWallet {
            requireU32(modelId, "modelId")
            val o = requireIndex(octane, "octane")
            return cap.res.use { h ->
                val out = LongArray(1)
                Status.check(Jni15.tokenWalletCreate(h, modelId, o, out), cap.context.state)
                TokenWallet(cap, out[0])
            }
        }
    }

    /** Adds `amount` of value. Needs `MINT`. */
    fun mint(amount: Long) = gated { c, h -> Jni15.tokenWalletMint(c, h, amount) }

    /** Removes `amount` of value. Needs `MINT`. */
    fun spend(amount: Long) = gated { c, h -> Jni15.tokenWalletSpend(c, h, amount) }

    /** Re-canonicalizes at a new octane. Needs `MINT`. */
    fun setOctane(octane: Int) {
        val o = requireIndex(octane, "octane")
        gated { c, h -> Jni15.tokenWalletSetOctane(c, h, o) }
    }

    /** Moves `amount` of value to `to`. Needs `MINT`. */
    fun transfer(to: TokenWallet, amount: Long) {
        capability.res.use { c ->
            res.use { h ->
                if (to === this) {
                    Status.check(Jni15.tokenWalletTransfer(c, h, h, amount), st)
                } else {
                    to.res.use { t -> Status.check(Jni15.tokenWalletTransfer(c, h, t, amount), st) }
                }
            }
        }
    }

    private inline fun gated(f: (Long, Long) -> Int) {
        capability.res.use { c -> res.use { h -> Status.check(f(c, h), st) } }
    }

    /** Model, octane, value, pieces. */
    fun info(): Info = res.use { h ->
        val o = LongArray(4)
        Status.check(Jni15.tokenWalletInfo(h, o), st)
        Info(o[0], o[1].toInt(), o[2], o[3])
    }

    /** The value held. */
    fun value(): Long = info().value

    /** The holding. */
    fun holding(): TokenHolding = res.use { h ->
        val o = LongArray(Tokens.TIERS)
        Status.check(Jni15.tokenWalletCounts(h, o), st)
        TokenHolding.of(o)
    }

    /** True once closed. */
    val isClosed: Boolean get() = res.isClosed

    /** Frees the wallet. Idempotent. */
    override fun close() = res.close()
}
