// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.mobile

import java.io.Closeable
import org.zedec.zxv.mobile.internal.Backstop
import org.zedec.zxv.mobile.internal.E_ARG
import org.zedec.zxv.mobile.internal.Jni
import org.zedec.zxv.mobile.internal.NativeResource
import org.zedec.zxv.mobile.internal.Status
import org.zedec.zxv.mobile.internal.requireLength
import org.zedec.zxv.mobile.internal.requireU32
import org.zedec.zxv.mobile.internal.requireU8
import org.zedec.zxv.mobile.internal.use2

/**
 * The ABI 1.3 quarantine log: an append-only, SHA-256 hash-chained record of
 * forbidden work, read for audit and never replayed.
 *
 * Creating and appending need `ATTEST` and pass [capability] on every call;
 * reading needs none but fails closed once the context halted. A full log is
 * REFUSED (`ZXV_E_CAPACITY`) and records nothing: the caller must then halt
 * the context ([ZxvContext.halt]) or stop the work. [verify] raises
 * [ZxvFatalError] (and poisons the context) on a broken chain.
 *
 * The log belongs to the context (ABI R6): it is closed with the context or
 * by [close], and stays readable after its capability is closed.
 */
class QuarantineLog private constructor(
    /** The capability every append passes. */
    val capability: Capability,
    handle: Long,
) : Closeable {
    private val res = NativeResource("quarantine log", handle, Jni::hexlogDestroy, capability.context.state, capability.context.res)
    private val st get() = capability.context.state

    init {
        val r = res
        Backstop.register(this) { r.close() }
    }

    /** One record. Byte arrays are copies. */
    class Record(
        val seq: Long,
        val tag: Hexagram,
        val reason: Int,
        /** uint32 submitter id. */
        val submitter: Long,
        subject: ByteArray,
        prev: ByteArray,
        hash: ByteArray,
    ) {
        private val s = subject.copyOf()
        private val p = prev.copyOf()
        private val h = hash.copyOf()

        /** The 32-byte digest of the quarantined work. */
        val subject: ByteArray get() = s.copyOf()

        /** The previous record's hash (zeros for the first). */
        val prev: ByteArray get() = p.copyOf()

        /** This record's SHA-256 chain hash. */
        val hash: ByteArray get() = h.copyOf()
    }

    companion object {
        /** The largest capacity. */
        const val MAX_RECORDS: Long = 65536

        /** A log of at most `capacity` records (1..65536). Needs `ATTEST`. */
        @JvmStatic
        fun create(cap: Capability, capacity: Long): QuarantineLog {
            requireU32(capacity, "capacity")
            return cap.res.use { h ->
                val out = LongArray(1)
                Status.check(Jni.hexlogCreate(h, capacity, out), cap.context.state)
                QuarantineLog(cap, out[0])
            }
        }
    }

    /**
     * Quarantines one record; only forbidden tags are accepted (`ZXV_E_ARG`).
     *
     * @param reason a caller reason code, 0..255
     * @param submitter a uint32 id of who submitted the work
     * @param subject a 32-byte digest of the work
     * @return the record's sequence number
     * @throws ZxvRefusedException `ZXV_E_CAPACITY` when full: halt the context
     */
    fun append(tag: Hexagram, reason: Int, submitter: Long, subject: ByteArray): Long {
        requireU8(reason, "reason")
        requireU32(submitter, "submitter")
        requireLength(subject, 32, "subject")
        return use2(capability.res, res) { c, l ->
            val out = LongArray(1)
            Status.check(Jni.hexlogAppend(c, l, tag.tag, reason, submitter, subject, out), st)
            out[0]
        }
    }

    /** Number of records. */
    fun count(): Long = res.use { l ->
        val out = LongArray(1)
        Status.check(Jni.hexlogCount(l, out), st)
        out[0]
    }

    /** The chain head: the last record's hash, 32 zeros when empty. */
    fun head(): ByteArray = res.use { l ->
        val out = ByteArray(32)
        Status.check(Jni.hexlogHead(l, out), st)
        out
    }

    /** Record `index`, oldest first. */
    fun record(index: Long): Record {
        if (index < 0) throw Status.usage(E_ARG, "index $index is negative")
        requireU32(index, "index")
        return res.use { l ->
            val o = LongArray(4)
            val s = ByteArray(32)
            val p = ByteArray(32)
            val h = ByteArray(32)
            Status.check(Jni.hexlogAt(l, index, o, s, p, h), st)
            Record(o[0], Hexagram.fromTag(o[1].toInt()), o[2].toInt(), o[3], s, p, h)
        }
    }

    /** Recomputes every link; a mismatch poisons the context and raises [ZxvFatalError]. */
    fun verify() {
        res.use { l -> Status.check(Jni.hexlogVerify(l), st) }
    }

    /** Destroys the log. Idempotent. */
    override fun close() = res.close()
}
