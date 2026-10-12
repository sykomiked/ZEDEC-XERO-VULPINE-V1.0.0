// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.paradox;

import static java.lang.foreign.ValueLayout.JAVA_INT;
import static java.lang.foreign.ValueLayout.JAVA_LONG;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import org.zedec.zxv.Capability;
import org.zedec.zxv.ZxvContext;
import org.zedec.zxv.internal.Buf;
import org.zedec.zxv.internal.Caps;
import org.zedec.zxv.internal.Errors;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Lease;
import org.zedec.zxv.internal.Native15;
import org.zedec.zxv.internal.NativeResource;

/**
 * A constructive policy ({@code zxv_paradox_policy}): n swarm parameters
 * (budget shares, routing weights, escrow timing; never balances) with
 * published bounds that never change. {@link ParadoxBook#constructive}
 * applies an update only when every value is inside its bounds; a policy
 * flagged {@link ParadoxFlag#MONEY} or {@link ParadoxFlag#DEVICE} is never
 * changed automatically: every update becomes a proposal for the owner.
 *
 * <p>Threading: the C handle is not thread-safe, so every method here, and
 * every operator that uses the policy, synchronizes on it.
 */
public final class ParadoxPolicy implements AutoCloseable {
    /**
     * The live values and the values before the last applied update.
     *
     * @param values    the live values
     * @param previous  the values before the last applied update
     * @param armed     whether an applied update awaits evaluation
     * @param tensionAt the tension it was applied at (uint32 bits)
     */
    public record Values(long[] values, long[] previous, boolean armed, int tensionAt) {}

    /**
     * The published bounds.
     *
     * @param lo    lower bounds
     * @param hi    upper bounds
     * @param flags the {@link ParadoxFlag} bits
     */
    public record Bounds(long[] lo, long[] hi, int flags) {}

    final ZxvContext ctx;
    final NativeResource res;
    private final int n;

    private ParadoxPolicy(ZxvContext ctx, MemorySegment h, int n) {
        this.ctx = ctx;
        this.n = n;
        this.res = new NativeResource("paradox policy", h, x -> Ffi.callVoid(Native15.paradox_policy_destroy, x), ctx,
                Caps.res(ctx));
    }

    /**
     * A policy.
     *
     * @param cap     a capability with {@code PARADOX}
     * @param lo      lower bounds (1..64 values)
     * @param hi      upper bounds, same length, {@code lo[i] <= hi[i]}
     * @param initial initial values inside the bounds, same length
     * @param flags   {@link ParadoxFlag} bits
     * @throws org.zedec.zxv.CapabilityDeniedException without {@code PARADOX}
     * @throws org.zedec.zxv.ZxvUsageException         ZXV_E_RANGE for crossed bounds or an initial value outside them
     */
    public static ParadoxPolicy create(Capability cap, long[] lo, long[] hi, long[] initial, int flags) {
        ZxvContext ctx = Caps.ctx(cap);
        int n = Buf.nonNull("lo", lo).length;
        if (Buf.nonNull("hi", hi).length != n || Buf.nonNull("initial", initial).length != n) {
            throw Errors.usage(-1, "lo, hi and initial must have the same length");
        }
        try (Lease l = Lease.of(Caps.res(cap)); Arena a = Arena.ofConfined()) {
            return new ParadoxPolicy(ctx, Buf.create(ctx, Native15.paradox_policy_create, l.h(0), n, seg(a, lo),
                    seg(a, hi), seg(a, initial), flags), n);
        }
    }

    static MemorySegment seg(Arena a, long[] v) {
        MemorySegment s = a.allocate(JAVA_LONG, Math.max(1, v.length));
        MemorySegment.copy(v, 0, s, JAVA_LONG, 0, v.length);
        return s;
    }

    /** The number of parameters. */
    public int size() {
        return n;
    }

    /** The live and previous values, and whether an update is armed. */
    public synchronized Values values() {
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            MemorySegment v = a.allocate(JAVA_LONG, n), p = a.allocate(JAVA_LONG, n), armed = a.allocate(JAVA_INT),
                    t = a.allocate(JAVA_INT);
            Ffi.check(Ffi.call(Native15.paradox_policy_values, l.h(0), v, p, n, armed, t), ctx);
            return new Values(v.toArray(JAVA_LONG), p.toArray(JAVA_LONG), armed.get(JAVA_INT, 0) != 0,
                    t.get(JAVA_INT, 0));
        }
    }

    /** The bounds and flags. */
    public synchronized Bounds bounds() {
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            MemorySegment lo = a.allocate(JAVA_LONG, n), hi = a.allocate(JAVA_LONG, n), f = a.allocate(JAVA_INT);
            Ffi.check(Ffi.call(Native15.paradox_policy_bounds, l.h(0), lo, hi, n, f), ctx);
            return new Bounds(lo.toArray(JAVA_LONG), hi.toArray(JAVA_LONG), f.get(JAVA_INT, 0));
        }
    }

    /**
     * A proposed update as bytes for the owner or for signing (12 + 32n
     * bytes: n, how many values are out of bounds, flags, then lo, hi,
     * current and proposed per value). Reads only.
     */
    public synchronized byte[] proposal(long[] synth) {
        Buf.nonNull("synth", synth);
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            MemorySegment h = l.h(0), s = seg(a, synth);
            int len = synth.length;
            return Buf.r4(ctx, 12L + 32L * n,
                    (buf, cap, out) -> Ffi.call(Native15.paradox_policy_proposal, h, s, len, buf, cap, out));
        }
    }

    /** Whether {@link #close()} has run. */
    public boolean isClosed() {
        return res.isClosed();
    }

    /** Frees the policy. Idempotent. */
    @Override
    public synchronized void close() {
        res.close();
    }
}
