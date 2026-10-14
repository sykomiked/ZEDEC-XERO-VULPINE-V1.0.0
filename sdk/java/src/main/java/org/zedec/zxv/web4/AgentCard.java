// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.web4;

import static java.lang.foreign.ValueLayout.JAVA_BYTE;
import static java.lang.foreign.ValueLayout.JAVA_LONG;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.util.Arrays;
import org.zedec.zxv.internal.Buf;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Native15;

/**
 * An agent card: what a peer needs to verify an agent ("W4CD", flags, name,
 * binding time, public key, node public key unless self-bound, binding
 * signature). The ids are not carried; a reader derives them from the keys.
 * An immutable byte value; {@link #verify()} checks it.
 */
public final class AgentCard {
    /**
     * The facts of a verified card.
     *
     * @param agentId SHA3-256 of the agent's public key
     * @param peerId  SHA3-256 of the hosting node's public key (the agent id when self-bound)
     * @param name    the agent's name
     * @param boundMs the binding time, Unix milliseconds (unsigned)
     */
    public record Verified(byte[] agentId, byte[] peerId, String name, long boundMs) {
        /** Whether the agent signed its own binding. */
        public boolean isSelfBound() {
            return Arrays.equals(agentId, peerId);
        }
    }

    private final byte[] bytes;

    private AgentCard(byte[] bytes) {
        this.bytes = bytes;
    }

    /** A card from its bytes (copied; not yet verified). */
    public static AgentCard of(byte[] bytes) {
        return new AgentCard(Buf.nonNull("bytes", bytes).clone());
    }

    static AgentCard wrap(byte[] bytes) {
        return new AgentCard(bytes);
    }

    /** The card's bytes (a copy). */
    public byte[] bytes() {
        return bytes.clone();
    }

    /**
     * Checks that the card is well formed and its binding signature is valid
     * under the node key (or the agent's own key when self-bound). Pure.
     *
     * @throws org.zedec.zxv.VerificationFailedException when the signature fails
     * @throws org.zedec.zxv.ZxvUsageException           ZXV_E_FORMAT for a malformed card
     */
    public Verified verify() {
        try (Arena a = Arena.ofConfined()) {
            MemorySegment id = a.allocate(32), peer = a.allocate(32), name = a.allocate(32), bound = a.allocate(JAVA_LONG);
            Ffi.check(Ffi.call(Native15.w4_card_verify, Buf.in(a, bytes), (long) bytes.length, id, peer, name, bound),
                    null);
            return new Verified(id.toArray(JAVA_BYTE), peer.toArray(JAVA_BYTE), W4.name(name, 32),
                    bound.get(JAVA_LONG, 0));
        }
    }

    /** Equal when the bytes are equal. */
    @Override
    public boolean equals(Object o) {
        return o instanceof AgentCard c && Arrays.equals(bytes, c.bytes);
    }

    /** A hash of the bytes. */
    @Override
    public int hashCode() {
        return Arrays.hashCode(bytes);
    }

    /** The card's length. */
    @Override
    public String toString() {
        return "AgentCard[" + bytes.length + " bytes]";
    }
}
