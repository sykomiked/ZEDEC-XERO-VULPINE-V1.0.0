// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.mobile

import java.io.Closeable
import org.zedec.zxv.mobile.internal.Backstop
import org.zedec.zxv.mobile.internal.E_ARG
import org.zedec.zxv.mobile.internal.E_NOT_FOUND
import org.zedec.zxv.mobile.internal.Jni15
import org.zedec.zxv.mobile.internal.NativeResource
import org.zedec.zxv.mobile.internal.OK
import org.zedec.zxv.mobile.internal.Status
import org.zedec.zxv.mobile.internal.cstr
import org.zedec.zxv.mobile.internal.requireIndex
import org.zedec.zxv.mobile.internal.requireLength
import org.zedec.zxv.mobile.internal.requireU32

/** Web 4 sizes, and the action digest consent tokens name. Pure. */
object Web4 {
    /** An ML-DSA-65 public key. */
    const val PUBLIC_KEY_BYTES = 1952

    /** An ML-DSA-65 signature. */
    const val SIGNATURE_BYTES = 3309

    /** An agent, peer or human id. */
    const val ID_BYTES = 32
    const val NAME_MAX = 31
    const val MAX_TOOLS = 16
    const val MAX_PAYLOAD = 2048
    const val CARD_MAX = 7259
    const val MANIFEST_MAX = 4349
    const val ENVELOPE_MAX = 8970
    const val DIRECTORY_MAX = 4096
    const val CONSENT_LOG_MAX = 65536

    /** The 32-byte digest of one tool call, which a consent token's `action` names. */
    @JvmStatic
    fun actionDigest(tool: String, from: ByteArray, to: ByteArray, requestId: Long, payload: ByteArray?): ByteArray {
        requireLength(from, ID_BYTES, "from")
        requireLength(to, ID_BYTES, "to")
        val out = ByteArray(32)
        Status.check(Jni15.w4ActionDigest(cstr(tool, "tool"), from, to, requestId, payload ?: ByteArray(0), out), null)
        return out
    }

    internal fun name(b: ByteArray): String {
        val n = b.indexOf(0).let { if (it < 0) b.size else it }
        return String(b, 0, n, Charsets.UTF_8)
    }
}

/** Consent scopes. */
object ConsentScope {
    const val MONEY = 0x01
    const val PII = 0x02
}

/** Tool flags. */
object ToolFlag {
    const val MONEY = 0x01
    const val PII = 0x02
    const val STREAM = 0x04
}

/** One tool in a manifest. Rates and sizes are uint32 values in a `Long`; the price is uint64 bits. */
data class Tool(val name: String, val priceVfv: Long, val ratePerMin: Long, val burst: Long, val flags: Long, val maxInput: Long)

/** An agent card: the agent's public key and name, bound by its node (or itself). A value. */
class AgentCard private constructor(private val b: ByteArray) {
    /** What a verified card says. */
    class Verified(agentId: ByteArray, peerId: ByteArray, val name: String, val boundMs: Long) {
        private val a = agentId.copyOf()
        private val p = peerId.copyOf()
        val agentId: ByteArray get() = a.copyOf()
        val peerId: ByteArray get() = p.copyOf()

        /** True if the agent bound itself (no node). */
        val isSelfBound: Boolean get() = a.contentEquals(p)
    }

    companion object {
        /** A card from its bytes (checked by [verify]). */
        @JvmStatic
        fun of(bytes: ByteArray): AgentCard = AgentCard(bytes.copyOf())

        internal fun wrap(bytes: ByteArray) = AgentCard(bytes)
    }

    /** A copy of the bytes. */
    val bytes: ByteArray get() = b.copyOf()

    /** Checks the binding signature; [VerificationFailedException] if it fails. */
    fun verify(): Verified {
        val id = ByteArray(32)
        val peer = ByteArray(32)
        val name = ByteArray(32)
        val bound = LongArray(1)
        Status.check(Jni15.w4CardVerify(b, id, peer, name, bound), null)
        return Verified(id, peer, Web4.name(name), bound[0])
    }

    override fun equals(other: Any?): Boolean = other is AgentCard && b.contentEquals(other.b)

    override fun hashCode(): Int = b.contentHashCode()

    override fun toString(): String = "AgentCard[${b.size} bytes]"
}

/** The kind of an envelope. */
enum class EnvelopeKind(val code: Int) {
    REQUEST(1), RESPONSE(2), STREAM(3), CANCEL(4), ERROR(5);

    companion object {
        /** The kind for a code. */
        @JvmStatic
        fun fromCode(code: Int): EnvelopeKind =
            entries.firstOrNull { it.code == code } ?: throw Status.usage(E_ARG, "unknown envelope kind $code")
    }
}

/**
 * A Web 4 agent's key handle (ML-DSA-65 in C memory, never returned in any
 * form), which signs its card, manifests and envelopes. Creating it and
 * anything it signs needs `CRYPTO_SIGN`; issuing consent (as a human's key)
 * needs `ATTEST`. Every signing call passes [capability].
 *
 * A seed for a reproducible key is a 32-byte [SecretKey], never a heap
 * array. The handle belongs to the context; it is not thread-safe in C, so
 * its methods are synchronized here.
 */
class AgentKey private constructor(
    /** The capability every signing call passes. */
    val capability: Capability,
    handle: Long,
) : Closeable {
    private val st get() = capability.context.state
    internal val res = NativeResource("web4 agent key", handle, Jni15::w4AgentDestroy, capability.context.state, capability.context.res)
    private val id = ByteArray(32)
    private val peer = ByteArray(32)
    private val pk = ByteArray(Web4.PUBLIC_KEY_BYTES)

    init {
        val r = res
        Backstop.register(this) { r.close() }
        res.use { h -> Status.check(Jni15.w4AgentInfo(h, id, peer, pk), capability.context.state) }
    }

    companion object {
        /**
         * A key: from `seed` (32 bytes) or fresh randomness (null), named
         * `name` (1..31 of [a-z0-9._-]), bound by `node` (or self-bound) at
         * `nowMs`. Needs `CRYPTO_SIGN`.
         */
        @JvmStatic
        fun create(cap: Capability, seed: SecretKey?, name: String, node: AgentKey?, nowMs: Long): AgentKey {
            if (seed != null && seed.size != 32) throw Status.usage(E_ARG, "an agent seed is 32 bytes, got ${seed.size}")
            val n = cstr(name, "name")
            val o = LongArray(1)
            cap.res.use { c ->
                fun call(nh: Long) {
                    val rc = if (seed == null) {
                        Jni15.w4AgentCreate(c, 0L, n, nh, nowMs, o)
                    } else {
                        seed.withPointer { s -> Jni15.w4AgentCreate(c, s, n, nh, nowMs, o) }
                    }
                    Status.check(rc, cap.context.state)
                }
                if (node == null) call(0L) else synchronized(node) { node.res.use { nh -> call(nh) } }
            }
            return AgentKey(cap, o[0])
        }
    }

    /** A copy of the 32-byte agent id. */
    val agentId: ByteArray get() = id.copyOf()

    /** A copy of the 32-byte peer id (the binding node's agent id). */
    val peerId: ByteArray get() = peer.copyOf()

    /** A copy of the ML-DSA-65 public key. */
    val publicKey: ByteArray get() = pk.copyOf()

    /** The signed agent card. */
    @Synchronized
    fun card(): AgentCard = res.use { h -> AgentCard.wrap(Status.bytes(st) { b, n -> Jni15.w4AgentCard(h, b, n) }) }

    /** Seals an envelope to `to`. Needs `CRYPTO_SIGN`. */
    @Synchronized
    @Suppress("LongParameterList")
    fun seal(
        kind: EnvelopeKind, to: ByteArray, requestId: Long, chunk: Long, tool: String, flags: Long, payload: ByteArray?,
        consent: ConsentToken?, nowMs: Long,
    ): Envelope {
        requireLength(to, Web4.ID_BYTES, "to")
        requireU32(chunk, "chunk")
        requireU32(flags, "flags")
        val t = cstr(tool, "tool")
        val u = longArrayOf(requestId, chunk, flags, nowMs)
        val p = payload ?: ByteArray(0)
        val c = consent?.raw
        return capability.res.use { cap ->
            res.use { h -> Envelope.wrap(Status.bytes(st) { b, n -> Jni15.w4EnvSeal(cap, h, kind.code, to, u, t, p, c, b, n) }) }
        }
    }

    /** A REQUEST envelope for `tool`. */
    fun request(to: ByteArray, requestId: Long, tool: String, payload: ByteArray?, nowMs: Long): Envelope =
        seal(EnvelopeKind.REQUEST, to, requestId, 0, tool, 0, payload, null, nowMs)

    /** A reply to `request`. Needs `CRYPTO_SIGN`. */
    @Synchronized
    fun reply(request: Envelope, kind: EnvelopeKind, chunk: Long, flags: Long, payload: ByteArray?, nowMs: Long): Envelope {
        requireU32(chunk, "chunk")
        requireU32(flags, "flags")
        val u = longArrayOf(chunk, flags, nowMs)
        val r = request.raw
        val p = payload ?: ByteArray(0)
        return capability.res.use { cap ->
            res.use { h -> Envelope.wrap(Status.bytes(st) { b, n -> Jni15.w4EnvReply(cap, h, r, kind.code, u, p, b, n) }) }
        }
    }

    /** Signs `manifest` with this key. Needs `CRYPTO_SIGN`. */
    fun sign(manifest: ToolManifest) = manifest.signWith(this)

    @Synchronized
    internal fun signManifest(m: NativeResource) {
        capability.res.use { c -> res.use { h -> m.use { mh -> Status.check(Jni15.w4ManifestSign(c, h, mh), st) } } }
    }

    /**
     * Issues a consent token as this (human's) key for `agentId` to perform
     * `action` (an [Web4.actionDigest]) in `scope`, up to `maxVfv`, valid in
     * [issuedMs, expiresMs). `nonce` is 16 bytes or null for random. Needs `ATTEST`.
     */
    @Synchronized
    @Suppress("LongParameterList")
    fun issueConsent(
        agentId: ByteArray, action: ByteArray, scope: Long, maxVfv: Long, issuedMs: Long, expiresMs: Long, nonce: ByteArray?,
    ): ConsentToken {
        requireLength(agentId, 32, "agentId")
        requireLength(action, 32, "action")
        requireU32(scope, "scope")
        nonce?.let { requireLength(it, 16, "nonce") }
        val out = ByteArray(ConsentToken.BYTES)
        val t = longArrayOf(maxVfv, issuedMs, expiresMs)
        capability.res.use { c ->
            res.use { h -> Status.check(Jni15.w4ConsentIssue(c, h, agentId, action, scope, t, nonce, out), st) }
        }
        return ConsentToken.of(out)
    }

    /** True once closed. */
    val isClosed: Boolean get() = res.isClosed

    /** Wipes and frees the key. Idempotent. */
    override fun close() = res.close()

    override fun toString(): String = "AgentKey[${id.joinToString("") { "%02x".format(it) }}]"
}

/** A directory of verified agent cards, by agent id. Pure; methods are synchronized. */
class AgentDirectory private constructor(handle: Long) : Closeable {
    private val res = NativeResource("agent directory", handle, Jni15::w4DirDestroy, null, null)

    init {
        val r = res
        Backstop.register(this) { r.close() }
    }

    companion object {
        /** A directory of at most `capacity` cards. */
        @JvmStatic
        fun create(capacity: Long): AgentDirectory {
            requireU32(capacity, "capacity")
            val o = LongArray(1)
            Status.check(Jni15.w4DirCreate(capacity, o), null)
            return AgentDirectory(o[0])
        }
    }

    /** Verifies and adds (or replaces) a card. */
    @Synchronized
    fun add(card: AgentCard) {
        val b = card.bytes
        res.use { h -> Status.check(Jni15.w4DirAdd(h, b), null) }
    }

    /** The card for `agentId`, or null. */
    @Synchronized
    fun find(agentId: ByteArray): AgentCard? {
        requireLength(agentId, Web4.ID_BYTES, "agentId")
        return res.use { h ->
            try {
                AgentCard.wrap(Status.bytes(null) { b, n -> Jni15.w4DirFind(h, agentId, b, n) })
            } catch (e: ZxvException) {
                if (e.status == E_NOT_FOUND) null else throw e
            }
        }
    }

    /** The number of cards. */
    @Synchronized
    fun count(): Long = res.use { h -> LongArray(1).also { Status.check(Jni15.w4DirCount(h, it), null) }[0] }

    /** True once closed. */
    val isClosed: Boolean get() = res.isClosed

    /** Frees the directory. Idempotent. */
    @Synchronized
    override fun close() = res.close()
}

/** A consent token (3453 bytes), signed by a human's key. A value. */
class ConsentToken private constructor(private val b: ByteArray) {
    private val human = ByteArray(32)
    private val agent = ByteArray(32)
    private val act = ByteArray(32)
    private val n = ByteArray(16)
    private val v = LongArray(4)

    init {
        Status.check(Jni15.w4ConsentInfo(b, human, agent, act, v, n), null)
    }

    companion object {
        /** The token size. */
        const val BYTES = 3453

        /** A token from its bytes. */
        @JvmStatic
        fun of(bytes: ByteArray): ConsentToken {
            requireLength(bytes, BYTES, "token")
            return ConsentToken(bytes.copyOf())
        }
    }

    internal val raw: ByteArray get() = b

    val bytes: ByteArray get() = b.copyOf()
    val humanId: ByteArray get() = human.copyOf()
    val agentId: ByteArray get() = agent.copyOf()
    val action: ByteArray get() = act.copyOf()
    val nonce: ByteArray get() = n.copyOf()
    val scope: Long get() = v[0]
    val maxVfv: Long get() = v[1]
    val issuedMs: Long get() = v[2]
    val expiresMs: Long get() = v[3]

    override fun equals(other: Any?): Boolean = other is ConsentToken && b.contentEquals(other.b)

    override fun hashCode(): Int = b.contentHashCode()

    override fun toString(): String = "ConsentToken[scope=$scope, maxVfv=${maxVfv.toULong()}, valid=[$issuedMs, $expiresMs)]"
}

/**
 * A consent log: checks tokens against a human's public key, the agent, the
 * action, the scope needed, the amount and the time, and spends each token
 * once; revoked tokens are refused. Pure (caller state only); synchronized.
 */
class ConsentLog private constructor(handle: Long) : Closeable {
    private val res = NativeResource("consent log", handle, Jni15::w4ConsentLogDestroy, null, null)

    init {
        val r = res
        Backstop.register(this) { r.close() }
    }

    companion object {
        /** A log of at most `capacity` entries. */
        @JvmStatic
        fun create(capacity: Long): ConsentLog {
            requireU32(capacity, "capacity")
            val o = LongArray(1)
            Status.check(Jni15.w4ConsentLogCreate(capacity, o), null)
            return ConsentLog(o[0])
        }
    }

    /** Checks (and spends) `token`; [ConsentException] if it does not allow this action now. */
    @Synchronized
    @Suppress("LongParameterList")
    fun check(token: ConsentToken?, humanPublicKey: ByteArray, agentId: ByteArray, action: ByteArray, need: Long, amount: Long, nowMs: Long) {
        requireLength(humanPublicKey, Web4.PUBLIC_KEY_BYTES, "humanPublicKey")
        requireLength(agentId, Web4.ID_BYTES, "agentId")
        requireLength(action, 32, "action")
        requireU32(need, "need")
        val t = longArrayOf(need, amount, nowMs)
        res.use { h -> Status.check(Jni15.w4ConsentCheck(h, token?.raw, humanPublicKey, agentId, action, t), null) }
    }

    /** Revokes `token` from `nowMs`. */
    @Synchronized
    fun revoke(token: ConsentToken, nowMs: Long) {
        res.use { h -> Status.check(Jni15.w4ConsentRevoke(h, token.raw, nowMs), null) }
    }

    /** Drops entries expired by `nowMs`; returns how many. */
    @Synchronized
    fun prune(nowMs: Long): Long = res.use { h -> LongArray(1).also { Status.check(Jni15.w4ConsentLogPrune(h, nowMs, it), null) }[0] }

    /** True once closed. */
    val isClosed: Boolean get() = res.isClosed

    /** Frees the log. Idempotent. */
    @Synchronized
    override fun close() = res.close()
}

/** A signed envelope. A value; [verify] checks the signature. */
class Envelope private constructor(private val b: ByteArray) {
    private val v = LongArray(7)
    private val f = ByteArray(32)
    private val t = ByteArray(32)
    private val tl = ByteArray(32)

    init {
        Status.check(Jni15.w4EnvDecode(b, v, f, t, tl), null)
    }

    companion object {
        const val FLAG_FINAL = 0x01
        const val FLAG_CONSENT = 0x02

        /** An envelope from its bytes. */
        @JvmStatic
        fun of(bytes: ByteArray): Envelope = Envelope(bytes.copyOf())

        internal fun wrap(bytes: ByteArray) = Envelope(bytes)
    }

    internal val raw: ByteArray get() = b

    val bytes: ByteArray get() = b.copyOf()
    val kind: EnvelopeKind get() = EnvelopeKind.fromCode(v[0].toInt())
    val flags: Long get() = v[1]
    val isFinal: Boolean get() = v[1] and FLAG_FINAL.toLong() != 0L
    val sequence: Long get() = v[2]
    val timestampMs: Long get() = v[3]
    val requestId: Long get() = v[4]
    val chunk: Long get() = v[5]
    val payloadLength: Int get() = v[6].toInt()
    val from: ByteArray get() = f.copyOf()
    val to: ByteArray get() = t.copyOf()
    val tool: String get() = Web4.name(tl)

    /** The payload. */
    fun payload(): ByteArray = Status.bytes(null) { buf, n -> Jni15.w4EnvPayload(b, buf, n) }

    /** The consent token it carries, or null. */
    fun consent(): ConsentToken? {
        val out = ByteArray(ConsentToken.BYTES)
        val st = Jni15.w4EnvConsent(b, out)
        if (st == E_NOT_FOUND) return null
        Status.check(st, null)
        return ConsentToken.of(out)
    }

    /** Returns normally if the sender's signature is valid; [VerificationFailedException] if not. */
    fun verify(senderPublicKey: ByteArray) {
        requireLength(senderPublicKey, Web4.PUBLIC_KEY_BYTES, "senderPublicKey")
        Status.check(Jni15.w4EnvVerify(b, senderPublicKey), null)
    }

    /** [verify] as a boolean. */
    fun isValid(senderPublicKey: ByteArray?): Boolean =
        senderPublicKey != null && senderPublicKey.size == Web4.PUBLIC_KEY_BYTES && Jni15.w4EnvVerify(b, senderPublicKey) == OK

    override fun equals(other: Any?): Boolean = other is Envelope && b.contentEquals(other.b)

    override fun hashCode(): Int = b.contentHashCode()

    override fun toString(): String = "Envelope[$kind, tool=$tool, req=${requestId.toULong()}, seq=${sequence.toULong()}]"
}

/**
 * A tool manifest: an agent's priced, rate-limited tools, signed by its key.
 * Pure (signing goes through [AgentKey.sign]); synchronized.
 */
class ToolManifest private constructor(handle: Long) : Closeable {
    private val res = NativeResource("tool manifest", handle, Jni15::w4ManifestDestroy, null, null)

    init {
        val r = res
        Backstop.register(this) { r.close() }
    }

    /** Ids, version, validity, tool count and whether it is signed. */
    class Info(agentId: ByteArray, peerId: ByteArray, val version: Long, val issuedMs: Long, val expiresMs: Long, val toolCount: Int, val signed: Boolean) {
        private val a = agentId.copyOf()
        private val p = peerId.copyOf()
        val agentId: ByteArray get() = a.copyOf()
        val peerId: ByteArray get() = p.copyOf()
    }

    companion object {
        /** An empty, unsigned manifest. */
        @JvmStatic
        fun create(version: Long, issuedMs: Long, expiresMs: Long): ToolManifest {
            requireU32(version, "version")
            val o = LongArray(1)
            Status.check(Jni15.w4ManifestCreate(version, issuedMs, expiresMs, o), null)
            return ToolManifest(o[0])
        }

        /** A manifest from its encoding. */
        @JvmStatic
        fun decode(bytes: ByteArray): ToolManifest {
            val o = LongArray(1)
            Status.check(Jni15.w4ManifestDecode(bytes, o), null)
            return ToolManifest(o[0])
        }
    }

    /** Adds a tool. */
    @Synchronized
    fun addTool(tool: Tool): ToolManifest {
        val n = cstr(tool.name, "tool name")
        val u = longArrayOf(tool.ratePerMin, tool.burst, tool.flags, tool.maxInput)
        res.use { h -> Status.check(Jni15.w4ManifestAddTool(h, n, tool.priceVfv, u), null) }
        return this
    }

    @Synchronized
    internal fun signWith(key: AgentKey) = key.signManifest(res)

    /** Checks the signature against `publicKey` and the validity at `nowMs`. */
    @Synchronized
    fun verify(publicKey: ByteArray, nowMs: Long) {
        requireLength(publicKey, Web4.PUBLIC_KEY_BYTES, "publicKey")
        res.use { h -> Status.check(Jni15.w4ManifestVerify(h, publicKey, nowMs), null) }
    }

    /** The encoding. */
    @Synchronized
    fun encode(): ByteArray = res.use { h -> Status.bytes(null) { b, n -> Jni15.w4ManifestEncode(h, b, n) } }

    /** Canonical JSON. */
    @Synchronized
    fun toJson(): String = res.use { h -> Status.checkedText(null) { b, n -> Jni15.w4ManifestToJson(h, b, n) } }

    /** Ids, version, validity, tools, signed. */
    @Synchronized
    fun info(): Info = res.use { h ->
        val id = ByteArray(32)
        val peer = ByteArray(32)
        val o = LongArray(5)
        Status.check(Jni15.w4ManifestInfo(h, id, peer, o), null)
        Info(id, peer, o[0], o[1], o[2], o[3].toInt(), o[4] != 0L)
    }

    /** Tool `index`. */
    @Synchronized
    fun tool(index: Int): Tool {
        val i = requireIndex(index, "index")
        return res.use { h ->
            val name = ByteArray(32)
            val o = LongArray(5)
            Status.check(Jni15.w4ManifestTool(h, i, name, o), null)
            Tool(Web4.name(name), o[0], o[1], o[2], o[3], o[4])
        }
    }

    /** Every tool. */
    @Synchronized
    fun tools(): List<Tool> = List(info().toolCount) { tool(it) }

    /** The index of the tool named `name`, or null. */
    @Synchronized
    fun find(name: String): Int? {
        val n = cstr(name, "name")
        return res.use { h ->
            val o = LongArray(1)
            val st = Jni15.w4ManifestFind(h, n, o)
            if (st == E_NOT_FOUND) {
                null
            } else {
                Status.check(st, null)
                o[0].toInt()
            }
        }
    }

    /** True once closed. */
    val isClosed: Boolean get() = res.isClosed

    /** Frees the manifest. Idempotent. */
    @Synchronized
    override fun close() = res.close()
}
