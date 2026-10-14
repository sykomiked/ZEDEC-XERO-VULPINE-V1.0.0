// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.paradox;

import static java.lang.foreign.ValueLayout.JAVA_BYTE;
import static java.lang.foreign.ValueLayout.JAVA_INT;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import org.zedec.zxv.Capability;
import org.zedec.zxv.ZxvContext;
import org.zedec.zxv.internal.Buf;
import org.zedec.zxv.internal.Caps;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Lease;
import org.zedec.zxv.internal.Native15;
import org.zedec.zxv.internal.NativeResource;

/**
 * A neutral hold ({@code zxv_paradox_hold}): candidate branches kept side
 * by side until one has decisive evidence or the owner picks one
 * ({@link ParadoxBook#neutral}). Adding branches and evidence needs no
 * capability.
 *
 * <p>Threading: the C handle is not thread-safe, so every method here, and
 * every operator that uses the hold, synchronizes on it.
 */
public final class ParadoxHold implements AutoCloseable {
    /**
     * The hold's state.
     *
     * @param count    branches held
     * @param settled  the settled branch, or -1 while deferred
     * @param decisive the branch that currently has decisive evidence, or -1
     */
    public record Info(int count, int settled, int decisive) {}

    /**
     * One branch.
     *
     * @param id       its 32-byte digest
     * @param positive evidence for (uint32 bits)
     * @param negative evidence against (uint32 bits)
     */
    public record Branch(byte[] id, int positive, int negative) {}

    final ZxvContext ctx;
    final NativeResource res;

    private ParadoxHold(ZxvContext ctx, MemorySegment h) {
        this.ctx = ctx;
        this.res = new NativeResource("paradox hold", h, x -> Ffi.callVoid(Native15.paradox_hold_destroy, x), ctx,
                Caps.res(ctx));
    }

    /**
     * A hold.
     *
     * @param cap      a capability with {@code PARADOX}
     * @param capacity 1..{@link Paradox#HOLD_MAX} branches
     * @param decisive the lead that settles a branch (0 means 1)
     */
    public static ParadoxHold create(Capability cap, int capacity, int decisive) {
        ZxvContext ctx = Caps.ctx(cap);
        try (Lease l = Lease.of(Caps.res(cap))) {
            return new ParadoxHold(ctx, Buf.create(ctx, Native15.paradox_hold_create, l.h(0), capacity, decisive));
        }
    }

    /** Adds a candidate (the same id again is the same candidate). */
    public synchronized void add(byte[] id) {
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            Ffi.check(Ffi.call(Native15.paradox_hold_add, l.h(0), Buf.fixed(a, "id", id, 32)), ctx);
        }
    }

    /** One piece of evidence for ({@code positive}) or against a candidate. */
    public synchronized void evidence(byte[] id, boolean positive) {
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            Ffi.check(Ffi.call(Native15.paradox_hold_evidence, l.h(0), Buf.fixed(a, "id", id, 32), positive ? 1 : 0),
                    ctx);
        }
    }

    /** The branch count, the settled branch and the decisive branch. */
    public synchronized Info info() {
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            MemorySegment c = a.allocate(JAVA_INT), s = a.allocate(JAVA_INT), d = a.allocate(JAVA_INT);
            Ffi.check(Ffi.call(Native15.paradox_hold_info, l.h(0), c, s, d), ctx);
            return new Info(c.get(JAVA_INT, 0), s.get(JAVA_INT, 0), d.get(JAVA_INT, 0));
        }
    }

    /** Branch {@code index}. */
    public synchronized Branch branch(int index) {
        Buf.index("index", index);
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            MemorySegment id = a.allocate(32), p = a.allocate(JAVA_INT), n = a.allocate(JAVA_INT);
            Ffi.check(Ffi.call(Native15.paradox_hold_branch, l.h(0), index, id, p, n), ctx);
            return new Branch(id.toArray(JAVA_BYTE), p.get(JAVA_INT, 0), n.get(JAVA_INT, 0));
        }
    }

    /** Whether {@link #close()} has run. */
    public boolean isClosed() {
        return res.isClosed();
    }

    /** Frees the hold. Idempotent. */
    @Override
    public synchronized void close() {
        res.close();
    }
}
