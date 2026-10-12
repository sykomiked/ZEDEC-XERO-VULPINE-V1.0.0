// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.web4;

import static java.lang.foreign.ValueLayout.JAVA_INT;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import org.zedec.zxv.internal.Buf;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Lease;
import org.zedec.zxv.internal.Native15;
import org.zedec.zxv.internal.NativeResource;

/**
 * A single-use log of consumed and revoked consent tokens
 * ({@code zxv_w4_consent_log}). A token passes {@link #check} once; a full
 * log refuses new consent (fail closed) until expired entries are pruned.
 * Holds only caller state: no context or capability.
 *
 * <p>Threading: the C log is not thread-safe, so every method here is
 * {@code synchronized}.
 */
public final class ConsentLog implements AutoCloseable {
    private final NativeResource res;

    private ConsentLog(MemorySegment h) {
        this.res = new NativeResource("consent log", h, x -> Ffi.callVoid(Native15.w4_consent_log_destroy, x), null, null);
    }

    /**
     * A log of {@code capacity} (1..{@link Web4#CONSENT_LOG_MAX}) entries.
     *
     * @throws org.zedec.zxv.ZxvUsageException ZXV_E_RANGE for a capacity out of range
     */
    public static ConsentLog create(int capacity) {
        return new ConsentLog(Buf.create(null, Native15.w4_consent_log_create, capacity));
    }

    /**
     * Checks that {@code token} lets {@code agentId} do {@code action} with
     * scope {@code need} for {@code amount}, signed by the human whose key is
     * {@code humanPublicKey} and valid at {@code nowMs}; then records it as
     * spent. Returning normally means consent was given and is now used.
     *
     * @param token  the token, or null (always refused)
     * @param need   nonzero {@link ConsentScope} bits
     * @param amount VFV minor units, checked against the token's maximum when {@code need} has MONEY (unsigned)
     * @throws ConsentException                  for no token, a mismatch, a token out of its validity, a bad
     *                                           signature, or a token already used or revoked
     * @throws org.zedec.zxv.ZxvRefusedException ZXV_E_CAPACITY when the log is full even after pruning
     */
    public synchronized void check(ConsentToken token, byte[] humanPublicKey, byte[] agentId, byte[] action, int need,
            long amount, long nowMs) {
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            MemorySegment t = token == null ? MemorySegment.NULL : Buf.in(a, token.raw());
            Ffi.check(Ffi.call(Native15.w4_consent_check, l.h(0), t,
                    Buf.fixed(a, "humanPublicKey", humanPublicKey, Web4.PUBLIC_KEY_BYTES),
                    Buf.fixed(a, "agentId", agentId, Web4.ID_BYTES), Buf.fixed(a, "action", action, 32), need, amount,
                    nowMs), null);
        }
    }

    /** Revokes a token before use (later checks fail); revoking twice is fine. */
    public synchronized void revoke(ConsentToken token, long nowMs) {
        Buf.nonNull("token", token);
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            Ffi.check(Ffi.call(Native15.w4_consent_revoke, l.h(0), Buf.in(a, token.raw()), nowMs), null);
        }
    }

    /** Drops entries whose token expired at or before {@code nowMs}; returns what is left. */
    public synchronized int prune(long nowMs) {
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            MemorySegment r = a.allocate(JAVA_INT);
            Ffi.check(Ffi.call(Native15.w4_consent_log_prune, l.h(0), nowMs, r), null);
            return r.get(JAVA_INT, 0);
        }
    }

    /** Whether {@link #close()} has run. */
    public boolean isClosed() {
        return res.isClosed();
    }

    /** Frees the log. Idempotent. */
    @Override
    public synchronized void close() {
        res.close();
    }
}
