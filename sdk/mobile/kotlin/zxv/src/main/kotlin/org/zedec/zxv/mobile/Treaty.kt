// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.mobile

import java.io.Closeable
import org.zedec.zxv.mobile.internal.Backstop
import org.zedec.zxv.mobile.internal.E_TREATY
import org.zedec.zxv.mobile.internal.Jni15
import org.zedec.zxv.mobile.internal.NativeResource
import org.zedec.zxv.mobile.internal.Status
import org.zedec.zxv.mobile.internal.cstr
import org.zedec.zxv.mobile.internal.requireIndex

/** The rule a treaty broke (`ZXV_TREATY_F_*`). */
enum class TreatyFault(val code: Int) {
    /** The call gave no fault code. */
    UNKNOWN(-1),

    /** The treaty validates. */
    NONE(0),

    /** The record or text does not start with the treaty magic. */
    MAGIC(2),

    /** Sutra syntax error. */
    SYNTAX(3),

    /** Bad name. */
    NAME(4),

    /** Bad asset code. */
    ASSET(5),

    /** Unknown operation. */
    OP(6),

    /** Party or obligation count out of range. */
    COUNT(7),

    /** A PIC S9(18) number is malformed or out of range. */
    NUMBER(8),

    /** Bad party (id, duplicate, deposit). */
    PARTY(9),

    /** Bad weight. */
    WEIGHT(10),

    /** Bad claim. */
    CLAIM(11),

    /** Bad obligation. */
    OWE(12),

    /** Record padding is not spaces. */
    PAD(13);

    companion object {
        /** The fault for a code ([UNKNOWN] for codes not listed). */
        @JvmStatic
        fun fromCode(code: Int): TreatyFault = entries.firstOrNull { it.code == code } ?: UNKNOWN
    }
}

/** A treaty's operation (`ZXV_TREATY_OP_*`). */
enum class TreatyOp(val code: Int) {
    /** No operation set. */
    NONE(0),

    /** Multilateral netting. */
    NET(1),

    /** Pro-rata settlement of claims from deposits. */
    SETL(2);

    companion object {
        /** The operation for a code ([NONE] for unknown codes). */
        @JvmStatic
        fun fromCode(code: Int): TreatyOp = entries.firstOrNull { it.code == code } ?: NONE
    }
}

/**
 * COBOL PIC S9(18) arithmetic exactly as the treaty engine does it: a result
 * beyond +-999,999,999,999,999,999 is `ZXV_E_RANGE`, never wrapped. Pure.
 */
object Pic18 {
    /** The largest PIC S9(18) magnitude. */
    const val MAX: Long = 999_999_999_999_999_999L

    /** A quotient and remainder. */
    data class Division(val quotient: Long, val remainder: Long)

    /** `a + b`. */
    @JvmStatic
    fun add(a: Long, b: Long): Long = one(Jni15::treatyPicAdd, a, b)

    /** `a - b`. */
    @JvmStatic
    fun subtract(a: Long, b: Long): Long = one(Jni15::treatyPicSub, a, b)

    /** `a * b`. */
    @JvmStatic
    fun multiply(a: Long, b: Long): Long = one(Jni15::treatyPicMul, a, b)

    /** `a / b` truncated toward zero, and the remainder. */
    @JvmStatic
    fun divide(a: Long, b: Long): Division {
        val o = LongArray(2)
        Status.check(Jni15.treatyPicDiv(a, b, o), null)
        return Division(o[0], o[1])
    }

    private inline fun one(f: (Long, Long, LongArray?) -> Int, a: Long, b: Long): Long {
        val o = LongArray(1)
        Status.check(f(a, b, o), null)
        return o[0]
    }
}

/**
 * A treaty: parties, deposits, claims, weights and obligations, read from
 * Sutra text or a 2542-byte COBOL copybook record, validated, and run
 * (netting or pro-rata settlement) into a [TreatyResult]. Pure: no context
 * and no capability. One treaty is not meant to be shared between threads
 * while it is being changed; its methods are synchronized.
 *
 * Close it (`use { }`); a GC backstop frees a dropped one.
 */
class Treaty private constructor(handle: Long) : Closeable {
    private val res = NativeResource("treaty", handle, Jni15::treatyDestroy, null, null)

    init {
        val r = res
        Backstop.register(this) { r.close() }
    }

    /** One party. Weights are an exact fraction `weightNum / weightDen`. */
    data class Party(val id: String, val deposit: Long, val claim: Long, val weightNum: Long, val weightDen: Long)

    /** One obligation: party `from` owes party `to` the `amount`. */
    data class Obligation(val from: Int, val to: Int, val amount: Long)

    companion object {
        /** The size of a copybook record. */
        const val RECORD_BYTES = 2542

        /** The size of a result record. */
        const val RESULT_RECORD_BYTES = 2422

        /** The longest Sutra text. */
        const val SUTRA_MAX = 3843

        /** Most parties. */
        const val MAX_PARTIES = 16

        /** Most obligations. */
        const val MAX_OBLIGATIONS = 64

        /** An empty treaty. */
        @JvmStatic
        fun create(): Treaty {
            val o = LongArray(1)
            Status.check(Jni15.treatyCreate(o), null)
            return Treaty(o[0])
        }

        /** A treaty parsed from Sutra text; [TreatyException] carries the fault and offset. */
        @JvmStatic
        fun parse(sutra: String): Treaty = create().also { t -> closeOnError(t) { t.load(sutra) } }

        /** A treaty parsed from a copybook record. */
        @JvmStatic
        fun parseRecord(record: ByteArray): Treaty = create().also { t -> closeOnError(t) { t.loadRecord(record) } }

        private inline fun closeOnError(t: Treaty, block: () -> Unit) {
            try {
                block()
            } catch (e: Throwable) {
                t.close()
                throw e
            }
        }
    }

    /** Replaces the contents with the parsed Sutra text. */
    @Synchronized
    fun load(sutra: String) {
        val text = cstr(sutra, "sutra")
        val f = LongArray(2)
        res.use { h -> checkParse(Jni15.treatyParseSutra(h, text, f), f) }
    }

    /** Replaces the contents with the parsed copybook record. */
    @Synchronized
    fun loadRecord(record: ByteArray) {
        val f = LongArray(2)
        res.use { h -> checkParse(Jni15.treatyParseRecord(h, record, f), f) }
    }

    private fun checkParse(st: Int, f: LongArray) {
        if (st == E_TREATY) {
            throw TreatyException(st, Status.name(st), Status.lastError(), TreatyFault.fromCode(f[1].toInt()), f[0])
        }
        Status.check(st, null)
    }

    /** Empties the treaty. */
    @Synchronized
    fun clear() {
        res.use { h -> Status.check(Jni15.treatyClear(h), null) }
    }

    /** The canonical Sutra text. */
    @Synchronized
    fun toSutra(): String = res.use { h ->
        withFault(h) { Status.checkedText(null) { b, n -> Jni15.treatyEmitSutra(h, b, n) } }
    }

    /** The 2542-byte copybook record. */
    @Synchronized
    fun toRecord(): ByteArray = res.use { h ->
        withFault(h) { Status.bytes(null) { b, n -> Jni15.treatyEmitRecord(h, b, n) } }
    }

    private inline fun <T> withFault(h: Long, block: () -> T): T = try {
        block()
    } catch (e: TreatyException) {
        throw TreatyException(e.status, e.statusName, e.detail, faultOf(h), -1)
    }

    private fun faultOf(h: Long): TreatyFault {
        val f = IntArray(1)
        val st = Jni15.treatyValidate(h, f)
        if (st != 0 && st != E_TREATY) Status.check(st, null)
        return TreatyFault.fromCode(f[0])
    }

    /** Throws [TreatyException] (with its [TreatyFault]) unless the treaty validates. */
    @Synchronized
    fun validate() {
        val f = fault()
        if (f != TreatyFault.NONE) {
            throw TreatyException(E_TREATY, Status.name(E_TREATY), "the treaty does not validate: $f", f, -1)
        }
    }

    /** The first rule the treaty breaks, or [TreatyFault.NONE]. */
    @Synchronized
    fun fault(): TreatyFault = res.use { h -> faultOf(h) }

    /** True if both treaties hold the same content. */
    fun contentEquals(other: Treaty): Boolean {
        val (a, b) = if (System.identityHashCode(this) <= System.identityHashCode(other)) this to other else other to this
        synchronized(a) {
            synchronized(b) {
                return res.use { ha ->
                    other.res.use { hb ->
                        val o = IntArray(1)
                        Status.check(Jni15.treatyEqual(ha, hb, o), null)
                        o[0] != 0
                    }
                }
            }
        }
    }

    /** The treaty's name. */
    @Synchronized
    fun name(): String = res.use { h -> Status.checkedText(null) { b, n -> Jni15.treatyName(h, b, n) } }

    /** The asset code. */
    @Synchronized
    fun asset(): String = res.use { h -> Status.checkedText(null) { b, n -> Jni15.treatyAsset(h, b, n) } }

    /** The operation. */
    @Synchronized
    fun op(): TreatyOp = TreatyOp.fromCode(header()[0].toInt())

    /** The number of parties. */
    @Synchronized
    fun partyCount(): Int = header()[1].toInt()

    /** The number of obligations. */
    @Synchronized
    fun obligationCount(): Int = header()[2].toInt()

    private fun header(): LongArray = res.use { h ->
        LongArray(3).also { Status.check(Jni15.treatyHeader(h, it), null) }
    }

    /** Party `index`. */
    @Synchronized
    fun party(index: Int): Party {
        val i = requireIndex(index, "index")
        return res.use { h ->
            val id = Status.checkedText(null) { b, n -> Jni15.treatyPartyId(h, i, b, n) }
            val o = LongArray(4)
            Status.check(Jni15.treatyParty(h, i, o), null)
            Party(id, o[0], o[1], o[2], o[3])
        }
    }

    /** Every party, in order. */
    @Synchronized
    fun parties(): List<Party> = List(partyCount()) { party(it) }

    /** Obligation `index`. */
    @Synchronized
    fun obligation(index: Int): Obligation {
        val i = requireIndex(index, "index")
        return res.use { h ->
            val o = LongArray(3)
            Status.check(Jni15.treatyOwe(h, i, o), null)
            Obligation(o[0].toInt(), o[1].toInt(), o[2])
        }
    }

    /** Every obligation, in order. */
    @Synchronized
    fun obligations(): List<Obligation> = List(obligationCount()) { obligation(it) }

    /** Runs the treaty's operation. The result is a plain value; nothing to close. */
    @Synchronized
    fun run(): TreatyResult {
        val r = res.use { h ->
            withFault(h) { LongArray(1).also { Status.check(Jni15.treatyRun(h, it), null) }[0] }
        }
        try {
            return TreatyResult.read(r)
        } finally {
            Jni15.treatyResultDestroy(r)
        }
    }

    /** True once closed. */
    val isClosed: Boolean get() = res.isClosed

    /** Frees the treaty. Idempotent. */
    override fun close() = res.close()

    override fun toString(): String =
        if (res.isClosed) "Treaty[closed]" else "Treaty[${name()} ${asset()} ${op()}, ${partyCount()} parties]"
}

/** What [Treaty.run] produced: per-party results, settlement legs, fees and the result record. */
class TreatyResult private constructor(
    /** Fees taken, in minor units. */
    val fees: Long,
    /** Per-party results, in party order. */
    val parties: List<PartyResult>,
    /** The settlement legs. */
    val legs: List<Leg>,
    private val record: ByteArray,
) {
    /** One party's result. */
    data class PartyResult(val net: Long, val balance: Long, val dividend: Long)

    /** One settlement leg. */
    data class Leg(val from: Int, val to: Int, val amount: Long)

    internal companion object {
        fun read(r: Long): TreatyResult {
            val s = LongArray(3)
            Status.check(Jni15.treatyResultSummary(r, s), null)
            val ps = List(s[0].toInt()) { i ->
                val o = LongArray(3)
                Status.check(Jni15.treatyResultParty(r, i.toLong(), o), null)
                PartyResult(o[0], o[1], o[2])
            }
            val ls = List(s[1].toInt()) { i ->
                val o = LongArray(3)
                Status.check(Jni15.treatyResultLeg(r, i.toLong(), o), null)
                Leg(o[0].toInt(), o[1].toInt(), o[2])
            }
            val rec = Status.bytes(null) { b, n -> Jni15.treatyResultRecord(r, b, n) }
            return TreatyResult(s[2], ps, ls, rec)
        }
    }

    /** The number of parties. */
    val partyCount: Int get() = parties.size

    /** The number of legs. */
    val legCount: Int get() = legs.size

    /** Party `index`'s result. */
    fun party(index: Int): PartyResult = parties[index]

    /** Leg `index`. */
    fun leg(index: Int): Leg = legs[index]

    /** A copy of the 2422-byte result record. */
    fun toRecord(): ByteArray = record.copyOf()

    override fun toString(): String = "TreatyResult[fees=$fees, parties=$parties, legs=$legs]"
}
