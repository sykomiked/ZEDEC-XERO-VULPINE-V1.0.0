// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.freight;

import static java.lang.foreign.ValueLayout.JAVA_BYTE;
import static java.lang.foreign.ValueLayout.JAVA_LONG;
import static java.lang.foreign.ValueLayout.JAVA_SHORT;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.util.Arrays;
import java.util.HexFormat;
import org.zedec.zxv.internal.Buf;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Native15;

/**
 * The 64-byte freight header, which travels apart from the packets (in the
 * outer message): "ZXFH", version, k, flags, payload length, seq,
 * seq_count, freight id, expand limit and the payload's SHA-256. Immutable;
 * always valid (every factory checks it in libzxv).
 *
 * <p>Honest limits: the header is not protected by the erasure code and is
 * not authenticated. Sign it, or the outer message, if the sender matters.
 */
public final class FreightHeader {
    /** Serialized size ({@code ZXV_FREIGHT_HEADER_BYTES}). */
    public static final int BYTES = 64;

    private final byte[] bytes;
    private final int version, k, flags, payloadLength, seq, seqCount;
    private final long freightId, expandLimit;
    private final byte[] sha256;

    private FreightHeader(byte[] bytes, MemorySegment v, MemorySegment k, MemorySegment f, MemorySegment pl,
            MemorySegment s, MemorySegment sc, MemorySegment id, MemorySegment el, MemorySegment sha) {
        this.bytes = bytes;
        this.version = Byte.toUnsignedInt(v.get(JAVA_BYTE, 0));
        this.k = Byte.toUnsignedInt(k.get(JAVA_BYTE, 0));
        this.flags = Byte.toUnsignedInt(f.get(JAVA_BYTE, 0));
        this.payloadLength = Short.toUnsignedInt(pl.get(JAVA_SHORT, 0));
        this.seq = Short.toUnsignedInt(s.get(JAVA_SHORT, 0));
        this.seqCount = Short.toUnsignedInt(sc.get(JAVA_SHORT, 0));
        this.freightId = id.get(JAVA_LONG, 0);
        this.expandLimit = el.get(JAVA_LONG, 0);
        this.sha256 = sha.toArray(JAVA_BYTE);
    }

    /**
     * Parses and checks a serialized header.
     *
     * @throws org.zedec.zxv.ZxvUsageException ZXV_E_FORMAT for bad magic, reserved bytes or fields
     */
    public static FreightHeader parse(byte[] header) {
        byte[] h = Buf.nonNull("header", header).clone();
        try (Arena a = Arena.ofConfined()) {
            MemorySegment hs = Buf.fixed(a, "header", h, BYTES);
            MemorySegment v = a.allocate(1), k = a.allocate(1), f = a.allocate(1), pl = a.allocate(JAVA_SHORT),
                    s = a.allocate(JAVA_SHORT), sc = a.allocate(JAVA_SHORT), id = a.allocate(JAVA_LONG),
                    el = a.allocate(JAVA_LONG), sha = a.allocate(32);
            Ffi.check(Ffi.call(Native15.freight_header_parse, hs, v, k, f, pl, s, sc, id, el, sha), null);
            return new FreightHeader(h, v, k, f, pl, s, sc, id, el, sha);
        }
    }

    /** Whether {@code header} is a well-formed serialized header. */
    public static boolean isValid(byte[] header) {
        if (header == null || header.length != BYTES) return false;
        try (Arena a = Arena.ofConfined()) {
            return Ffi.call(Native15.freight_header_check, Buf.in(a, header)) == Ffi.OK;
        }
    }

    /**
     * The header of a single freight (seq 0 of 1) carrying {@code payload}; it hashes the payload.
     *
     * @param freightId   the id (unsigned)
     * @param k           data rows, 1..168
     * @param flags       0 or {@link Freight#F_OPSTREAM}
     * @param expandLimit 0 unless {@code flags} has {@link Freight#F_OPSTREAM} (unsigned)
     * @param payload     at most {@code k * 20} bytes
     * @throws org.zedec.zxv.ZxvUsageException ZXV_E_TOO_LONG for a payload over {@code k * 20} bytes
     */
    public static FreightHeader forPayload(long freightId, int k, int flags, long expandLimit, byte[] payload) {
        Buf.nonNull("payload", payload);
        try (Arena a = Arena.ofConfined()) {
            MemorySegment out = a.allocate(BYTES);
            Ffi.check(Ffi.call(Native15.freight_header_init, freightId, Buf.u8("k", k), Buf.u8("flags", flags),
                    expandLimit, Buf.in(a, payload), (long) payload.length, out), null);
            return parse(out.toArray(JAVA_BYTE));
        }
    }

    /**
     * A header from its fields (for a set member, or one received field by field).
     *
     * @throws org.zedec.zxv.ZxvUsageException ZXV_E_ARG for any field that fails the check
     */
    public static FreightHeader of(int k, int flags, int payloadLength, int seq, int seqCount, long freightId,
            long expandLimit, byte[] payloadSha256) {
        try (Arena a = Arena.ofConfined()) {
            MemorySegment out = a.allocate(BYTES);
            Ffi.check(Ffi.call(Native15.freight_header_serialize, Buf.u8("k", k), Buf.u8("flags", flags),
                    Buf.u16("payloadLength", payloadLength), Buf.u16("seq", seq), Buf.u16("seqCount", seqCount),
                    freightId, expandLimit, Buf.fixed(a, "payloadSha256", payloadSha256, 32), out), null);
            return parse(out.toArray(JAVA_BYTE));
        }
    }

    /** The 64 serialized bytes (a copy). */
    public byte[] toBytes() {
        return bytes.clone();
    }

    /** The format version (1). */
    public int version() {
        return version;
    }

    /** Data rows, 1..168: any k distinct rows rebuild the payload. */
    public int k() {
        return k;
    }

    /** The flags (0 or {@link Freight#F_OPSTREAM}). */
    public int flags() {
        return flags;
    }

    /** Whether the (set's) payload is an op stream. */
    public boolean isOpStream() {
        return (flags & Freight.F_OPSTREAM) != 0;
    }

    /** This freight's payload length. */
    public int payloadLength() {
        return payloadLength;
    }

    /** This member's index in its set. */
    public int seq() {
        return seq;
    }

    /** Freights in the set, 1..16384. */
    public int seqCount() {
        return seqCount;
    }

    /** The id the set's members share (unsigned). */
    public long freightId() {
        return freightId;
    }

    /** The most bytes an op stream payload may expand to (unsigned; 0 without the flag). */
    public long expandLimit() {
        return expandLimit;
    }

    /** The SHA-256 of this freight's payload (a copy). */
    public byte[] payloadSha256() {
        return sha256.clone();
    }

    /** Equal when the serialized bytes are equal. */
    @Override
    public boolean equals(Object o) {
        return o instanceof FreightHeader h && Arrays.equals(bytes, h.bytes);
    }

    /** A hash of the serialized bytes. */
    @Override
    public int hashCode() {
        return Arrays.hashCode(bytes);
    }

    /** The serialized bytes in hex. */
    @Override
    public String toString() {
        return "FreightHeader[" + HexFormat.of().formatHex(bytes) + "]";
    }
}
