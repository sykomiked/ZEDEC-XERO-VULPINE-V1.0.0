// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.web4;

import static java.lang.foreign.ValueLayout.JAVA_INT;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.util.Optional;
import org.zedec.zxv.ZxvStatus;
import org.zedec.zxv.ZxvUsageException;
import org.zedec.zxv.internal.Buf;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Lease;
import org.zedec.zxv.internal.Native15;
import org.zedec.zxv.internal.NativeResource;

/**
 * A directory of verified agent cards, keyed by agent id
 * ({@code zxv_w4_dir}). Every card is verified before it is added; a card
 * whose binding is older than the one held is refused (no rollback). Holds
 * only caller state: no context or capability.
 *
 * <p>Threading: the C directory is not thread-safe, so every method here is
 * {@code synchronized}.
 */
public final class AgentDirectory implements AutoCloseable {
    private final NativeResource res;

    private AgentDirectory(MemorySegment h) {
        this.res = new NativeResource("agent directory", h, x -> Ffi.callVoid(Native15.w4_dir_destroy, x), null, null);
    }

    /**
     * A directory of at most {@code capacity} (1..{@link Web4#DIRECTORY_MAX}) cards.
     *
     * @throws org.zedec.zxv.ZxvUsageException ZXV_E_RANGE for a capacity out of range
     */
    public static AgentDirectory create(int capacity) {
        return new AgentDirectory(Buf.create(null, Native15.w4_dir_create, capacity));
    }

    /**
     * Verifies a card, then adds it or replaces the entry for the same agent.
     *
     * @throws org.zedec.zxv.VerificationFailedException when the card's signature fails
     * @throws org.zedec.zxv.ZxvRefusedException         ZXV_E_STATE for an older binding, ZXV_E_CAPACITY when full
     */
    public synchronized void add(AgentCard card) {
        Buf.nonNull("card", card);
        byte[] b = card.bytes();
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            Ffi.check(Ffi.call(Native15.w4_dir_add, l.h(0), Buf.in(a, b), (long) b.length), null);
        }
    }

    /** The card held for {@code agentId}, if any. */
    public synchronized Optional<AgentCard> find(byte[] agentId) {
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            MemorySegment h = l.h(0), id = Buf.fixed(a, "agentId", agentId, Web4.ID_BYTES);
            return Optional.of(AgentCard.wrap(
                    Buf.r4(null, Web4.CARD_MAX, (buf, n, len) -> Ffi.call(Native15.w4_dir_find, h, id, buf, n, len))));
        } catch (ZxvUsageException e) {
            if (e.status() == ZxvStatus.E_NOT_FOUND) return Optional.empty();
            throw e;
        }
    }

    /** The number of cards held. */
    public synchronized int count() {
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            MemorySegment n = a.allocate(JAVA_INT);
            Ffi.check(Ffi.call(Native15.w4_dir_count, l.h(0), n), null);
            return n.get(JAVA_INT, 0);
        }
    }

    /** Whether {@link #close()} has run. */
    public boolean isClosed() {
        return res.isClosed();
    }

    /** Frees the directory. Idempotent. */
    @Override
    public synchronized void close() {
        res.close();
    }
}
