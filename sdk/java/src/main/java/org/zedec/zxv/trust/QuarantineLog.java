// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.trust;

import static java.lang.foreign.ValueLayout.ADDRESS;
import static java.lang.foreign.ValueLayout.JAVA_BYTE;
import static java.lang.foreign.ValueLayout.JAVA_INT;
import static java.lang.foreign.ValueLayout.JAVA_LONG;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import org.zedec.zxv.Capability;
import org.zedec.zxv.Hexagram;
import org.zedec.zxv.ZxvContext;
import org.zedec.zxv.internal.Caps;
import org.zedec.zxv.internal.Errors;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Lease;
import org.zedec.zxv.internal.Native;
import org.zedec.zxv.internal.NativeResource;

/**
 * The quarantine log of ABI 1.3 ({@code zxv_hexlog}): an append-only,
 * SHA-256 hash-chained record of forbidden work, read for audit and never
 * replayed. Creating and appending need {@code ATTEST}.
 *
 * <p>Record k's hash is SHA-256("zxv/lpx-qlog/v1" || seq (u64 LE) || tag ||
 * reason || submitter (u32 LE) || subject || prev), so the chain can be
 * recomputed from {@link #record(int)}. A full log refuses the append with
 * {@code ZXV_E_CAPACITY}; the caller must then halt the context
 * ({@link ZxvContext#halt}), since forbidden work it cannot record must not
 * continue. Signing {@link #head()} is the caller's job.
 */
public final class QuarantineLog implements AutoCloseable {
    /** Largest capacity ({@code ZXV_HEXLOG_MAX_RECORDS}). */
    public static final int MAX_RECORDS = 65536;

    /**
     * One record.
     *
     * @param seq       its index
     * @param tag       the forbidden tag
     * @param reason    the caller's reason code, 0..255
     * @param submitter who submitted the work (uint32 bits)
     * @param subject   32-byte digest of the refused work
     * @param prev      the previous record's hash (zeros for the first)
     * @param hash      this record's hash
     */
    public record Record(long seq, Hexagram tag, int reason, int submitter, byte[] subject, byte[] prev, byte[] hash) {}

    private final Capability cap;
    private final ZxvContext ctx;
    private final NativeResource res;

    private QuarantineLog(Capability cap, ZxvContext ctx, MemorySegment h) {
        this.cap = cap;
        this.ctx = ctx;
        this.res = new NativeResource("quarantine log", h, x -> Ffi.callVoid(Native.hexlog_destroy, x), ctx, Caps.res(ctx));
    }

    /** A log of at most {@code capacity} (1..{@link #MAX_RECORDS}) records, appended through {@code cap}. */
    public static QuarantineLog create(Capability cap, int capacity) {
        ZxvContext ctx = Caps.ctx(cap);
        try (Lease l = Lease.of(Caps.res(cap)); Arena a = Arena.ofConfined()) {
            MemorySegment out = a.allocate(ADDRESS);
            Ffi.check(Ffi.call(Native.hexlog_create, l.h(0), capacity, out), ctx);
            return new QuarantineLog(cap, ctx, out.get(ADDRESS, 0));
        }
    }

    /**
     * Quarantines forbidden work.
     *
     * @param tag       a forbidden tag (any other is ZXV_E_ARG)
     * @param reason    0..255
     * @param submitter who submitted it (uint32 bits)
     * @param subject   a 32-byte digest of the work
     * @return the record's sequence number
     */
    public long append(Hexagram tag, int reason, int submitter, byte[] subject) {
        if (reason < 0 || reason > 255) throw Errors.usage(-1, "reason must be 0..255");
        Ffi.requireLength("subject", subject, 32);
        try (Lease l = Lease.of(Caps.res(cap), res); Arena a = Arena.ofConfined()) {
            MemorySegment seq = a.allocate(JAVA_LONG);
            Ffi.check(Ffi.call(Native.hexlog_append, l.h(0), l.h(1), tag.tag(), reason, submitter,
                    Ffi.bytes(a, subject), seq), ctx);
            return seq.get(JAVA_LONG, 0);
        }
    }

    /** Number of records. */
    public int count() {
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            MemorySegment n = a.allocate(JAVA_INT);
            Ffi.check(Ffi.call(Native.hexlog_count, l.h(0), n), ctx);
            return n.get(JAVA_INT, 0);
        }
    }

    /** The chain head: the last record's hash (32 zero bytes when empty). */
    public byte[] head() {
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            MemorySegment h = a.allocate(32);
            Ffi.check(Ffi.call(Native.hexlog_head, l.h(0), h), ctx);
            return h.toArray(JAVA_BYTE);
        }
    }

    /** Record {@code index}, oldest first. */
    public Record record(int index) {
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            MemorySegment seq = a.allocate(JAVA_LONG), tag = a.allocate(JAVA_BYTE), reason = a.allocate(JAVA_BYTE),
                    who = a.allocate(JAVA_INT), subj = a.allocate(32), prev = a.allocate(32), hash = a.allocate(32);
            Ffi.check(Ffi.call(Native.hexlog_at, l.h(0), index, seq, tag, reason, who, subj, prev, hash), ctx);
            return new Record(seq.get(JAVA_LONG, 0), Hexagram.fromTag(Byte.toUnsignedInt(tag.get(JAVA_BYTE, 0))),
                    Byte.toUnsignedInt(reason.get(JAVA_BYTE, 0)), who.get(JAVA_INT, 0),
                    subj.toArray(JAVA_BYTE), prev.toArray(JAVA_BYTE), hash.toArray(JAVA_BYTE));
        }
    }

    /**
     * Recomputes every link.
     *
     * @throws org.zedec.zxv.ZxvFatalError if the chain is broken (the context halts)
     */
    public void verify() {
        try (Lease l = Lease.of(res)) {
            Ffi.check(Ffi.call(Native.hexlog_verify, l.h(0)), ctx);
        }
    }

    /** Frees the log. Idempotent. */
    @Override
    public void close() {
        res.close();
    }
}
