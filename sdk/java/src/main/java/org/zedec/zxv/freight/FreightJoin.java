// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.freight;

import static java.lang.foreign.ValueLayout.JAVA_INT;
import static java.lang.foreign.ValueLayout.JAVA_LONG;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import org.zedec.zxv.internal.Buf;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Lease;
import org.zedec.zxv.internal.Native15;
import org.zedec.zxv.internal.NativeResource;

/**
 * Reassembles one freight set, member by member, in any order
 * ({@code zxv_freight_join}). The first member accepted fixes the set (id,
 * k, flags, count, expand limit). It holds only its own memory: no context
 * and no capability.
 *
 * <p>Threading: the C join is not thread-safe, so every method here is
 * {@code synchronized}; one thread at a time uses it.
 */
public final class FreightJoin implements AutoCloseable {
    /**
     * Progress.
     *
     * @param received members accepted
     * @param count    members in the set (0 until the first is accepted)
     * @param complete whether every member arrived
     * @param total    the joined length once complete, else 0
     */
    public record Status(int received, int count, boolean complete, long total) {}

    private final NativeResource res;

    private FreightJoin(MemorySegment h) {
        this.res = new NativeResource("freight join", h, x -> Ffi.callVoid(Native15.freight_join_destroy, x), null, null);
    }

    /**
     * A join with a buffer of {@code capacity} bytes (0..{@link Freight#SET_MAX_BYTES}).
     *
     * @throws org.zedec.zxv.ZxvUsageException ZXV_E_RANGE for a larger capacity
     */
    public static FreightJoin create(long capacity) {
        return new FreightJoin(Buf.create(null, Native15.freight_join_create, capacity));
    }

    /**
     * Decodes one member into its place. A member already received changes
     * nothing; a failed member is not recorded (add it again when more rows arrive).
     *
     * @throws ErasureException                       with fewer than k distinct rows
     * @throws org.zedec.zxv.ZxvRefusedException     ZXV_E_STATE for a member of another set;
     *                                                ZXV_E_CAPACITY when the set cannot fit
     */
    public synchronized void add(FreightHeader header, byte[] packets) {
        Buf.nonNull("header", header);
        Buf.nonNull("packets", packets);
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            Ffi.check(Ffi.call(Native15.freight_join_add, l.h(0), Buf.in(a, header.toBytes()), Buf.in(a, packets),
                    (long) packets.length), null);
        }
    }

    /** Adds a member produced by {@link Freight#split}. */
    public void add(Freight.Member member) {
        Buf.nonNull("member", member);
        add(member.header(), member.packets());
    }

    /** Progress so far. */
    public synchronized Status status() {
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            MemorySegment r = a.allocate(JAVA_INT), c = a.allocate(JAVA_INT), done = a.allocate(JAVA_INT),
                    t = a.allocate(JAVA_LONG);
            Ffi.check(Ffi.call(Native15.freight_join_status, l.h(0), r, c, done, t), null);
            return new Status(r.get(JAVA_INT, 0), c.get(JAVA_INT, 0), done.get(JAVA_INT, 0) != 0, t.get(JAVA_LONG, 0));
        }
    }

    /**
     * The joined bytes.
     *
     * @throws org.zedec.zxv.ZxvRefusedException ZXV_E_STATE until every member arrived
     */
    public synchronized byte[] result() {
        try (Lease l = Lease.of(res)) {
            MemorySegment h = l.h(0);
            return Buf.r4(null, 4096, (buf, cap, len) -> Ffi.call(Native15.freight_join_result, h, buf, cap, len));
        }
    }

    /** Whether {@link #close()} has run. */
    public boolean isClosed() {
        return res.isClosed();
    }

    /** Frees the join. Idempotent. */
    @Override
    public synchronized void close() {
        res.close();
    }
}
