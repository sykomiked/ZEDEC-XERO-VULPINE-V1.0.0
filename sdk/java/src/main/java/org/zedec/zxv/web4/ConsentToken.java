// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.web4;

import static java.lang.foreign.ValueLayout.JAVA_BYTE;
import static java.lang.foreign.ValueLayout.JAVA_INT;
import static java.lang.foreign.ValueLayout.JAVA_LONG;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.util.Arrays;
import org.zedec.zxv.internal.Buf;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Native15;

/**
 * A human's consent token ("W4CS", {@link #BYTES} bytes): signed by the
 * human's key over {human_id, agent_id, action digest, scope, max amount,
 * validity, nonce}. Single-use: a {@link ConsentLog} spends it. An immutable
 * value whose fields are read when it is created; reading them does not
 * check the signature ({@link ConsentLog#check} does).
 */
public final class ConsentToken {
    /** Encoded size ({@code ZXV_W4_CONSENT_BYTES}). */
    public static final int BYTES = 3453;

    private final byte[] bytes;
    private final byte[] humanId, agentId, action, nonce;
    private final int scope;
    private final long maxVfv, issuedMs, expiresMs;

    private ConsentToken(byte[] bytes) {
        this.bytes = bytes;
        try (Arena a = Arena.ofConfined()) {
            MemorySegment h = a.allocate(32), ag = a.allocate(32), ac = a.allocate(32), s = a.allocate(JAVA_INT),
                    m = a.allocate(JAVA_LONG), i = a.allocate(JAVA_LONG), e = a.allocate(JAVA_LONG), n = a.allocate(16);
            Ffi.check(Ffi.call(Native15.w4_consent_info, Buf.fixed(a, "token", bytes, BYTES), h, ag, ac, s, m, i, e, n),
                    null);
            humanId = h.toArray(JAVA_BYTE);
            agentId = ag.toArray(JAVA_BYTE);
            action = ac.toArray(JAVA_BYTE);
            scope = s.get(JAVA_INT, 0);
            maxVfv = m.get(JAVA_LONG, 0);
            issuedMs = i.get(JAVA_LONG, 0);
            expiresMs = e.get(JAVA_LONG, 0);
            nonce = n.toArray(JAVA_BYTE);
        }
    }

    /**
     * A token from its bytes (copied; the fields are parsed, the signature is not checked).
     *
     * @throws org.zedec.zxv.ZxvUsageException ZXV_E_FORMAT for a malformed token
     */
    public static ConsentToken of(byte[] bytes) {
        return new ConsentToken(Buf.nonNull("bytes", bytes).clone());
    }

    /** The encoded token (a copy). */
    public byte[] bytes() {
        return bytes.clone();
    }

    byte[] raw() {
        return bytes;
    }

    /** The signing human's id. */
    public byte[] humanId() {
        return humanId.clone();
    }

    /** The agent the token lets act. */
    public byte[] agentId() {
        return agentId.clone();
    }

    /** The action digest ({@link Web4#actionDigest}). */
    public byte[] action() {
        return action.clone();
    }

    /** The {@link ConsentScope} bits. */
    public int scope() {
        return scope;
    }

    /** The most money, in VFV minor units (unsigned). */
    public long maxVfv() {
        return maxVfv;
    }

    /** Start of validity, Unix milliseconds. */
    public long issuedMs() {
        return issuedMs;
    }

    /** End of validity (exclusive), Unix milliseconds. */
    public long expiresMs() {
        return expiresMs;
    }

    /** The 16-byte nonce that makes the token single-use. */
    public byte[] nonce() {
        return nonce.clone();
    }

    /** Equal when the bytes are equal. */
    @Override
    public boolean equals(Object o) {
        return o instanceof ConsentToken t && Arrays.equals(bytes, t.bytes);
    }

    /** A hash of the bytes. */
    @Override
    public int hashCode() {
        return Arrays.hashCode(bytes);
    }

    /** Scope, amount and validity (never the signature). */
    @Override
    public String toString() {
        return "ConsentToken[scope=" + scope + ", maxVfv=" + Long.toUnsignedString(maxVfv) + ", valid=[" + issuedMs
                + ", " + expiresMs + ")]";
    }
}
