// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.paradox;

import static java.lang.foreign.ValueLayout.JAVA_BYTE;
import static java.lang.foreign.ValueLayout.JAVA_INT;
import static java.lang.foreign.ValueLayout.JAVA_LONG;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.util.OptionalInt;
import org.zedec.zxv.Capability;
import org.zedec.zxv.Hexagram;
import org.zedec.zxv.ZxvContext;
import org.zedec.zxv.ZxvStatus;
import org.zedec.zxv.internal.Buf;
import org.zedec.zxv.internal.Caps;
import org.zedec.zxv.internal.Errors;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Lease;
import org.zedec.zxv.internal.Native15;
import org.zedec.zxv.internal.NativeResource;

/**
 * The paradox book ({@code zxv_paradox}): the operator log, the frontier log
 * (special records, never edited), the quarantine log and the signature
 * registry, plus the operators that write them. All three logs are
 * append-only and SHA-256 hash-chained.
 *
 * <p>OUTCOMES. An operator that ran returns its {@link Outcome} and has
 * appended one operator record; REFUSED and PROPOSAL_FOR_OWNER are
 * decisions, not errors. An exception means it did not run: nothing was
 * logged and nothing changed (two exceptions: {@link #destructive} restores
 * the state even when the logs are full, and {@link #pass} logs the pass
 * that reached the step limit before throwing {@link StepLimitException}).
 * A full log is ZXV_E_CAPACITY: halt the context, since work that cannot be
 * recorded must not continue.
 *
 * <p>CAPABILITIES. The book keeps the capability it was created with
 * ({@code PARADOX}) and uses it for every write. Reading and classifying
 * need none. Nothing here signs: sign proofs and drafts yourself.
 *
 * <p>Threading: libzxv serializes the book, so any thread may call. An
 * operator that uses a policy, hold or graph synchronizes on that handle.
 * See {@link Paradox} for the honest limits.
 */
public final class ParadoxBook implements AutoCloseable {
    /**
     * A registry entry.
     *
     * @param signature the signature
     * @param kind      its kind
     * @param reason    the reason code
     * @param flags     the {@link ParadoxFlag} bits
     * @param source    the frontier record it was learned from, or {@link Paradox#SRC_SEED}
     */
    public record RegistryEntry(byte[] signature, ParadoxKind kind, int reason, int flags, long source) {}

    /**
     * The detection inputs of {@link #destructive}.
     *
     * @param steps       steps taken (uint32 bits)
     * @param stepLimit   the step limit (0: none)
     * @param spent       budget spent (uint32 bits)
     * @param budget      the budget (0: none)
     * @param trace       state fingerprints in visit order (at most {@link Paradox#TRACE_MAX}), or null
     * @param invariantOk the caller's invariant check result
     */
    public record Detection(int steps, int stepLimit, int spent, int budget, long[] trace, boolean invariantOk) {}

    /**
     * The result of {@link #destructive}.
     *
     * @param outcome CONTAINED or REFUSED
     * @param state   the state afterwards (restored from the checkpoint when contained)
     * @param proof   the RepairProof when contained, else null
     */
    public record Containment(Outcome outcome, byte[] state, RepairProof proof) {}

    /**
     * The result of {@link #neutral}.
     *
     * @param outcome SETTLED, DEFERRED, ROLLED_BACK or REFUSED
     * @param settled the settled branch, or -1
     */
    public record Hold(Outcome outcome, int settled) {}

    /**
     * The result of {@link #special}.
     *
     * @param outcome RECORDED, REFUSED or ROLLED_BACK
     * @param record  the frontier index when recorded, else -1 (UINT32_MAX)
     */
    public record Frontier(Outcome outcome, int record) {}

    private final Capability cap;
    private final ZxvContext ctx;
    private final NativeResource res;

    private ParadoxBook(Capability cap, ZxvContext ctx, MemorySegment h) {
        this.cap = cap;
        this.ctx = ctx;
        this.res = new NativeResource("paradox book", h, x -> Ffi.callVoid(Native15.paradox_destroy, x), ctx,
                Caps.res(ctx));
    }

    /**
     * A book with three empty logs and an empty registry.
     *
     * @param cap              a capability with {@code PARADOX}; every write uses it
     * @param logCapacity      records per log, 1..{@link Paradox#LOG_MAX}
     * @param registryCapacity registry entries, 1..{@link Paradox#REGISTRY_MAX}
     * @throws org.zedec.zxv.CapabilityDeniedException without {@code PARADOX}
     */
    public static ParadoxBook create(Capability cap, int logCapacity, int registryCapacity) {
        ZxvContext ctx = Caps.ctx(cap);
        try (Lease l = Lease.of(Caps.res(cap))) {
            return new ParadoxBook(cap, ctx,
                    Buf.create(ctx, Native15.paradox_create, l.h(0), logCapacity, registryCapacity));
        }
    }

    /**
     * Classifies a tag. A known signature overrides the trend for Both and Neither.
     *
     * @param evidence  the trend, or null (polarity NEUTRAL)
     * @param signature a 32-byte signature to look up, or null
     */
    public Classification classify(Hexagram tag, Evidence evidence, byte[] signature) {
        Buf.nonNull("tag", tag);
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            MemorySegment ev = MemorySegment.NULL;
            if (evidence != null) {
                ev = a.allocate(JAVA_INT, 4);
                ev.setAtIndex(JAVA_INT, 0, evidence.pos0());
                ev.setAtIndex(JAVA_INT, 1, evidence.neg0());
                ev.setAtIndex(JAVA_INT, 2, evidence.pos1());
                ev.setAtIndex(JAVA_INT, 3, evidence.neg1());
            }
            MemorySegment k = a.allocate(JAVA_INT), t = a.allocate(1), r = a.allocate(JAVA_INT);
            Ffi.check(Ffi.call(Native15.paradox_classify, l.h(0), tag.tag(), ev,
                    evidence != null && evidence.stale() ? 1 : 0, Buf.fixedOrNull(a, "signature", signature, 32), k, t, r),
                    ctx);
            return new Classification(ParadoxKind.fromCode(k.get(JAVA_INT, 0)),
                    Hexagram.fromTag(Byte.toUnsignedInt(t.get(JAVA_BYTE, 0))), r.get(JAVA_INT, 0));
        }
    }

    /**
     * Classifies a tag with no book (no registry lookup). Pure.
     *
     * @param evidence the trend, or null
     */
    public static Classification classifyTag(Hexagram tag, Evidence evidence) {
        Buf.nonNull("tag", tag);
        try (Arena a = Arena.ofConfined()) {
            MemorySegment ev = MemorySegment.NULL;
            if (evidence != null) {
                ev = a.allocate(JAVA_INT, 4);
                ev.setAtIndex(JAVA_INT, 0, evidence.pos0());
                ev.setAtIndex(JAVA_INT, 1, evidence.neg0());
                ev.setAtIndex(JAVA_INT, 2, evidence.pos1());
                ev.setAtIndex(JAVA_INT, 3, evidence.neg1());
            }
            MemorySegment k = a.allocate(JAVA_INT), t = a.allocate(1), r = a.allocate(JAVA_INT);
            Ffi.check(Ffi.call(Native15.paradox_classify, MemorySegment.NULL, tag.tag(), ev,
                    evidence != null && evidence.stale() ? 1 : 0, MemorySegment.NULL, k, t, r), null);
            return new Classification(ParadoxKind.fromCode(k.get(JAVA_INT, 0)),
                    Hexagram.fromTag(Byte.toUnsignedInt(t.get(JAVA_BYTE, 0))), r.get(JAVA_INT, 0));
        }
    }

    // ---- registry

    /**
     * Adds a reviewed signature as kind CONSTRUCTIVE, DESTRUCTIVE or NEUTRAL
     * (logged). Append-only: an entry is never edited.
     *
     * @param ownerOk the caller's assertion that the owner approved (needed for MONEY or DEVICE flags)
     * @return the entry's index
     * @throws OwnerApprovalRequiredException for a MONEY or DEVICE entry without {@code ownerOk}
     * @throws org.zedec.zxv.ZxvUsageException ZXV_E_DUPLICATE when already registered
     */
    public int registryAdd(byte[] signature, ParadoxKind kind, int reason, int flags, boolean ownerOk) {
        Buf.nonNull("kind", kind);
        try (Lease l = Lease.of(Caps.res(cap), res); Arena a = Arena.ofConfined()) {
            MemorySegment idx = a.allocate(JAVA_INT);
            Ffi.check(Ffi.call(Native15.paradox_registry_add, l.h(0), l.h(1), Buf.fixed(a, "signature", signature, 32),
                    kind.code(), Buf.u8("reason", reason), Buf.u8("flags", flags), ownerOk ? 1 : 0, idx), ctx);
            return idx.get(JAVA_INT, 0);
        }
    }

    /** The index of a signature, if registered. */
    public OptionalInt registryFind(byte[] signature) {
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            MemorySegment idx = a.allocate(JAVA_INT);
            int st = Ffi.call(Native15.paradox_registry_find, l.h(0), Buf.fixed(a, "signature", signature, 32), idx);
            if (st == ZxvStatus.E_NOT_FOUND) return OptionalInt.empty();
            Ffi.check(st, ctx);
            return OptionalInt.of(idx.get(JAVA_INT, 0));
        }
    }

    /** The number of registry entries. */
    public int registryCount() {
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            MemorySegment n = a.allocate(JAVA_INT);
            Ffi.check(Ffi.call(Native15.paradox_registry_count, l.h(0), n), ctx);
            return n.get(JAVA_INT, 0);
        }
    }

    /** Registry entry {@code index}. */
    public RegistryEntry registryEntry(int index) {
        Buf.index("index", index);
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            MemorySegment s = a.allocate(32), k = a.allocate(JAVA_INT), r = a.allocate(1), f = a.allocate(1),
                    src = a.allocate(JAVA_LONG);
            Ffi.check(Ffi.call(Native15.paradox_registry_at, l.h(0), index, s, k, r, f, src), ctx);
            return new RegistryEntry(s.toArray(JAVA_BYTE), ParadoxKind.fromCode(k.get(JAVA_INT, 0)),
                    Byte.toUnsignedInt(r.get(JAVA_BYTE, 0)), Byte.toUnsignedInt(f.get(JAVA_BYTE, 0)),
                    src.get(JAVA_LONG, 0));
        }
    }

    // ---- logs

    /** The record count of a log. */
    public int logCount(ParadoxLog log) {
        Buf.nonNull("log", log);
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            MemorySegment n = a.allocate(JAVA_INT);
            Ffi.check(Ffi.call(Native15.paradox_log_count, l.h(0), log.code(), n), ctx);
            return n.get(JAVA_INT, 0);
        }
    }

    /** A log's chain head: the last record's hash (32 zeros when empty). */
    public byte[] logHead(ParadoxLog log) {
        Buf.nonNull("log", log);
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            MemorySegment h = a.allocate(32);
            Ffi.check(Ffi.call(Native15.paradox_log_head, l.h(0), log.code(), h), ctx);
            return h.toArray(JAVA_BYTE);
        }
    }

    /**
     * Record {@code index} of a log as bytes ({@link LogRecord#BYTES}, or
     * {@link QuarantineRecord#BYTES} for the quarantine log).
     *
     * @throws org.zedec.zxv.ZxvUsageException ZXV_E_NOT_FOUND past the last record
     */
    public byte[] logRecord(ParadoxLog log, int index) {
        Buf.nonNull("log", log);
        Buf.index("index", index);
        try (Lease l = Lease.of(res)) {
            MemorySegment h = l.h(0);
            int w = log.code();
            return Buf.r4(ctx, LogRecord.BYTES,
                    (buf, n, len) -> Ffi.call(Native15.paradox_log_record, h, w, index, buf, n, len));
        }
    }

    /**
     * Recomputes every link of a log.
     *
     * @throws org.zedec.zxv.ZxvFatalError when a record or the head no longer matches (the context halts)
     */
    public void logVerify(ParadoxLog log) {
        Buf.nonNull("log", log);
        try (Lease l = Lease.of(res)) {
            Ffi.check(Ffi.call(Native15.paradox_log_verify, l.h(0), log.code()), ctx);
        }
    }

    // ---- operators

    /**
     * One evaluation of a constructive policy and, if {@code synth} is not
     * null, one proposed update (the synthesis is the caller's; this checks
     * and applies it). A flagged policy, or any value outside its bounds,
     * gives PROPOSAL_FOR_OWNER and changes nothing.
     *
     * @param synth   the proposed values (the policy's size), or null to evaluate only
     * @param tension the caller's integer measure of the paradox, higher is worse (uint32 bits)
     * @return APPLIED, KEPT, PROPOSAL_FOR_OWNER, ROLLED_BACK or REFUSED
     */
    public Outcome constructive(ParadoxPolicy policy, ParadoxKind kind, Hexagram tag, long[] synth, int tension) {
        Buf.nonNull("policy", policy);
        Buf.nonNull("kind", kind);
        Buf.nonNull("tag", tag);
        synchronized (policy) {
            try (Lease l = Lease.of(Caps.res(cap), res, policy.res); Arena a = Arena.ofConfined()) {
                MemorySegment s = synth == null ? MemorySegment.NULL : ParadoxPolicy.seg(a, synth);
                int n = synth == null ? 0 : synth.length;
                MemorySegment out = a.allocate(JAVA_INT);
                Ffi.check(Ffi.call(Native15.paradox_constructive, l.h(0), l.h(1), l.h(2), kind.code(), tag.tag(), s, n,
                        tension, out), ctx);
                return Outcome.fromCode(out.get(JAVA_INT, 0));
            }
        }
    }

    /**
     * Detects and contains. When the kind is DESTRUCTIVE or any check fires,
     * the work is quarantined, the state is restored from {@code checkpoint}
     * and a RepairProof is returned (outcome CONTAINED); otherwise the
     * refusal is logged and nothing changes. It contains: there is no way to
     * reply to or act against the submitter.
     *
     * @param state      the state as found (not modified; the result carries the state afterwards)
     * @param checkpoint the checkpoint, the same length as {@code state}
     * @param pattern    a canonical description of the trap (its signature goes in the proof), or null
     * @param submitter  who submitted the work (uint32 bits)
     */
    public Containment destructive(ParadoxKind kind, Hexagram tag, Detection detection, byte[] state, byte[] checkpoint,
            byte[] pattern, int submitter) {
        Buf.nonNull("kind", kind);
        Buf.nonNull("tag", tag);
        Buf.nonNull("detection", detection);
        Buf.nonNull("state", state);
        Buf.nonNull("checkpoint", checkpoint);
        if (state.length != checkpoint.length) throw Errors.usage(-1, "state and checkpoint differ in length");
        long[] trace = detection.trace() == null ? new long[0] : detection.trace();
        try (Lease l = Lease.of(Caps.res(cap), res); Arena a = Arena.ofConfined()) {
            MemorySegment st = a.allocate(Math.max(1, state.length));
            MemorySegment.copy(state, 0, st, JAVA_BYTE, 0, state.length);
            MemorySegment tr = trace.length == 0 ? MemorySegment.NULL : ParadoxPolicy.seg(a, trace);
            MemorySegment out = a.allocate(JAVA_INT), proof = a.allocate(RepairProof.BYTES);
            int pl = pattern == null ? 0 : pattern.length;
            int rc = Ffi.call(Native15.paradox_destructive, l.h(0), l.h(1), kind.code(), tag.tag(), detection.steps(),
                    detection.stepLimit(), detection.spent(), detection.budget(), tr, trace.length,
                    detection.invariantOk() ? 1 : 0, state.length == 0 ? MemorySegment.NULL : st,
                    Buf.in(a, checkpoint), state.length, Buf.in(a, pattern), pl, submitter, out, proof);
            Ffi.check(rc, ctx);
            Outcome o = Outcome.fromCode(out.get(JAVA_INT, 0));
            return new Containment(o, Buf.read(st, state.length),
                    o == Outcome.CONTAINED ? RepairProof.of(proof.toArray(JAVA_BYTE)) : null);
        }
    }

    /**
     * Settles a neutral hold on decisive evidence or the owner's pick;
     * otherwise keeps every branch.
     *
     * @param ownerPick a branch index, or -1 for none
     */
    public Hold neutral(ParadoxHold hold, ParadoxKind kind, Hexagram tag, int ownerPick) {
        Buf.nonNull("hold", hold);
        Buf.nonNull("kind", kind);
        Buf.nonNull("tag", tag);
        synchronized (hold) {
            try (Lease l = Lease.of(Caps.res(cap), res, hold.res); Arena a = Arena.ofConfined()) {
                MemorySegment out = a.allocate(JAVA_INT), s = a.allocate(JAVA_INT);
                Ffi.check(Ffi.call(Native15.paradox_neutral, l.h(0), l.h(1), l.h(2), kind.code(), tag.tag(), ownerPick,
                        out, s), ctx);
                return new Hold(Outcome.fromCode(out.get(JAVA_INT, 0)), s.get(JAVA_INT, 0));
            }
        }
    }

    /**
     * Records an unclassified paradox: a permanent frontier record, never
     * edited or erased. The work stays sandboxed (not run) until a reviewed
     * classification exists.
     *
     * @param signature the anomaly's signature (32 bytes)
     * @param subject   the work's digest (32 bytes)
     */
    public Frontier special(ParadoxKind kind, Hexagram tag, byte[] signature, byte[] subject) {
        Buf.nonNull("kind", kind);
        Buf.nonNull("tag", tag);
        try (Lease l = Lease.of(Caps.res(cap), res); Arena a = Arena.ofConfined()) {
            MemorySegment out = a.allocate(JAVA_INT), rec = a.allocate(JAVA_INT);
            Ffi.check(Ffi.call(Native15.paradox_special, l.h(0), l.h(1), kind.code(), tag.tag(),
                    Buf.fixed(a, "signature", signature, 32), Buf.fixed(a, "subject", subject, 32), out, rec), ctx);
            return new Frontier(Outcome.fromCode(out.get(JAVA_INT, 0)), rec.get(JAVA_INT, 0));
        }
    }

    /**
     * A draft classification of frontier record {@code record}, for review (logged).
     *
     * @param kind CONSTRUCTIVE, DESTRUCTIVE or NEUTRAL
     */
    public Draft draft(int record, ParadoxKind kind, int reason, int flags) {
        Buf.nonNull("kind", kind);
        try (Lease l = Lease.of(Caps.res(cap), res); Arena a = Arena.ofConfined()) {
            MemorySegment out = a.allocate(Draft.BYTES);
            Ffi.check(Ffi.call(Native15.paradox_draft, l.h(0), l.h(1), record, kind.code(), Buf.u8("reason", reason),
                    Buf.u8("flags", flags), out), ctx);
            return Draft.of(out.toArray(JAVA_BYTE));
        }
    }

    /**
     * Reviews a draft (ours or a peer's; it must match this book's frontier
     * record). A MONEY or DEVICE draft needs {@code ownerOk}; others need
     * {@code ownerOk} or {@code consensusOk} (the caller's quorum result).
     *
     * @return ACCEPTED, PROPOSAL_FOR_OWNER or REFUSED
     * @throws org.zedec.zxv.VerificationFailedException when the draft does not match the record
     */
    public Outcome review(Draft draft, boolean ownerOk, boolean consensusOk) {
        Buf.nonNull("draft", draft);
        try (Lease l = Lease.of(Caps.res(cap), res); Arena a = Arena.ofConfined()) {
            MemorySegment out = a.allocate(JAVA_INT);
            Ffi.check(Ffi.call(Native15.paradox_review, l.h(0), l.h(1), Buf.in(a, draft.raw()), ownerOk ? 1 : 0,
                    consensusOk ? 1 : 0, out), ctx);
            return Outcome.fromCode(out.get(JAVA_INT, 0));
        }
    }

    /**
     * Runs one pass of a graph in the fixed order (logged).
     *
     * @return RESOLVED or PENDING
     * @throws StepLimitException when the pass count reaches the step limit unresolved (the pass is
     *                            logged; the graph stays readable and goes to the owner)
     */
    public Outcome pass(ParadoxGraph graph) {
        Buf.nonNull("graph", graph);
        synchronized (graph) {
            try (Lease l = Lease.of(Caps.res(cap), res, graph.res); Arena a = Arena.ofConfined()) {
                MemorySegment out = a.allocate(JAVA_INT);
                Ffi.check(Ffi.call(Native15.paradox_graph_pass, l.h(0), l.h(1), l.h(2), out), ctx);
                return Outcome.fromCode(out.get(JAVA_INT, 0));
            }
        }
    }

    /** Whether {@link #close()} has run. */
    public boolean isClosed() {
        return res.isClosed();
    }

    /** Frees the book. Idempotent. */
    @Override
    public void close() {
        res.close();
    }
}
