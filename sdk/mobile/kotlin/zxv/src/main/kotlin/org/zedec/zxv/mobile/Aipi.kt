// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.mobile

import java.io.Closeable
import org.zedec.zxv.mobile.internal.Backstop
import org.zedec.zxv.mobile.internal.E_ARG
import org.zedec.zxv.mobile.internal.E_CID
import org.zedec.zxv.mobile.internal.E_NOT_FOUND
import org.zedec.zxv.mobile.internal.Jni15
import org.zedec.zxv.mobile.internal.NativeResource
import org.zedec.zxv.mobile.internal.Status
import org.zedec.zxv.mobile.internal.cstr
import org.zedec.zxv.mobile.internal.requireLength
import org.zedec.zxv.mobile.internal.requireU32

/** Multicodec and multihash codes the AIPI and CID calls use. */
object Multicodec {
    /** Raw bytes. */
    const val RAW: Long = 0x55

    /** A dag-pb (UnixFS) node. */
    const val DAG_PB: Long = 0x70

    /** An AIPI function record (intent FN). */
    const val AIPI_FN: Long = 0x3A1000

    /** An AIPI function record, intent TENSOR_INFER. */
    const val AIPI_TENSOR_INFER: Long = 0x3A1001

    /** An AIPI function record, intent SETTLE_LEDGER. */
    const val AIPI_SETTLE_LEDGER: Long = 0x3A1002

    /** An AIPI function record, intent SWARM_ROUTE. */
    const val AIPI_SWARM_ROUTE: Long = 0x3A1003

    /** An AIPI call record. */
    const val AIPI_CALL: Long = 0x3A10C0

    /** The identity multihash. */
    const val HASH_IDENTITY: Int = 0x00

    /** The SHA2-256 multihash. */
    const val HASH_SHA2_256: Int = 0x12
}

/** The text base of a CID. */
enum class CidBase(val code: Int) {
    /** The canonical base for the version: base58btc for v0, base32 lower for v1. */
    DEFAULT(0),

    /** base58btc ('z' prefix for v1). */
    BASE58BTC(1),
}

/**
 * An IPFS content identifier (v0 or v1), checked canonical on the way in.
 * A value: equal when the bytes are equal. Pure.
 */
class Cid private constructor(
    private val b: ByteArray,
    /** 0 or 1. */
    val version: Int,
    /** The content multicodec (uint32). */
    val codec: Long,
    /** The multihash code. */
    val multihash: Int,
    private val d: ByteArray,
) {
    companion object {
        /** The longest binary CID. */
        const val MAX_BYTES = 80

        /** The longest CID text. */
        const val TEXT_MAX = 139

        /** The longest multihash digest. */
        const val DIGEST_MAX = 64

        /** A CID from its binary form. [CidException] if it is not canonical. */
        @JvmStatic
        fun of(bytes: ByteArray): Cid {
            val o = LongArray(3)
            val dig = ByteArray(DIGEST_MAX)
            val dl = LongArray(1)
            Status.check(Jni15.cidInfo(bytes, o, dig, dl), null)
            return Cid(bytes.copyOf(), o[0].toInt(), o[1], o[2].toInt(), dig.copyOf(dl[0].toInt()))
        }

        /** A CID from its text form (base58btc v0, or multibase v1). */
        @JvmStatic
        fun parse(text: String): Cid = of(Status.cid(null) { c, n -> Jni15.cidFromText(cstr(text, "text"), c, n) })

        /** The CIDv1 of `data` under `codec`, SHA2-256. */
        @JvmStatic
        fun compute(codec: Long, data: ByteArray): Cid {
            if (codec !in 0..0xFFFF_FFFFL) throw Status.usage(E_ARG, "codec $codec is not a uint32")
            return of(Status.cid(null) { c, n -> Jni15.cidCompute(codec, data, c, n) })
        }

        /** [compute] with [Multicodec.RAW]. */
        @JvmStatic
        fun raw(data: ByteArray): Cid = compute(Multicodec.RAW, data)

        /** The CIDv0 `ipfs add` gives `data` as one UnixFS file (one block, up to 256 KiB). */
        @JvmStatic
        fun unixfs(data: ByteArray): Cid = of(Status.cid(null) { c, n -> Jni15.cidUnixfs(data, c, n) })
    }

    /** A copy of the binary CID. */
    val bytes: ByteArray get() = b.copyOf()

    /** The binary length. */
    val length: Int get() = b.size

    /** A copy of the multihash digest. */
    val digest: ByteArray get() = d.copyOf()

    /** The text form in `base`. */
    fun toText(base: CidBase = CidBase.DEFAULT): String =
        Status.checkedText(null) { buf, n -> Jni15.cidToText(b, base.code.toLong(), buf, n) }

    /** The same content as a CIDv1 (this one if it already is). */
    fun toV1(): Cid = if (version == 1) this else of(Status.cid(null) { c, n -> Jni15.cidToV1(b, c, n) })

    /** True if `data` hashes to this CID; other errors still throw. */
    fun matches(data: ByteArray): Boolean {
        val st = Jni15.cidVerify(b, data)
        if (st == E_CID) return false
        Status.check(st, null)
        return true
    }

    /** Returns normally if `data` hashes to this CID; [CidException] if not. */
    fun verify(data: ByteArray) = Status.check(Jni15.cidVerify(b, data), null)

    /** The 32-byte DHT provider key (SHA-256 of the multihash). */
    fun providerKey(): ByteArray = ByteArray(32).also { Status.check(Jni15.aipiProviderKey(b, it), null) }

    /** Lower-case hex of the binary CID. */
    fun toHex(): String = b.joinToString("") { "%02x".format(it) }

    override fun equals(other: Any?): Boolean = other is Cid && b.contentEquals(other.b)

    override fun hashCode(): Int = b.contentHashCode()

    override fun toString(): String = toText()
}

/** What a function does (`ZXV_AIPI_INTENT_*`); it picks the record's multicodec. */
enum class Intent(val code: Int) {
    FN(0), TENSOR_INFER(1), SETTLE_LEDGER(2), SWARM_ROUTE(3);

    /** The multicodec of a function record with this intent. */
    val codec: Long get() = Multicodec.AIPI_FN + code

    companion object {
        /** The intent for a code. */
        @JvmStatic
        fun fromCode(code: Int): Intent = entries.firstOrNull { it.code == code } ?: badCode("Intent", code)
    }
}

/** How a result is verified (`ZXV_AIPI_VERIFY_*`). */
enum class VerifyMode(val code: Int) {
    CHECKER(1), CERT(2), QUORUM(3), LOCAL(4);

    companion object {
        /** The mode for a code. */
        @JvmStatic
        fun fromCode(code: Int): VerifyMode = entries.firstOrNull { it.code == code } ?: badCode("VerifyMode", code)
    }
}

/** Whether inputs may leave the device (`ZXV_AIPI_PRIVACY_*`). */
enum class Privacy(val code: Int) {
    PUBLIC(0), LOCAL(1);

    companion object {
        /** The privacy for a code. */
        @JvmStatic
        fun fromCode(code: Int): Privacy = entries.firstOrNull { it.code == code } ?: badCode("Privacy", code)
    }
}

/** Whether a function is deterministic. */
enum class Determinism(val code: Int) {
    NONDETERMINISTIC(0), DETERMINISTIC(1);

    companion object {
        /** The value for a code. */
        @JvmStatic
        fun fromCode(code: Int): Determinism = entries.firstOrNull { it.code == code } ?: badCode("Determinism", code)
    }
}

/** Where [AipiNode.plan] would run a call (`ZXV_AIPI_ROUTE_*`). */
enum class Route(val code: Int) {
    CACHE(1), LOCAL(2), FETCH_LOCAL(3), MESH(4), UNAVAILABLE(5);

    companion object {
        /** The route for a code. */
        @JvmStatic
        fun fromCode(code: Int): Route = entries.firstOrNull { it.code == code } ?: badCode("Route", code)
    }
}

private fun badCode(what: String, code: Int): Nothing = throw Status.usage(E_ARG, "not a $what code: $code")

private fun slice(b: ByteArray, off: Long, len: Long): ByteArray = b.copyOfRange(off.toInt(), (off + len).toInt())

/**
 * A content-addressed function record (at most 373 bytes): what to run
 * (module CID, entry export), how to verify it, its privacy and fuel cap.
 * A value, built with [builder] or [decode]. Pure.
 */
class FunctionRecord private constructor(
    private val rec: ByteArray,
    val intent: Intent,
    val verify: VerifyMode,
    val privacy: Privacy,
    val determinism: Determinism,
    val quorumN: Int,
    val quorumK: Int,
    /** The fuel cap (uint64 bits). */
    val fuelMax: Long,
    val module: Cid,
    /** The contract CID, or null. */
    val contract: Cid?,
    val entry: String,
    val checker: String,
    val certifier: String,
    /** The record's own CID. */
    val cid: Cid,
) {
    companion object {
        /** The longest record. */
        const val MAX_BYTES = 373

        /** The longest export name. */
        const val NAME_MAX = 63

        /** The largest quorum. */
        const val QUORUM_MAX = 8

        /** A builder. */
        @JvmStatic
        fun builder(): Builder = Builder()

        /** Encodes a record from its fields (the builder calls this). */
        @JvmStatic
        @Suppress("LongParameterList")
        fun encode(
            intent: Intent, verify: VerifyMode, privacy: Privacy, determinism: Determinism, quorumN: Int, quorumK: Int,
            fuelMax: Long, module: Cid, contract: Cid?, entry: String, checker: String?, certifier: String?,
        ): FunctionRecord {
            val u = longArrayOf(
                intent.code.toLong(), verify.code.toLong(), privacy.code.toLong(), determinism.code.toLong(),
                quorumN.toLong(), quorumK.toLong(),
            )
            val e = cstr(entry, "entry")
            val ch = checker?.let { cstr(it, "checker") }
            val ce = certifier?.let { cstr(it, "certifier") }
            val m = module.bytes
            val c = contract?.bytes
            return decode(Status.bytes(null) { b, n -> Jni15.aipiFnEncode(u, fuelMax, m, c, e, ch, ce, b, n) })
        }

        /** Decodes and checks a record. */
        @JvmStatic
        fun decode(record: ByteArray): FunctionRecord {
            val rec = record.copyOf()
            val o = LongArray(7)
            Status.check(Jni15.aipiFnDecode(rec, o), null)
            val module = Cid.of(Status.cid(null) { c, n -> Jni15.aipiFnGetCid(rec, 0, c, n) })
            val cb = ByteArray(Cid.MAX_BYTES)
            val cl = LongArray(1)
            val st = Jni15.aipiFnGetCid(rec, 1, cb, cl)
            val contract = if (st == E_NOT_FOUND) {
                null
            } else {
                Status.check(st, null)
                Cid.of(cb.copyOf(cl[0].toInt()))
            }
            fun name(field: Long) = Status.checkedText(null) { b, n -> Jni15.aipiFnGetName(rec, field, b, n) }
            val cid = Cid.of(Status.cid(null) { c, n -> Jni15.aipiFnCid(rec, c, n) })
            return FunctionRecord(
                rec, Intent.fromCode(o[0].toInt()), VerifyMode.fromCode(o[1].toInt()), Privacy.fromCode(o[2].toInt()),
                Determinism.fromCode(o[3].toInt()), o[4].toInt(), o[5].toInt(), o[6], module, contract,
                name(0), name(1), name(2), cid,
            )
        }
    }

    /** A copy of the encoded record. */
    val bytes: ByteArray get() = rec.copyOf()

    override fun equals(other: Any?): Boolean = other is FunctionRecord && rec.contentEquals(other.rec)

    override fun hashCode(): Int = rec.contentHashCode()

    override fun toString(): String = "FunctionRecord[${cid.toText(CidBase.BASE58BTC)} $intent $entry]"

    /** Builds a [FunctionRecord]. Defaults: FN, LOCAL verification, PUBLIC, DETERMINISTIC. */
    class Builder internal constructor() {
        private var intent = Intent.FN
        private var verify = VerifyMode.LOCAL
        private var privacy = Privacy.PUBLIC
        private var determinism = Determinism.DETERMINISTIC
        private var quorumN = 0
        private var quorumK = 0
        private var fuelMax = 0L
        private var module: Cid? = null
        private var contract: Cid? = null
        private var entry: String? = null
        private var checker: String? = null
        private var certifier: String? = null

        fun intent(v: Intent) = apply { intent = v }
        fun module(v: Cid) = apply { module = v }
        fun contract(v: Cid?) = apply { contract = v }
        fun entry(v: String) = apply { entry = v }
        fun fuelMax(v: Long) = apply { fuelMax = v }
        fun privacy(v: Privacy) = apply { privacy = v }
        fun determinism(v: Determinism) = apply { determinism = v }

        /** Verified by running it locally. */
        fun verifyLocal() = mode(VerifyMode.LOCAL, null, null, 0, 0)

        /** Verified by a checker export. */
        fun verifyByChecker(checkerExport: String) = mode(VerifyMode.CHECKER, checkerExport, null, 0, 0)

        /** Verified by a certificate and its checker. */
        fun verifyByCertificate(checkerExport: String, certifierExport: String) =
            mode(VerifyMode.CERT, checkerExport, certifierExport, 0, 0)

        /** Verified by `k` of `n` executors agreeing. */
        fun verifyByQuorum(n: Int, k: Int) = mode(VerifyMode.QUORUM, null, null, n, k)

        private fun mode(m: VerifyMode, ch: String?, ce: String?, n: Int, k: Int) = apply {
            verify = m
            checker = ch
            certifier = ce
            quorumN = n
            quorumK = k
        }

        /** Encodes the record. */
        fun build(): FunctionRecord {
            val m = module ?: throw Status.usage(E_ARG, "module is required")
            val e = entry ?: throw Status.usage(E_ARG, "entry is required")
            return encode(intent, verify, privacy, determinism, quorumN, quorumK, fuelMax, m, contract, e, checker, certifier)
        }
    }
}

/** A call record: a function CID and its input (at most 1024 bytes). A value. Pure. */
class CallRecord private constructor(private val rec: ByteArray, val function: Cid, private val inp: ByteArray, val cid: Cid) {
    companion object {
        /** The largest input. */
        const val INPUT_MAX = 1024

        /** The longest record. */
        const val MAX_BYTES = 1112

        /** Encodes a call of `function` with `input`. */
        @JvmStatic
        fun encode(function: Cid, input: ByteArray): CallRecord {
            val f = function.bytes
            return decode(Status.bytes(null) { b, n -> Jni15.aipiCallEncode(f, input, b, n) })
        }

        /** The CID of that call record, without building it. */
        @JvmStatic
        fun cidOf(function: Cid, input: ByteArray): Cid {
            val f = function.bytes
            return Cid.of(Status.cid(null) { c, n -> Jni15.aipiCallCid(f, input, c, n) })
        }

        /** Decodes and checks a record. */
        @JvmStatic
        fun decode(record: ByteArray): CallRecord {
            val rec = record.copyOf()
            val o = LongArray(4)
            Status.check(Jni15.aipiCallDecode(rec, o), null)
            val fn = Cid.of(slice(rec, o[0], o[1]))
            val input = slice(rec, o[2], o[3])
            return CallRecord(rec, fn, input, cidOf(fn, input))
        }
    }

    /** A copy of the encoded record. */
    val bytes: ByteArray get() = rec.copyOf()

    /** A copy of the input. */
    val input: ByteArray get() = inp.copyOf()

    override fun equals(other: Any?): Boolean = other is CallRecord && rec.contentEquals(other.rec)

    override fun hashCode(): Int = rec.contentHashCode()

    override fun toString(): String = "CallRecord[$function, ${inp.size} input bytes]"
}

/** A claim record: a call CID, the 32-byte executor id, the output and a certificate. A value. Pure. */
class ClaimRecord private constructor(
    private val rec: ByteArray,
    val call: Cid,
    private val ex: ByteArray,
    private val out: ByteArray,
    private val cert: ByteArray,
) {
    companion object {
        /** The largest output. */
        const val OUTPUT_MAX = 1024

        /** The largest certificate. */
        const val CERT_MAX = 1024

        /** The longest record. */
        const val MAX_BYTES = 2170

        /** Encodes a claim. */
        @JvmStatic
        fun encode(call: Cid, executor: ByteArray, output: ByteArray, certificate: ByteArray): ClaimRecord {
            requireLength(executor, 32, "executor")
            val c = call.bytes
            return decode(Status.bytes(null) { b, n -> Jni15.aipiClaimEncode(c, executor, output, certificate, b, n) })
        }

        /** Decodes and checks a record. */
        @JvmStatic
        fun decode(record: ByteArray): ClaimRecord {
            val rec = record.copyOf()
            val o = LongArray(7)
            Status.check(Jni15.aipiClaimDecode(rec, o), null)
            return ClaimRecord(
                rec, Cid.of(slice(rec, o[0], o[1])), slice(rec, o[2], 32), slice(rec, o[3], o[4]), slice(rec, o[5], o[6]),
            )
        }
    }

    /** A copy of the encoded record. */
    val bytes: ByteArray get() = rec.copyOf()

    /** A copy of the executor id. */
    val executor: ByteArray get() = ex.copyOf()

    /** A copy of the output. */
    val output: ByteArray get() = out.copyOf()

    /** A copy of the certificate. */
    val certificate: ByteArray get() = cert.copyOf()

    override fun equals(other: Any?): Boolean = other is ClaimRecord && rec.contentEquals(other.rec)

    override fun hashCode(): Int = rec.contentHashCode()

    override fun toString(): String = "ClaimRecord[$call, ${out.size} output bytes]"
}

/**
 * An AIPI node: a bounded content-addressed block store that checks every
 * block against its CID on the way out, publishes function records, and
 * plans where a call would run. Putting and publishing need `AIPI` and pass
 * [capability]; reads need none but fail closed once the context halted.
 *
 * The node belongs to the context (closed with it, ABI R6). Its optional
 * 32-byte node key is a [SecretKey], so it never sits on the Java heap.
 */
class AipiNode private constructor(
    /** The capability puts and publishes pass. */
    val capability: Capability,
    handle: Long,
) : Closeable {
    private val st get() = capability.context.state
    private val res =
        NativeResource("AIPI node", handle, Jni15::aipiDestroy, capability.context.state, capability.context.res)

    init {
        val r = res
        Backstop.register(this) { r.close() }
    }

    /** Block count, store bytes used, verified gets and forbidden (private) refusals. */
    data class Info(val blocks: Long, val storeUsed: Long, val getsVerified: Long, val forbidden: Long)

    companion object {
        /** The smallest store. */
        const val STORE_MIN: Long = 4096

        /** The largest store. */
        const val STORE_MAX: Long = 268_435_456L

        /** The most blocks. */
        const val BLOCKS_MAX: Long = 65536

        /**
         * A node with `storeBytes` of block store and at most `maxBlocks`
         * blocks. `nodeKey` (32 bytes, or null) keys private blocks. Needs `AIPI`.
         */
        @JvmStatic
        @JvmOverloads
        fun create(cap: Capability, storeBytes: Long, maxBlocks: Long, nodeKey: SecretKey? = null): AipiNode {
            if (storeBytes < 0) throw Status.usage(E_ARG, "storeBytes is negative")
            requireU32(maxBlocks, "maxBlocks")
            if (nodeKey != null && nodeKey.size != 32) throw Status.usage(E_ARG, "a node key is 32 bytes, got ${nodeKey.size}")
            return cap.res.use { h ->
                val o = LongArray(1)
                val rc = if (nodeKey == null) {
                    Jni15.aipiCreate(h, storeBytes, maxBlocks, 0L, o)
                } else {
                    nodeKey.withPointer { k -> Jni15.aipiCreate(h, storeBytes, maxBlocks, k, o) }
                }
                Status.check(rc, cap.context.state)
                AipiNode(cap, o[0])
            }
        }
    }

    /** Stores a public block; returns its CID. */
    fun put(data: ByteArray): Cid = put(data, false)

    /** Stores a private block (never served to the mesh); returns its CID. */
    fun putPrivate(data: ByteArray): Cid = put(data, true)

    private fun put(data: ByteArray, priv: Boolean): Cid = capability.res.use { c ->
        res.use { h -> Cid.of(Status.cid(st) { b, n -> Jni15.aipiPut(c, h, data, if (priv) 1 else 0, b, n) }) }
    }

    /** The block for `cid`, checked against it. */
    fun get(cid: Cid): ByteArray {
        val c = cid.bytes
        return res.use { h -> Status.bytes(st) { b, n -> Jni15.aipiGet(h, c, b, n) } }
    }

    /** Publishes a function record; returns its CID. */
    fun publish(record: FunctionRecord): Cid {
        val r = record.bytes
        return capability.res.use { c -> res.use { h -> Cid.of(Status.cid(st) { b, n -> Jni15.aipiPublishFn(c, h, r, b, n) }) } }
    }

    /** The published function record for `fn`. */
    fun loadFunction(fn: Cid): FunctionRecord {
        val c = fn.bytes
        return FunctionRecord.decode(res.use { h -> Status.bytes(st) { b, n -> Jni15.aipiLoadFn(h, c, b, n) } })
    }

    /** Where a call of `fn` with `input` would run. */
    @JvmOverloads
    fun plan(fn: Cid, input: ByteArray?, inputPrivate: Boolean = false): Route {
        val c = fn.bytes
        return res.use { h ->
            val o = IntArray(1)
            Status.check(Jni15.aipiPlan(h, c, input, if (inputPrivate) 1 else 0, o), st)
            Route.fromCode(o[0])
        }
    }

    /** Counters. */
    fun info(): Info = res.use { h ->
        val o = LongArray(4)
        Status.check(Jni15.aipiInfo(h, o), st)
        Info(o[0], o[1], o[2], o[3])
    }

    /** True once closed. */
    val isClosed: Boolean get() = res.isClosed

    /** Frees the node. Idempotent. */
    override fun close() = res.close()
}
