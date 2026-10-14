// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.coin;

import static java.lang.foreign.ValueLayout.JAVA_INT;
import static java.lang.foreign.ValueLayout.JAVA_LONG;

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
 * A token wallet ({@code zxv_token_wallet}): a holding kept canonical at the
 * wallet's own octane. Every change needs {@link org.zedec.zxv.Permission#MINT}
 * from the wallet's context; reads need nothing. It fails closed when its
 * context halts.
 *
 * <p>Honest limits: tokens enter a wallet only by {@link #mint}, which
 * creates them. The wallet is not tied to a swarm budget (the kernel's
 * budget lockstep is not in this ABI), and density earns nothing: changing
 * octane re-expresses the same value.
 *
 * <p>Threading: libzxv serializes the wallet, so any thread may call.
 * Amounts are unsigned 64-bit values in a Java {@code long}.
 */
public final class TokenWallet implements AutoCloseable {
    /**
     * A snapshot of the wallet.
     *
     * @param modelId the wallet's label (uint32 bits)
     * @param octane  its octane
     * @param value   its value (unsigned)
     * @param pieces  its number of pieces (unsigned)
     */
    public record Info(int modelId, int octane, long value, long pieces) {}

    private final Capability cap;
    private final ZxvContext ctx;
    private final NativeResource res;

    private TokenWallet(Capability cap, ZxvContext ctx, MemorySegment h) {
        this.cap = cap;
        this.ctx = ctx;
        this.res = new NativeResource("token wallet", h, x -> Ffi.callVoid(Native15.token_wallet_destroy, x), ctx,
                Caps.res(ctx));
    }

    /**
     * An empty wallet.
     *
     * @param cap     a capability with {@code MINT}; every later change uses it
     * @param modelId a free label, e.g. the swarm model that holds it (uint32 bits)
     * @param octane  0..63; see {@link Octane}
     * @throws org.zedec.zxv.CapabilityDeniedException without {@code MINT}
     */
    public static TokenWallet create(Capability cap, int modelId, int octane) {
        ZxvContext ctx = Caps.ctx(cap);
        Buf.index("octane", octane);
        try (Lease l = Lease.of(Caps.res(cap))) {
            return new TokenWallet(cap, ctx, Buf.create(ctx, Native15.token_wallet_create, l.h(0), modelId, octane));
        }
    }

    /** Creates {@code amount} new tokens here. Needs {@code MINT}; ZXV_E_RANGE (nothing changes) past 64 bits. */
    public void mint(long amount) {
        gated(Native15.token_wallet_mint, amount);
    }

    /**
     * Spends (burns) {@code amount}; the change stays canonical. Needs {@code MINT}.
     *
     * @throws org.zedec.zxv.InsufficientFundsException when the wallet holds less (nothing changes)
     */
    public void spend(long amount) {
        gated(Native15.token_wallet_spend, amount);
    }

    /** Re-expresses the same value at a new octane (0..63). Needs {@code MINT}. */
    public void setOctane(int octane) {
        Buf.index("octane", octane);
        gated(Native15.token_wallet_set_octane, octane);
    }

    /**
     * Moves {@code amount} to {@code to}, which receives it at its own octane.
     * The total is conserved exactly; neither wallet changes on failure.
     * Uses this wallet's capability, which must hold {@code MINT} for the context both wallets belong to.
     *
     * @throws org.zedec.zxv.InsufficientFundsException when this wallet holds less
     */
    public void transfer(TokenWallet to, long amount) {
        Buf.nonNull("to", to);
        try (Lease l = to == this ? Lease.of(Caps.res(cap), res) : Lease.of(Caps.res(cap), res, to.res)) {
            Ffi.check(Ffi.call(Native15.token_wallet_transfer, l.h(0), l.h(1), to == this ? l.h(1) : l.h(2), amount), ctx);
        }
    }

    private void gated(java.lang.invoke.MethodHandle mh, Object arg) {
        try (Lease l = Lease.of(Caps.res(cap), res)) {
            Ffi.check(Ffi.call(mh, l.h(0), l.h(1), arg), ctx);
        }
    }

    /** The wallet's label, octane, value and piece count. */
    public Info info() {
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            MemorySegment m = a.allocate(JAVA_INT), o = a.allocate(JAVA_INT), v = a.allocate(JAVA_LONG),
                    p = a.allocate(JAVA_LONG);
            Ffi.check(Ffi.call(Native15.token_wallet_info, l.h(0), m, o, v, p), ctx);
            return new Info(m.get(JAVA_INT, 0), o.get(JAVA_INT, 0), v.get(JAVA_LONG, 0), p.get(JAVA_LONG, 0));
        }
    }

    /** The wallet's value (unsigned). */
    public long value() {
        return info().value();
    }

    /** The wallet's counts per tier, as a holding. */
    public TokenHolding holding() {
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            MemorySegment c = a.allocate(JAVA_LONG, Tokens.TIERS);
            Ffi.check(Ffi.call(Native15.token_wallet_counts, l.h(0), c), ctx);
            return TokenHolding.of(c.toArray(JAVA_LONG));
        }
    }

    /** Whether {@link #close()} has run. */
    public boolean isClosed() {
        return res.isClosed();
    }

    /** Frees the wallet. Idempotent. */
    @Override
    public void close() {
        res.close();
    }
}
