// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.mobile

import java.io.ByteArrayOutputStream
import java.io.Closeable
import org.zedec.zxv.mobile.internal.Backstop
import org.zedec.zxv.mobile.internal.E_ARG
import org.zedec.zxv.mobile.internal.E_BUFFER
import org.zedec.zxv.mobile.internal.E_NOT_FOUND
import org.zedec.zxv.mobile.internal.E_RANGE
import org.zedec.zxv.mobile.internal.Jni15
import org.zedec.zxv.mobile.internal.NativeResource
import org.zedec.zxv.mobile.internal.OK
import org.zedec.zxv.mobile.internal.Status
import org.zedec.zxv.mobile.internal.requireIndex
import org.zedec.zxv.mobile.internal.requireLength
import org.zedec.zxv.mobile.internal.requireU8

/**
 * A 64-byte freight header: version, k (data rows), flags, payload length,
 * the member's place in a set, the freight id, the op-stream expansion
 * limit and the payload's SHA-256. A value. Pure.
 */
class FreightHeader private constructor(
    private val b: ByteArray,
    val version: Int,
    val k: Int,
    val flags: Int,
    val payloadLength: Int,
    val seq: Int,
    val seqCount: Int,
    /** uint64 bits. */
    val freightId: Long,
    /** uint64 bits. */
    val expandLimit: Long,
    private val sha: ByteArray,
) {
    companion object {
        /** The header size. */
        const val BYTES = 64

        /** Parses and checks a header. */
        @JvmStatic
        fun parse(header: ByteArray): FreightHeader {
            requireLength(header, BYTES, "header")
            val h = header.copyOf()
            val o = LongArray(8)
            val sha = ByteArray(32)
            Status.check(Jni15.freightHeaderParse(h, o, sha), null)
            return FreightHeader(h, o[0].toInt(), o[1].toInt(), o[2].toInt(), o[3].toInt(), o[4].toInt(), o[5].toInt(), o[6], o[7], sha)
        }

        /** True if `header` is a well-formed header. */
        @JvmStatic
        fun isValid(header: ByteArray?): Boolean =
            header != null && header.size == BYTES && Jni15.freightHeaderCheck(header) == OK

        /** The header for a single freight carrying `payload`. */
        @JvmStatic
        fun forPayload(freightId: Long, k: Int, flags: Int, expandLimit: Long, payload: ByteArray): FreightHeader {
            requireU8(k, "k")
            requireU8(flags, "flags")
            val out = ByteArray(BYTES)
            Status.check(Jni15.freightHeaderInit(freightId, k, flags, expandLimit, payload, out), null)
            return parse(out)
        }

        /** A header from its fields. */
        @JvmStatic
        @Suppress("LongParameterList")
        fun of(
            k: Int, flags: Int, payloadLength: Int, seq: Int, seqCount: Int, freightId: Long, expandLimit: Long,
            payloadSha256: ByteArray,
        ): FreightHeader {
            requireU8(k, "k")
            requireU8(flags, "flags")
            for ((v, n) in listOf(payloadLength to "payloadLength", seq to "seq", seqCount to "seqCount")) {
                if (v !in 0..0xFFFF) throw Status.usage(E_ARG, "$n $v is not a uint16")
            }
            requireLength(payloadSha256, 32, "payloadSha256")
            val out = ByteArray(BYTES)
            val f = intArrayOf(k, flags, payloadLength, seq, seqCount)
            Status.check(Jni15.freightHeaderSerialize(f, freightId, expandLimit, payloadSha256, out), null)
            return parse(out)
        }
    }

    /** A copy of the 64 bytes. */
    fun toBytes(): ByteArray = b.copyOf()

    /** True if the payload is an op stream. */
    val isOpStream: Boolean get() = flags and Freight.F_OPSTREAM != 0

    /** A copy of the payload's SHA-256. */
    val payloadSha256: ByteArray get() = sha.copyOf()

    override fun equals(other: Any?): Boolean = other is FreightHeader && b.contentEquals(other.b)

    override fun hashCode(): Int = b.contentHashCode()

    override fun toString(): String = "FreightHeader[${b.joinToString("") { "%02x".format(it) }}]"
}

/**
 * Erasure-coded freight: a payload of up to 3360 bytes in 168 rows of 21-byte
 * packets, any `k` distinct rows of which rebuild it; and sets of freights
 * for larger data. Pure.
 */
object Freight {
    const val PACKET_BYTES = 21
    const val ROW_PAYLOAD = 20
    const val ROWS = 168
    const val BYTES = 3528
    const val MAX_PAYLOAD = 3360
    const val MAX_SET = 16384
    const val SET_MAX_BYTES: Long = 55_050_240L

    /** The header flag for an op-stream payload. */
    const val F_OPSTREAM = 0x01

    /** One member of a set: its header and its 3528 bytes of packets. */
    class Member(val header: FreightHeader, packets: ByteArray) {
        private val p = packets.copyOf()

        /** A copy of the packets. */
        val packets: ByteArray get() = p.copyOf()

        /** The packet of `row`. */
        fun packet(row: Int): ByteArray = Freight.packet(p, row)
    }

    /** The 168 packets of `payload` under `header`, concatenated. */
    @JvmStatic
    fun encode(header: FreightHeader, payload: ByteArray): ByteArray =
        ByteArray(BYTES).also { Status.check(Jni15.freightEncode(header.toBytes(), payload, it), null) }

    /** Rebuilds the payload from any `k` distinct packets (concatenated); [ErasureException] if too few. */
    @JvmStatic
    fun decode(header: FreightHeader, packets: ByteArray): ByteArray {
        val h = header.toBytes()
        return Status.bytes(null) { b, n -> Jni15.freightDecode(h, packets, b, n) }
    }

    /** Concatenates packets. */
    @JvmStatic
    fun packets(vararg packets: ByteArray): ByteArray {
        val out = ByteArray(packets.size * PACKET_BYTES)
        packets.forEachIndexed { i, p ->
            requireLength(p, PACKET_BYTES, "packet")
            p.copyInto(out, i * PACKET_BYTES)
        }
        return out
    }

    /** The packet of `row` in an encoded freight. */
    @JvmStatic
    fun packet(encoded: ByteArray, row: Int): ByteArray {
        requireLength(encoded, BYTES, "encoded")
        if (row !in 0 until ROWS) throw Status.usage(E_RANGE, "row $row")
        return encoded.copyOfRange(row * PACKET_BYTES, (row + 1) * PACKET_BYTES)
    }

    /** How many freights a set for `length` bytes at `k` needs. */
    @JvmStatic
    fun setCount(length: Long, k: Int): Int {
        requireU8(k, "k")
        return LongArray(1).also { Status.check(Jni15.freightSetCount(length, k, it), null) }[0].toInt()
    }

    /** Member `seq` of the set for `data`. */
    @JvmStatic
    fun split(freightId: Long, k: Int, flags: Int, expandLimit: Long, data: ByteArray, seq: Int): Member {
        requireU8(k, "k")
        requireU8(flags, "flags")
        val s = requireIndex(seq, "seq")
        val h = ByteArray(FreightHeader.BYTES)
        val p = ByteArray(BYTES)
        Status.check(Jni15.freightSplit(freightId, k, flags, expandLimit, data, s, h, p), null)
        return Member(FreightHeader.parse(h), p)
    }
}

/** Collects set members in any order and rebuilds the data. Pure; methods are synchronized. */
class FreightJoin private constructor(handle: Long) : Closeable {
    private val res = NativeResource("freight join", handle, Jni15::freightJoinDestroy, null, null)

    init {
        val r = res
        Backstop.register(this) { r.close() }
    }

    /** Members received, members needed, whether complete, and the total length. */
    data class Status(val received: Int, val count: Int, val complete: Boolean, val total: Long)

    companion object {
        /** A join for a set of at most `capacity` bytes. */
        @JvmStatic
        fun create(capacity: Long): FreightJoin {
            val o = LongArray(1)
            org.zedec.zxv.mobile.internal.Status.check(Jni15.freightJoinCreate(capacity, o), null)
            return FreightJoin(o[0])
        }
    }

    /** Adds a member's packets (any `k` distinct packets of it). */
    @Synchronized
    fun add(header: FreightHeader, packets: ByteArray) {
        val h = header.toBytes()
        res.use { j -> org.zedec.zxv.mobile.internal.Status.check(Jni15.freightJoinAdd(j, h, packets), null) }
    }

    /** Adds a whole member. */
    fun add(member: Freight.Member) = add(member.header, member.packets)

    /** Progress. */
    @Synchronized
    fun status(): Status = res.use { j ->
        val o = LongArray(4)
        org.zedec.zxv.mobile.internal.Status.check(Jni15.freightJoinStatus(j, o), null)
        Status(o[0].toInt(), o[1].toInt(), o[2] != 0L, o[3])
    }

    /** The rebuilt data; `ZXV_E_STATE` until complete. */
    @Synchronized
    fun result(): ByteArray = res.use { j -> org.zedec.zxv.mobile.internal.Status.bytes(null) { b, n -> Jni15.freightJoinResult(j, b, n) } }

    /** True once closed. */
    val isClosed: Boolean get() = res.isClosed

    /** Frees the join. Idempotent. */
    @Synchronized
    override fun close() = res.close()
}

/** The procedural generators an op stream's SEED op can name. */
enum class Generator(val code: Int) {
    XORSHIFT(1), REPEAT(2), FIBWORD(3), SIERPINSKI(4);

    companion object {
        /** The generator for a code. */
        @JvmStatic
        fun fromCode(code: Int): Generator =
            entries.firstOrNull { it.code == code } ?: throw Status.usage(E_ARG, "unknown generator $code")
    }
}

/**
 * Resolves a CIDREF op: the bytes `cid` names, exactly `length` long, or
 * null if unknown (the expansion then fails with `ZXV_E_NOT_FOUND`).
 */
fun interface CidResolver {
    fun resolve(cid: ByteArray, length: Long): ByteArray?
}

/**
 * Op streams: a compact description of bytes (literals, back copies,
 * CID references, procedural seeds) that expands under a limit. Pure.
 */
object OpStream {
    const val OPS_MAX: Long = 67_108_864L
    const val CID_MAX = 128
    const val GEN_PARAM_MAX = 32

    /** The declared total and whether a SHA-256 is carried. */
    data class Peek(val total: Long, val hasSha: Boolean)

    /** Reads an op stream's header. */
    @JvmStatic
    fun peek(ops: ByteArray): Peek {
        val o = LongArray(2)
        Status.check(Jni15.freightOpsPeek(ops, o), null)
        return Peek(o[0], o[1] != 0L)
    }

    /**
     * Expands `ops` to at most `limit` bytes. CIDREF ops go to `resolver`
     * (none: they fail with `ZXV_E_NOT_FOUND`).
     *
     * The resolver never runs inside a native call (so it can never throw
     * through C, and it may block): the shim reports the first unresolved
     * CID, the resolver answers it here, and the expansion runs again with
     * every answer so far. A resolver's exception is rethrown as is; an
     * answer of the wrong length is a [ZxvUsageException].
     */
    @JvmStatic
    @JvmOverloads
    fun expand(ops: ByteArray, limit: Long, resolver: CidResolver? = null): ByteArray {
        val cids = ByteArrayOutputStream()
        val data = ByteArrayOutputStream()
        val cidLens = ArrayList<Int>()
        val dataLens = ArrayList<Long>()
        val missCid = ByteArray(CID_MAX)
        val miss = LongArray(2)
        val seen = HashSet<String>()
        while (true) {
            val tc = if (resolver == null) null else cids.toByteArray()
            val tcl = if (resolver == null) null else cidLens.toIntArray()
            val td = if (resolver == null) null else data.toByteArray()
            val tdl = if (resolver == null) null else dataLens.toLongArray()
            val len = LongArray(1)
            miss.fill(0)
            var st = Jni15.freightOpsExpand(ops, limit, tc, tcl, td, tdl, null, len, missCid, miss)
            var out = ByteArray(0)
            if (st == E_BUFFER) {
                out = ByteArray(Math.toIntExact(len[0]))
                miss.fill(0)
                st = Jni15.freightOpsExpand(ops, limit, tc, tcl, td, tdl, out, len, missCid, miss)
            }
            if (st == OK) return if (len[0].toInt() == out.size) out else out.copyOf(len[0].toInt())
            val detail = Status.lastError()
            if (st != E_NOT_FOUND || resolver == null || miss[0] == 0L) throw Status.raise(st, detail, null)
            val id = missCid.copyOf(miss[0].toInt())
            val need = miss[1]
            // Each round answers a new CID; a repeat would loop forever, so report it instead.
            if (!seen.add(id.contentToString() + "/" + need)) throw Status.raise(st, detail, null)
            val got = resolver.resolve(id, need) ?: throw Status.raise(st, detail, null)
            if (got.size.toLong() != need) {
                throw Status.usage(E_ARG, "CidResolver returned ${got.size} bytes, the op needs $need")
            }
            cids.write(id)
            cidLens.add(id.size)
            data.write(got)
            dataLens.add(need)
        }
    }

    /** `input` as an op stream (literals and copies), with its SHA-256 if `withSha`. */
    @JvmStatic
    fun encode(input: ByteArray, withSha: Boolean): ByteArray =
        Status.bytes(null) { b, n -> Jni15.freightOpsEncode(input, if (withSha) 1 else 0, b, n) }

    /** The largest [encode] output for `n` input bytes. */
    @JvmStatic
    fun bound(n: Long, withSha: Boolean): Long =
        LongArray(1).also { Status.check(Jni15.freightOpsBound(n, if (withSha) 1 else 0, it), null) }[0]

    /** `length` bytes from a generator. */
    @JvmStatic
    fun generate(generator: Generator, params: ByteArray?, length: Int): ByteArray {
        requireIndex(length, "length")
        return ByteArray(length).also { Status.check(Jni15.freightGenRun(generator.code, params ?: ByteArray(0), it), null) }
    }
}

/** Writes an op stream op by op. Pure; methods are synchronized. */
class OpStreamWriter private constructor(handle: Long) : Closeable {
    private val res = NativeResource("op stream writer", handle, Jni15::freightOpsWriterDestroy, null, null)

    init {
        val r = res
        Backstop.register(this) { r.close() }
    }

    companion object {
        /** A writer for `declaredTotal` bytes, with an optional SHA-256 of them, of at most `maxBytes` encoded. */
        @JvmStatic
        fun create(declaredTotal: Long, sha256: ByteArray?, maxBytes: Long): OpStreamWriter {
            sha256?.let { requireLength(it, 32, "sha256") }
            val o = LongArray(1)
            Status.check(Jni15.freightOpsWriterCreate(declaredTotal, sha256, maxBytes, o), null)
            return OpStreamWriter(o[0])
        }
    }

    private inline fun op(f: (Long) -> Int): OpStreamWriter {
        res.use { w -> Status.check(f(w), null) }
        return this
    }

    /** A literal. */
    @Synchronized
    fun literal(bytes: ByteArray): OpStreamWriter = op { Jni15.freightOpsPutLiteral(it, bytes) }

    /** A copy of `length` bytes from `distance` back. */
    @Synchronized
    fun copy(distance: Long, length: Long): OpStreamWriter = op { Jni15.freightOpsPutCopy(it, distance, length) }

    /** A reference to `length` bytes named by `cid`. */
    @Synchronized
    fun cidRef(cid: ByteArray, length: Long): OpStreamWriter = op { Jni15.freightOpsPutCidref(it, cid, length) }

    /** `length` bytes from a generator. */
    @Synchronized
    fun seed(generator: Generator, params: ByteArray?, length: Long): OpStreamWriter =
        op { Jni15.freightOpsPutSeed(it, generator.code, params ?: ByteArray(0), length) }

    /** The encoded stream. */
    @Synchronized
    fun finish(): ByteArray = res.use { w -> Status.bytes(null) { b, n -> Jni15.freightOpsWriterFinish(w, b, n) } }

    /** True once closed. */
    val isClosed: Boolean get() = res.isClosed

    /** Frees the writer. Idempotent. */
    @Synchronized
    override fun close() = res.close()
}
