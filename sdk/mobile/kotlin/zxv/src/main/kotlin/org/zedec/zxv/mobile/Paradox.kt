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
import org.zedec.zxv.mobile.internal.requireIndex
import org.zedec.zxv.mobile.internal.requireLength
import org.zedec.zxv.mobile.internal.requireU32
import org.zedec.zxv.mobile.internal.requireU8

/** The kind of a paradox (`ZXV_PDX_*`). */
enum class ParadoxKind(val code: Int) {
    CONSTRUCTIVE(0), DESTRUCTIVE(1), NEUTRAL(2), SPECIAL(3), NONE(4);

    companion object {
        @JvmStatic
        fun fromCode(code: Int): ParadoxKind = entries.firstOrNull { it.code == code } ?: unknown("ParadoxKind", code)
    }
}

/** What to do with a paradox. */
enum class ParadoxAction(val code: Int) {
    NONE(0), SYNTHESIZE(1), CONTAIN(2), HOLD(3), RECORD(4), ROLLBACK(5);

    companion object {
        @JvmStatic
        fun fromCode(code: Int): ParadoxAction = entries.firstOrNull { it.code == code } ?: unknown("ParadoxAction", code)
    }
}

/** An operator's outcome. */
enum class Outcome(val code: Int) {
    APPLIED(0), KEPT(1), PROPOSAL_FOR_OWNER(2), ROLLED_BACK(3), CONTAINED(4), DEFERRED(5), SETTLED(6), RECORDED(7),
    ACCEPTED(8), PENDING(9), RESOLVED(10), ESCALATE_TO_OWNER(11), REFUSED(12), LOG_FULL(13);

    companion object {
        @JvmStatic
        fun fromCode(code: Int): Outcome = entries.firstOrNull { it.code == code } ?: unknown("Outcome", code)
    }
}

/** A graph node's state. */
enum class NodeState(val code: Int) {
    OPEN(0), CLEAR(1), CONTAINED(2), ROLLED_BACK(3), TAINTED(4), SANDBOXED(5), WAITING(6), HELD(7), BLOCKED(8), APPLIED(9);

    companion object {
        @JvmStatic
        fun fromCode(code: Int): NodeState = entries.firstOrNull { it.code == code } ?: unknown("NodeState", code)
    }
}

/** The book's three hash-chained logs. */
enum class ParadoxLog(val code: Int) {
    OPS(0), FRONTIER(1), QUARANTINE(2);

    companion object {
        @JvmStatic
        fun fromCode(code: Int): ParadoxLog = entries.firstOrNull { it.code == code } ?: unknown("ParadoxLog", code)
    }
}

private fun unknown(what: String, code: Int): Nothing = throw Status.usage(E_ARG, "unknown $what $code")

/** Registry flags: entries with these need the owner's approval. */
object ParadoxFlag {
    const val MONEY = 1
    const val DEVICE = 2
}

/** The trip bits of a destructive detection. */
object Trip {
    const val STEPS = 1
    const val BUDGET = 2
    const val CYCLE = 4
    const val INVARIANT = 8
    const val FORBIDDEN = 16
}

/** Evidence for a classification: positive and negative counts of two observations (uint32), and staleness. */
data class Evidence(val pos0: Long, val neg0: Long, val pos1: Long, val neg1: Long, val stale: Boolean)

/** A classification: the kind, the (possibly re-tagged) hexagram and the registry index (-1 for none). */
data class Classification(val kind: ParadoxKind, val tag: Hexagram, val registryIndex: Int) {
    /** What to do. */
    fun action(): ParadoxAction = Paradox.action(kind, tag)
}

private fun le32(b: ByteArray, o: Int): Int =
    (b[o].toInt() and 0xFF) or (b[o + 1].toInt() and 0xFF shl 8) or (b[o + 2].toInt() and 0xFF shl 16) or
        (b[o + 3].toInt() and 0xFF shl 24)

private fun le64(b: ByteArray, o: Int): Long = (le32(b, o).toLong() and 0xFFFF_FFFFL) or (le32(b, o + 4).toLong() shl 32)

private fun u8(b: ByteArray, o: Int): Int = b[o].toInt() and 0xFF

private fun sl(b: ByteArray, o: Int, n: Int): ByteArray = b.copyOfRange(o, o + n)

/** One 144-byte operator or frontier log record. */
class LogRecord private constructor(r: ByteArray) {
    val seq: Long = le64(r, 0)
    val op: Int = u8(r, 8)
    val kind: Int = u8(r, 9)
    val tag: Int = u8(r, 10)
    val outcome: Int = u8(r, 11)
    val aux: Int = le32(r, 12)
    private val a0 = sl(r, 16, 32)
    private val b0 = sl(r, 48, 32)
    private val p0 = sl(r, 80, 32)
    private val h0 = sl(r, 112, 32)
    val a: ByteArray get() = a0.copyOf()
    val b: ByteArray get() = b0.copyOf()
    val prev: ByteArray get() = p0.copyOf()
    val hash: ByteArray get() = h0.copyOf()

    companion object {
        const val BYTES = 144

        @JvmStatic
        fun parse(r: ByteArray): LogRecord {
            requireLength(r, BYTES, "record")
            return LogRecord(r)
        }
    }
}

/** One 110-byte quarantine log record. */
class QuarantineRecord private constructor(r: ByteArray) {
    val seq: Long = le64(r, 0)
    val tag: Int = u8(r, 8)
    val reason: Int = u8(r, 9)
    val submitter: Int = le32(r, 10)
    private val s0 = sl(r, 14, 32)
    private val p0 = sl(r, 46, 32)
    private val h0 = sl(r, 78, 32)
    val subject: ByteArray get() = s0.copyOf()
    val prev: ByteArray get() = p0.copyOf()
    val hash: ByteArray get() = h0.copyOf()

    companion object {
        const val BYTES = 110

        @JvmStatic
        fun parse(r: ByteArray): QuarantineRecord {
            requireLength(r, BYTES, "record")
            return QuarantineRecord(r)
        }
    }
}

/** A 160-byte repair proof from a containment. A value; [verify] checks it. */
class RepairProof private constructor(private val b: ByteArray) {
    companion object {
        const val BYTES = 160

        @JvmStatic
        fun of(bytes: ByteArray): RepairProof {
            requireLength(bytes, BYTES, "proof")
            return RepairProof(bytes.copyOf())
        }
    }

    val bytes: ByteArray get() = b.copyOf()
    val trips: Int get() = le32(b, 0)
    val steps: Int get() = le32(b, 4)
    val spent: Int get() = le32(b, 8)
    val traceLength: Int get() = le32(b, 12)
    val submitter: Int get() = le32(b, 16)
    val stateLength: Int get() = le32(b, 20)
    val stateFoundHash: ByteArray get() = sl(b, 24, 32)
    val stateRestoredHash: ByteArray get() = sl(b, 56, 32)
    val signature: ByteArray get() = sl(b, 88, 32)
    val hash: ByteArray get() = sl(b, 120, 32)
    val quarantineIndex: Long get() = le64(b, 152)

    /** Returns normally if the proof's hash is consistent; [VerificationFailedException] if not. */
    fun verify() = Status.check(Jni15.pdxRepairVerify(b), null)

    /** [verify] as a boolean. */
    fun isValid(): Boolean {
        val st = Jni15.pdxRepairVerify(b)
        if (st != OK && st != -21) Status.check(st, null)
        return st == OK
    }

    override fun equals(other: Any?): Boolean = other is RepairProof && b.contentEquals(other.b)

    override fun hashCode(): Int = b.contentHashCode()

    override fun toString(): String = "RepairProof[trips=$trips, quarantine=$quarantineIndex]"
}

/** A 71-byte draft classification for review. A value. */
class Draft private constructor(private val b: ByteArray) {
    companion object {
        const val BYTES = 71

        @JvmStatic
        fun of(bytes: ByteArray): Draft {
            requireLength(bytes, BYTES, "draft")
            return Draft(bytes.copyOf())
        }
    }

    internal val raw: ByteArray get() = b
    val bytes: ByteArray get() = b.copyOf()
    val record: Int get() = le32(b, 0)
    val recordHash: ByteArray get() = sl(b, 4, 32)
    val signature: ByteArray get() = sl(b, 36, 32)
    val kindCode: Int get() = u8(b, 68)
    val reason: Int get() = u8(b, 69)
    val flags: Int get() = u8(b, 70)

    override fun equals(other: Any?): Boolean = other is Draft && b.contentEquals(other.b)

    override fun hashCode(): Int = b.contentHashCode()

    override fun toString(): String = "Draft[rec=$record, kind=$kindCode, reason=$reason, flags=$flags]"
}

/** The paradox engine's pure functions. */
object Paradox {
    const val HASH_BYTES = 32
    const val LOG_MAX = 65536
    const val REGISTRY_MAX = 65536
    const val POLICY_MAX = 64
    const val HOLD_MAX = 64
    const val GRAPH_MAX = 64
    const val MAX_DEPS = 4
    const val UPDATES_MAX = 256
    const val TRACE_MAX = 4096

    /** A registry entry's source when it came from the seed table. */
    const val SRC_SEED: Long = -1L

    /** The 32-byte paradox signature of `data`. */
    @JvmStatic
    fun signature(data: ByteArray?): ByteArray =
        ByteArray(32).also { Status.check(Jni15.pdxSignature(data ?: ByteArray(0), it), null) }

    /** The kind a hexagram tag implies. */
    @JvmStatic
    fun kindOfTag(tag: Hexagram): ParadoxKind =
        ParadoxKind.fromCode(IntArray(1).also { Status.check(Jni15.pdxKindOfTag(tag.tag, it), null) }[0])

    /** What to do with a paradox of `kind` tagged `tag`. */
    @JvmStatic
    fun action(kind: ParadoxKind, tag: Hexagram): ParadoxAction =
        ParadoxAction.fromCode(IntArray(1).also { Status.check(Jni15.pdxAction(kind.code, tag.tag, it), null) }[0])

    /** The hexagram action an outcome maps to. */
    @JvmStatic
    fun outcomeAction(outcome: Outcome): HexAction =
        HexAction.fromCode(IntArray(1).also { Status.check(Jni15.pdxOutcomeAction(outcome.code, it), null) }[0])

    /** libzxv's name for a kind. */
    @JvmStatic
    fun name(kind: ParadoxKind): String = Jni15.pdxKindName(kind.code)

    /** libzxv's name for an outcome. */
    @JvmStatic
    fun name(outcome: Outcome): String = Jni15.pdxOutcomeName(outcome.code)

    /** libzxv's name for a node state. */
    @JvmStatic
    fun name(state: NodeState): String = Jni15.pdxNodeStateName(state.code)

    internal fun classify(book: Long, tag: Hexagram, evidence: Evidence?, signature: ByteArray?, st: org.zedec.zxv.mobile.internal.ContextState?): Classification {
        signature?.let { requireLength(it, 32, "signature") }
        val ev = evidence?.let {
            longArrayOf(it.pos0, it.neg0, it.pos1, it.neg1).onEach { v -> requireU32(v, "evidence") }
        }
        val o = LongArray(3)
        Status.check(Jni15.pdxClassify(book, tag.tag, ev, if (evidence?.stale == true) 1 else 0, signature, o), st)
        return Classification(ParadoxKind.fromCode(o[0].toInt()), Hexagram.fromTag(o[1].toInt()), o[2].toInt())
    }
}

/**
 * A paradox book: the registry of known paradox signatures and the three
 * hash-chained logs (operators, frontier, quarantine) every operator writes.
 * Writing a log or the registry, applying a policy update or creating a
 * handle needs `PARADOX` and passes [capability]; reading and classifying
 * need none. Nothing here signs. The book belongs to the context and is
 * internally serialized; a full log is `ZXV_E_CAPACITY` (halt the context).
 */
class ParadoxBook private constructor(
    /** The capability every write passes. */
    val capability: Capability,
    handle: Long,
) : Closeable {
    private val st get() = capability.context.state
    private val res = NativeResource("paradox book", handle, Jni15::pdxDestroy, capability.context.state, capability.context.res)

    init {
        val r = res
        Backstop.register(this) { r.close() }
    }

    /** One registry entry; `source` is [Paradox.SRC_SEED] for seed entries, else the ops record that added it. */
    class RegistryEntry(signature: ByteArray, val kind: ParadoxKind, val reason: Int, val flags: Int, val source: Long) {
        private val s = signature.copyOf()
        val signature: ByteArray get() = s.copyOf()
    }

    /** What a destructive detection saw. All values are uint32 in a `Long`. */
    class Detection(
        val steps: Long,
        val stepLimit: Long,
        val spent: Long,
        val budget: Long,
        trace: LongArray?,
        val invariantOk: Boolean,
    ) {
        private val t = trace?.copyOf()
        val trace: LongArray? get() = t?.copyOf()
    }

    /** A containment: the outcome, the state after it, and the repair proof when CONTAINED. */
    class Containment(val outcome: Outcome, state: ByteArray, val proof: RepairProof?) {
        private val s = state.copyOf()
        val state: ByteArray get() = s.copyOf()
    }

    /** A neutral operator's outcome and the settled branch (if any). */
    data class Hold(val outcome: Outcome, val settled: Long)

    /** A special operator's outcome and the frontier record (UINT32_MAX when none). */
    data class Frontier(val outcome: Outcome, val record: Long)

    companion object {
        /** A book with logs of `logCapacity` records and a registry of `registryCapacity` entries. Needs `PARADOX`. */
        @JvmStatic
        fun create(cap: Capability, logCapacity: Long, registryCapacity: Long): ParadoxBook {
            requireU32(logCapacity, "logCapacity")
            requireU32(registryCapacity, "registryCapacity")
            return cap.res.use { h ->
                val o = LongArray(1)
                Status.check(Jni15.pdxCreate(h, logCapacity, registryCapacity, o), cap.context.state)
                ParadoxBook(cap, o[0])
            }
        }

        /** Classifies without a book (no registry lookup). Pure. */
        @JvmStatic
        fun classifyTag(tag: Hexagram, evidence: Evidence?): Classification = Paradox.classify(0L, tag, evidence, null, null)
    }

    private inline fun <T> gated(f: (Long, Long) -> T): T = capability.res.use { c -> res.use { h -> f(c, h) } }

    /** Classifies `tag` with optional evidence and a registry lookup of `signature`. */
    fun classify(tag: Hexagram, evidence: Evidence?, signature: ByteArray?): Classification =
        res.use { h -> Paradox.classify(h, tag, evidence, signature, st) }

    /**
     * Adds a registry entry; a MONEY or DEVICE flag needs `ownerOk`
     * ([OwnerApprovalRequiredException] otherwise). Returns its index. Needs `PARADOX`.
     */
    fun registryAdd(signature: ByteArray, kind: ParadoxKind, reason: Int, flags: Int, ownerOk: Boolean): Int {
        requireLength(signature, 32, "signature")
        requireU8(reason, "reason")
        requireU8(flags, "flags")
        return gated { c, h ->
            val o = LongArray(1)
            Status.check(Jni15.pdxRegistryAdd(c, h, signature, kind.code, reason, flags, if (ownerOk) 1 else 0, o), st)
            o[0].toInt()
        }
    }

    /** The index of `signature` in the registry, or null. */
    fun registryFind(signature: ByteArray): Int? {
        requireLength(signature, 32, "signature")
        return res.use { h ->
            val o = LongArray(1)
            val rc = Jni15.pdxRegistryFind(h, signature, o)
            if (rc == E_NOT_FOUND) {
                null
            } else {
                Status.check(rc, st)
                o[0].toInt()
            }
        }
    }

    /** The number of registry entries. */
    fun registryCount(): Int = res.use { h -> LongArray(1).also { Status.check(Jni15.pdxRegistryCount(h, it), st) }[0].toInt() }

    /** Registry entry `index`. */
    fun registryEntry(index: Int): RegistryEntry {
        val i = requireIndex(index, "index")
        return res.use { h ->
            val s = ByteArray(32)
            val o = LongArray(4)
            Status.check(Jni15.pdxRegistryAt(h, i, s, o), st)
            RegistryEntry(s, ParadoxKind.fromCode(o[0].toInt()), o[1].toInt(), o[2].toInt(), o[3])
        }
    }

    /** The number of records in `log`. */
    fun logCount(log: ParadoxLog): Int =
        res.use { h -> LongArray(1).also { Status.check(Jni15.pdxLogCount(h, log.code, it), st) }[0].toInt() }

    /** The chain head of `log` (zeros when empty). */
    fun logHead(log: ParadoxLog): ByteArray = res.use { h -> ByteArray(32).also { Status.check(Jni15.pdxLogHead(h, log.code, it), st) } }

    /** Record `index` of `log` (144 bytes, or 110 for the quarantine log). */
    fun logRecord(log: ParadoxLog, index: Int): ByteArray {
        val i = requireIndex(index, "index")
        return res.use { h -> Status.bytes(st) { b, n -> Jni15.pdxLogRecord(h, log.code, i, b, n) } }
    }

    /** Re-walks `log`'s chain; a break is FATAL (the context is poisoned). */
    fun logVerify(log: ParadoxLog) {
        res.use { h -> Status.check(Jni15.pdxLogVerify(h, log.code), st) }
    }

    /** The constructive operator: synthesize within `policy`'s bounds (or propose to the owner). Needs `PARADOX`. */
    fun constructive(policy: ParadoxPolicy, kind: ParadoxKind, tag: Hexagram, synth: LongArray?, tension: Long): Outcome {
        requireU32(tension, "tension")
        synchronized(policy) {
            return gated { c, h ->
                policy.res.use { p ->
                    val o = IntArray(1)
                    val n = synth?.size?.toLong() ?: 0L
                    Status.check(Jni15.pdxConstructive(c, h, p, kind.code, tag.tag, synth, n, tension, o), st)
                    Outcome.fromCode(o[0])
                }
            }
        }
    }

    /**
     * The destructive operator: contain a runaway, restoring `state` from
     * `checkpoint` (same length) when contained. Needs `PARADOX`.
     */
    @Suppress("LongParameterList")
    fun destructive(
        kind: ParadoxKind, tag: Hexagram, detection: Detection, state: ByteArray, checkpoint: ByteArray, pattern: ByteArray?,
        submitter: Long,
    ): Containment {
        if (state.size != checkpoint.size) throw Status.usage(E_ARG, "state and checkpoint differ in length")
        val d = longArrayOf(detection.steps, detection.stepLimit, detection.spent, detection.budget, submitter)
        d.forEach { requireU32(it, "detection") }
        val trace = detection.trace?.takeIf { it.isNotEmpty() }
        val s = state.copyOf()
        return gated { c, h ->
            val o = IntArray(1)
            val proof = ByteArray(RepairProof.BYTES)
            Status.check(
                Jni15.pdxDestructive(c, h, kind.code, tag.tag, d, trace, if (detection.invariantOk) 1 else 0, s, checkpoint, pattern, o, proof),
                st,
            )
            val out = Outcome.fromCode(o[0])
            Containment(out, s, if (out == Outcome.CONTAINED) RepairProof.of(proof) else null)
        }
    }

    /** The neutral operator: hold the branches until evidence (or `ownerPick`, -1 for none) settles one. Needs `PARADOX`. */
    fun neutral(hold: ParadoxHold, kind: ParadoxKind, tag: Hexagram, ownerPick: Int): Hold {
        synchronized(hold) {
            return gated { c, h ->
                hold.res.use { hh ->
                    val o = LongArray(2)
                    Status.check(Jni15.pdxNeutral(c, h, hh, kind.code, tag.tag, ownerPick, o), st)
                    Hold(Outcome.fromCode(o[0].toInt()), o[1])
                }
            }
        }
    }

    /** The special operator: record the paradox on the frontier log. Needs `PARADOX`. */
    fun special(kind: ParadoxKind, tag: Hexagram, signature: ByteArray, subject: ByteArray): Frontier {
        requireLength(signature, 32, "signature")
        requireLength(subject, 32, "subject")
        return gated { c, h ->
            val o = LongArray(2)
            Status.check(Jni15.pdxSpecial(c, h, kind.code, tag.tag, signature, subject, o), st)
            Frontier(Outcome.fromCode(o[0].toInt()), o[1])
        }
    }

    /** A draft classification of frontier record `record`. Needs `PARADOX`. */
    fun draft(record: Long, kind: ParadoxKind, reason: Int, flags: Int): Draft {
        requireU32(record, "record")
        requireU8(reason, "reason")
        requireU8(flags, "flags")
        return gated { c, h ->
            val out = ByteArray(Draft.BYTES)
            Status.check(Jni15.pdxDraft(c, h, record, kind.code, reason, flags, out), st)
            Draft.of(out)
        }
    }

    /** Reviews a draft; accepted drafts join the registry. Needs `PARADOX`. */
    fun review(draft: Draft, ownerOk: Boolean, consensusOk: Boolean): Outcome = gated { c, h ->
        val o = IntArray(1)
        Status.check(Jni15.pdxReview(c, h, draft.raw, if (ownerOk) 1 else 0, if (consensusOk) 1 else 0, o), st)
        Outcome.fromCode(o[0])
    }

    /** One pass over `graph`; [StepLimitException] when its step limit is reached. Needs `PARADOX`. */
    fun pass(graph: ParadoxGraph): Outcome {
        synchronized(graph) {
            return gated { c, h ->
                graph.res.use { g ->
                    val o = IntArray(1)
                    Status.check(Jni15.pdxGraphPass(c, h, g, o), st)
                    Outcome.fromCode(o[0])
                }
            }
        }
    }

    /** True once closed. */
    val isClosed: Boolean get() = res.isClosed

    /** Frees the book (and nothing else: policies, holds and graphs are separate handles). Idempotent. */
    override fun close() = res.close()
}

/**
 * A bounded policy the constructive operator may tune: `n` int64 values
 * with low and high bounds. Not thread-safe in C; synchronized here.
 */
class ParadoxPolicy private constructor(cap: Capability, handle: Long, val size: Int) : Closeable {
    private val st = cap.context.state
    internal val res = NativeResource("paradox policy", handle, Jni15::pdxPolicyDestroy, cap.context.state, cap.context.res)

    init {
        val r = res
        Backstop.register(this) { r.close() }
    }

    /** Current and previous values, whether armed, and the tension threshold. */
    class Values(values: LongArray, previous: LongArray, val armed: Boolean, val tensionAt: Long) {
        private val v = values.copyOf()
        private val p = previous.copyOf()
        val values: LongArray get() = v.copyOf()
        val previous: LongArray get() = p.copyOf()
    }

    /** Bounds and flags. */
    class Bounds(lo: LongArray, hi: LongArray, val flags: Long) {
        private val l = lo.copyOf()
        private val h = hi.copyOf()
        val lo: LongArray get() = l.copyOf()
        val hi: LongArray get() = h.copyOf()
    }

    companion object {
        /** A policy. Needs `PARADOX`. */
        @JvmStatic
        fun create(cap: Capability, lo: LongArray, hi: LongArray, initial: LongArray, flags: Long): ParadoxPolicy {
            if (hi.size != lo.size || initial.size != lo.size) throw Status.usage(E_ARG, "lo, hi and initial must have the same length")
            requireU32(flags, "flags")
            return cap.res.use { h ->
                val o = LongArray(1)
                Status.check(Jni15.pdxPolicyCreate(h, lo, hi, initial, flags, o), cap.context.state)
                ParadoxPolicy(cap, o[0], lo.size)
            }
        }
    }

    /** The values. */
    @Synchronized
    fun values(): Values = res.use { h ->
        val v = LongArray(size)
        val p = LongArray(size)
        val o = LongArray(2)
        Status.check(Jni15.pdxPolicyValues(h, v, p, size.toLong(), o), st)
        Values(v, p, o[0] != 0L, o[1])
    }

    /** The bounds. */
    @Synchronized
    fun bounds(): Bounds = res.use { h ->
        val lo = LongArray(size)
        val hi = LongArray(size)
        val o = LongArray(1)
        Status.check(Jni15.pdxPolicyBounds(h, lo, hi, size.toLong(), o), st)
        Bounds(lo, hi, o[0])
    }

    /** The proposal bytes the owner would review for `synth`. */
    @Synchronized
    fun proposal(synth: LongArray): ByteArray = res.use { h -> Status.bytes(st) { b, n -> Jni15.pdxPolicyProposal(h, synth, b, n) } }

    /** True once closed. */
    val isClosed: Boolean get() = res.isClosed

    /** Frees the policy. Idempotent. */
    @Synchronized
    override fun close() = res.close()
}

/** The branches a neutral paradox holds, with evidence per branch. Synchronized. */
class ParadoxHold private constructor(cap: Capability, handle: Long) : Closeable {
    private val st = cap.context.state
    internal val res = NativeResource("paradox hold", handle, Jni15::pdxHoldDestroy, cap.context.state, cap.context.res)

    init {
        val r = res
        Backstop.register(this) { r.close() }
    }

    /** Branch count, settled branch, decisive margin. */
    data class Info(val count: Long, val settled: Long, val decisive: Long)

    /** One branch and its evidence counts. */
    class Branch(id: ByteArray, val positive: Long, val negative: Long) {
        private val i = id.copyOf()
        val id: ByteArray get() = i.copyOf()
    }

    companion object {
        /** A hold of at most `capacity` branches settling at a margin of `decisive`. Needs `PARADOX`. */
        @JvmStatic
        fun create(cap: Capability, capacity: Long, decisive: Long): ParadoxHold {
            requireU32(capacity, "capacity")
            requireU32(decisive, "decisive")
            return cap.res.use { h ->
                val o = LongArray(1)
                Status.check(Jni15.pdxHoldCreate(h, capacity, decisive, o), cap.context.state)
                ParadoxHold(cap, o[0])
            }
        }
    }

    /** Adds a branch (a 32-byte id). */
    @Synchronized
    fun add(id: ByteArray) {
        requireLength(id, 32, "id")
        res.use { h -> Status.check(Jni15.pdxHoldAdd(h, id), st) }
    }

    /** Adds one piece of evidence for or against a branch. */
    @Synchronized
    fun evidence(id: ByteArray, positive: Boolean) {
        requireLength(id, 32, "id")
        res.use { h -> Status.check(Jni15.pdxHoldEvidence(h, id, if (positive) 1 else 0), st) }
    }

    /** Counts. */
    @Synchronized
    fun info(): Info = res.use { h ->
        val o = LongArray(3)
        Status.check(Jni15.pdxHoldInfo(h, o), st)
        Info(o[0], o[1], o[2])
    }

    /** Branch `index`. */
    @Synchronized
    fun branch(index: Int): Branch {
        val i = requireIndex(index, "index")
        return res.use { h ->
            val id = ByteArray(32)
            val o = LongArray(2)
            Status.check(Jni15.pdxHoldBranch(h, i, id, o), st)
            Branch(id, o[0], o[1])
        }
    }

    /** True once closed. */
    val isClosed: Boolean get() = res.isClosed

    /** Frees the hold. Idempotent. */
    @Synchronized
    override fun close() = res.close()
}

/** A dependency graph of paradoxes resolved pass by pass under a step limit. Synchronized. */
class ParadoxGraph private constructor(cap: Capability, handle: Long) : Closeable {
    private val st = cap.context.state
    internal val res = NativeResource("paradox graph", handle, Jni15::pdxGraphDestroy, cap.context.state, cap.context.res)

    init {
        val r = res
        Backstop.register(this) { r.close() }
    }

    /** One node. */
    class Node(id: ByteArray, val kind: ParadoxKind, val state: NodeState) {
        private val i = id.copyOf()
        val id: ByteArray get() = i.copyOf()
    }

    /** Nodes, passes, total magnitude and direction, whether escalated, and the pass chain hash. */
    class Info(val nodes: Long, val passes: Long, val totalMag: Long, val totalDir: Long, val escalated: Boolean, chain: ByteArray) {
        private val c = chain.copyOf()
        val chain: ByteArray get() = c.copyOf()
    }

    companion object {
        /** A graph resolving within `stepLimit` passes. Needs `PARADOX`. */
        @JvmStatic
        fun create(cap: Capability, stepLimit: Long): ParadoxGraph {
            requireU32(stepLimit, "stepLimit")
            return cap.res.use { h ->
                val o = LongArray(1)
                Status.check(Jni15.pdxGraphCreate(h, stepLimit, o), cap.context.state)
                ParadoxGraph(cap, o[0])
            }
        }
    }

    /** Adds a node; `mag` is uint32, `dir` is -1, 0 or +1. */
    @Synchronized
    fun addNode(id: ByteArray, kind: ParadoxKind, tag: Hexagram, mag: Long, dir: Int) =
        node(id, mag, dir) { h -> Jni15.pdxGraphAddNode(h, id, kind.code, tag.tag, mag, dir) }

    /** Makes `id` depend on `dep`. */
    @Synchronized
    fun addDependency(id: ByteArray, dep: ByteArray) {
        requireLength(id, 32, "id")
        requireLength(dep, 32, "dep")
        res.use { h -> Status.check(Jni15.pdxGraphAddDep(h, id, dep), st) }
    }

    /** Updates a node for the next pass. */
    @Synchronized
    fun update(id: ByteArray, kind: ParadoxKind, tag: Hexagram, mag: Long, dir: Int) =
        node(id, mag, dir) { h -> Jni15.pdxGraphUpdate(h, id, kind.code, tag.tag, mag, dir) }

    private inline fun node(id: ByteArray, mag: Long, dir: Int, f: (Long) -> Int) {
        requireLength(id, 32, "id")
        requireU32(mag, "mag")
        if (dir !in -1..1) throw Status.usage(E_ARG, "dir must be -1, 0 or +1")
        res.use { h -> Status.check(f(h), st) }
    }

    /** The state of node `id`. */
    @Synchronized
    fun state(id: ByteArray): NodeState {
        requireLength(id, 32, "id")
        return res.use { h -> NodeState.fromCode(IntArray(1).also { Status.check(Jni15.pdxGraphState(h, id, it), st) }[0]) }
    }

    /** Node `index`. */
    @Synchronized
    fun node(index: Int): Node {
        val i = requireIndex(index, "index")
        return res.use { h ->
            val id = ByteArray(32)
            val o = LongArray(2)
            Status.check(Jni15.pdxGraphNodeAt(h, i, id, o), st)
            Node(id, ParadoxKind.fromCode(o[0].toInt()), NodeState.fromCode(o[1].toInt()))
        }
    }

    /** Totals and the chain hash. */
    @Synchronized
    fun info(): Info = res.use { h ->
        val o = LongArray(5)
        val chain = ByteArray(32)
        Status.check(Jni15.pdxGraphInfo(h, o, chain), st)
        Info(o[0], o[1], o[2], o[3], o[4] != 0L, chain)
    }

    /** True once closed. */
    val isClosed: Boolean get() = res.isClosed

    /** Frees the graph. Idempotent. */
    @Synchronized
    override fun close() = res.close()
}
