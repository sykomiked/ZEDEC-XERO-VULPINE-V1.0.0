// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.web4;

import static java.lang.foreign.ValueLayout.JAVA_BYTE;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import org.zedec.zxv.internal.Buf;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Native15;

/**
 * The Web 4 agent layer's constants and its one pure helper. Agents hold
 * ML-DSA-65 keys ({@link AgentKey}); agent_id = SHA3-256(public key); every
 * signature carries a FIPS 204 context naming its purpose, so a signature
 * for one purpose cannot be replayed as another; an agent never moves money
 * or shares personal data without a {@link ConsentToken} signed by the
 * human's key.
 *
 * <p>Honest limits: envelopes are signed, not encrypted. A consent token
 * proves that a key holder signed; that the holder is the right human and
 * understood what they signed depends on the device and UI that showed the
 * action, which this library cannot see. The receiving runtime (replay
 * window, rate limits) and the Web 2 / Web 3 bridges are not in this ABI.
 * Interoperability is between ZXV instances only; no other agent protocol
 * is implemented or claimed. There is no clock: every call that needs the
 * time takes Unix milliseconds from the caller.
 */
public final class Web4 {
    private Web4() {}

    /** ML-DSA-65 public key size ({@code ZXV_W4_PK_BYTES}). */
    public static final int PUBLIC_KEY_BYTES = 1952;
    /** ML-DSA-65 signature size ({@code ZXV_W4_SIG_BYTES}). */
    public static final int SIGNATURE_BYTES = 3309;
    /** agent_id, peer_id and human_id size ({@code ZXV_W4_ID_BYTES}). */
    public static final int ID_BYTES = 32;
    /** Longest agent or tool name ({@code ZXV_W4_NAME_MAX}): 1..31 of [a-z0-9._-]. */
    public static final int NAME_MAX = 31;
    /** Tools per manifest ({@code ZXV_W4_MAX_TOOLS}). */
    public static final int MAX_TOOLS = 16;
    /** Largest envelope payload ({@code ZXV_W4_MSG_MAX_PAYLOAD}). */
    public static final int MAX_PAYLOAD = 2048;
    /** Largest agent card ({@code ZXV_W4_CARD_MAX}). */
    public static final int CARD_MAX = 7259;
    /** Largest manifest encoding ({@code ZXV_W4_MANIFEST_MAX}). */
    public static final int MANIFEST_MAX = 4349;
    /** Largest envelope ({@code ZXV_W4_ENV_MAX}). */
    public static final int ENVELOPE_MAX = 8970;
    /** Directory capacity limit ({@code ZXV_W4_DIR_MAX}). */
    public static final int DIRECTORY_MAX = 4096;
    /** Consent log capacity limit ({@code ZXV_W4_CONSENT_LOG_MAX}). */
    public static final int CONSENT_LOG_MAX = 65536;

    /**
     * The digest a consent token for one request must carry: SHA3-256 over
     * the purpose, tool, from, to, request id and payload. Pure.
     *
     * @param tool      a valid tool name
     * @param from      the sender's agent id (32 bytes)
     * @param to        the receiver's agent id (32 bytes)
     * @param requestId the request id (unsigned)
     * @param payload   at most {@link #MAX_PAYLOAD} bytes
     * @throws org.zedec.zxv.ZxvUsageException ZXV_E_TOO_LONG for a longer payload, ZXV_E_ARG for a bad tool name
     */
    public static byte[] actionDigest(String tool, byte[] from, byte[] to, long requestId, byte[] payload) {
        Buf.nonNull("tool", tool);
        try (Arena a = Arena.ofConfined()) {
            MemorySegment out = a.allocate(32);
            long n = payload == null ? 0 : payload.length;
            Ffi.check(Ffi.call(Native15.w4_action_digest, Ffi.cstr(a, tool), Buf.fixed(a, "from", from, ID_BYTES),
                    Buf.fixed(a, "to", to, ID_BYTES), requestId, Buf.in(a, payload), n, out), null);
            return out.toArray(JAVA_BYTE);
        }
    }
}
