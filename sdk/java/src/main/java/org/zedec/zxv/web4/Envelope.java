// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.web4;

import static java.lang.foreign.ValueLayout.JAVA_BYTE;
import static java.lang.foreign.ValueLayout.JAVA_INT;
import static java.lang.foreign.ValueLayout.JAVA_LONG;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.util.Arrays;
import java.util.Optional;
import org.zedec.zxv.ZxvStatus;
import org.zedec.zxv.internal.Buf;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Native15;

/**
 * A signed envelope: kind, flags, from, to, a per-sender sequence number, a
 * timestamp, request id, chunk index, tool, payload, an optional consent
 * token and the sender's ML-DSA-65 signature. An immutable value whose
 * header is parsed when it is created; parsing does not check the signature
 * ({@link #verify} does). Replay, timestamp and rate checks are the
 * receiver's job. Envelopes are signed, not encrypted.
 */
public final class Envelope {
    /** Flag: the last STREAM chunk or final response ({@code ZXV_W4_MF_FINAL}). */
    public static final int FLAG_FINAL = 0x01;
    /** Flag: a consent token is attached ({@code ZXV_W4_MF_CONSENT}; set by sealing, never by the caller). */
    public static final int FLAG_CONSENT = 0x02;

    private final byte[] bytes;
    private final EnvelopeKind kind;
    private final int flags, chunk, payloadLength;
    private final byte[] from, to;
    private final long seq, timestampMs, requestId;
    private final String tool;

    private Envelope(byte[] bytes) {
        this.bytes = bytes;
        try (Arena a = Arena.ofConfined()) {
            MemorySegment k = a.allocate(JAVA_INT), f = a.allocate(JAVA_INT), fr = a.allocate(32), t = a.allocate(32),
                    s = a.allocate(JAVA_LONG), ts = a.allocate(JAVA_LONG), r = a.allocate(JAVA_LONG),
                    c = a.allocate(JAVA_INT), tool = a.allocate(32), pl = a.allocate(JAVA_INT);
            Ffi.check(Ffi.call(Native15.w4_env_decode, Buf.in(a, bytes), (long) bytes.length, k, f, fr, t, s, ts, r, c,
                    tool, pl), null);
            this.kind = EnvelopeKind.fromCode(k.get(JAVA_INT, 0));
            this.flags = f.get(JAVA_INT, 0);
            this.from = fr.toArray(JAVA_BYTE);
            this.to = t.toArray(JAVA_BYTE);
            this.seq = s.get(JAVA_LONG, 0);
            this.timestampMs = ts.get(JAVA_LONG, 0);
            this.requestId = r.get(JAVA_LONG, 0);
            this.chunk = c.get(JAVA_INT, 0);
            this.tool = W4.name(tool, 32);
            this.payloadLength = pl.get(JAVA_INT, 0);
        }
    }

    /**
     * An envelope from its bytes (copied; parsed, not verified).
     *
     * @throws org.zedec.zxv.ZxvUsageException ZXV_E_FORMAT for a malformed envelope
     */
    public static Envelope of(byte[] bytes) {
        return new Envelope(Buf.nonNull("bytes", bytes).clone());
    }

    static Envelope wrap(byte[] bytes) {
        return new Envelope(bytes);
    }

    byte[] raw() {
        return bytes;
    }

    /** The encoded envelope (a copy). */
    public byte[] bytes() {
        return bytes.clone();
    }

    /** The kind. */
    public EnvelopeKind kind() {
        return kind;
    }

    /** The flags ({@link #FLAG_FINAL}, {@link #FLAG_CONSENT}). */
    public int flags() {
        return flags;
    }

    /** Whether {@link #FLAG_FINAL} is set. */
    public boolean isFinal() {
        return (flags & FLAG_FINAL) != 0;
    }

    /** The sender's agent id. */
    public byte[] from() {
        return from.clone();
    }

    /** The receiver's agent id. */
    public byte[] to() {
        return to.clone();
    }

    /** The sender's sequence number (unsigned, strictly increasing per sender). */
    public long sequence() {
        return seq;
    }

    /** The sender's timestamp, Unix milliseconds. */
    public long timestampMs() {
        return timestampMs;
    }

    /** The request id (unsigned). */
    public long requestId() {
        return requestId;
    }

    /** The chunk index (uint32 bits). */
    public int chunk() {
        return chunk;
    }

    /** The tool name. */
    public String tool() {
        return tool;
    }

    /** The payload's length. */
    public int payloadLength() {
        return payloadLength;
    }

    /** The payload. */
    public byte[] payload() {
        try (Arena a = Arena.ofConfined()) {
            MemorySegment e = Buf.in(a, bytes);
            long n = bytes.length;
            return Buf.r4(null, payloadLength, (buf, cap, len) -> Ffi.call(Native15.w4_env_payload, e, n, buf, cap, len));
        }
    }

    /** The attached consent token, if any. */
    public Optional<ConsentToken> consent() {
        try (Arena a = Arena.ofConfined()) {
            MemorySegment out = a.allocate(ConsentToken.BYTES);
            int st = Ffi.call(Native15.w4_env_consent, Buf.in(a, bytes), (long) bytes.length, out);
            if (st == ZxvStatus.E_NOT_FOUND) return Optional.empty();
            Ffi.check(st, null);
            return Optional.of(ConsentToken.of(out.toArray(JAVA_BYTE)));
        }
    }

    /**
     * Checks that {@code senderPublicKey} is the sender's (from = its
     * SHA3-256) and that the signature verifies.
     *
     * @throws org.zedec.zxv.VerificationFailedException otherwise
     */
    public void verify(byte[] senderPublicKey) {
        try (Arena a = Arena.ofConfined()) {
            Ffi.check(Ffi.call(Native15.w4_env_verify, Buf.in(a, bytes), (long) bytes.length,
                    Buf.fixed(a, "senderPublicKey", senderPublicKey, Web4.PUBLIC_KEY_BYTES)), null);
        }
    }

    /** Whether {@link #verify} would succeed. */
    public boolean isValid(byte[] senderPublicKey) {
        if (senderPublicKey == null || senderPublicKey.length != Web4.PUBLIC_KEY_BYTES) return false;
        try (Arena a = Arena.ofConfined()) {
            return Ffi.call(Native15.w4_env_verify, Buf.in(a, bytes), (long) bytes.length,
                    Buf.in(a, senderPublicKey)) == Ffi.OK;
        }
    }

    /** Equal when the bytes are equal. */
    @Override
    public boolean equals(Object o) {
        return o instanceof Envelope e && Arrays.equals(bytes, e.bytes);
    }

    /** A hash of the bytes. */
    @Override
    public int hashCode() {
        return Arrays.hashCode(bytes);
    }

    /** Kind, tool, request id and sequence (never the payload). */
    @Override
    public String toString() {
        return "Envelope[" + kind + ", tool=" + tool + ", req=" + Long.toUnsignedString(requestId) + ", seq="
                + Long.toUnsignedString(seq) + "]";
    }
}
