// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.freight;

import static java.lang.foreign.ValueLayout.JAVA_BYTE;
import static java.lang.foreign.ValueLayout.JAVA_INT;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import org.zedec.zxv.internal.Buf;
import org.zedec.zxv.internal.Errors;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Native15;

/**
 * Freight packets: up to 3360 payload bytes as 168 smart packets of 21 bytes
 * (byte 0 the row index, bytes 1..20 row data). Rows 0..k-1 hold the payload
 * and rows k..167 are Cauchy Reed-Solomon parity, so ANY k distinct rows
 * rebuild the payload: k trades payload per freight (k * 20 bytes) against
 * loss tolerance (168 - k rows). A larger payload is split into a set of up
 * to {@link #MAX_SET} freights sharing one id; see {@link FreightJoin}.
 *
 * <p>Pure and thread-safe; no context or capability.
 *
 * <p>Honest limits: erasure coding only. A lost row is recovered; a
 * corrupted row is detected (the payload SHA-256 fails,
 * {@link org.zedec.zxv.VerificationFailedException}) but not located or
 * repaired: drop rows you distrust and decode again. A freight is larger
 * than its payload (3528 wire bytes for at most 3360).
 */
public final class Freight {
    private Freight() {}

    /** Bytes per packet ({@code ZXV_FREIGHT_PACKET_BYTES}). */
    public static final int PACKET_BYTES = 21;
    /** Payload bytes per row ({@code ZXV_FREIGHT_ROW_PAYLOAD}). */
    public static final int ROW_PAYLOAD = 20;
    /** Rows per freight ({@code ZXV_FREIGHT_ROWS}). */
    public static final int ROWS = 168;
    /** One encoded freight ({@code ZXV_FREIGHT_BYTES}, 168 * 21). */
    public static final int BYTES = 3528;
    /** Largest payload of one freight, at k = 168 ({@code ZXV_FREIGHT_MAX_PAYLOAD}). */
    public static final int MAX_PAYLOAD = 3360;
    /** Freights in one set ({@code ZXV_FREIGHT_MAX_SET}). */
    public static final int MAX_SET = 16384;
    /** Largest set payload ({@code ZXV_FREIGHT_SET_MAX_BYTES}). */
    public static final long SET_MAX_BYTES = 55050240L;
    /** Flag: the (set's) payload is an op stream ({@code ZXV_FREIGHT_F_OPSTREAM}). */
    public static final int F_OPSTREAM = 0x01;

    /**
     * One member of a set.
     *
     * @param header  its header
     * @param packets its 168 packets (3528 bytes, rows in order)
     */
    public record Member(FreightHeader header, byte[] packets) {
        /** Packet {@code row} (21 bytes, a copy). */
        public byte[] packet(int row) {
            return Freight.packet(packets, row);
        }
    }

    /**
     * The 168 packets (3528 bytes, rows in order) of the freight
     * {@code header} describes. {@code payload} must be exactly what the
     * header was built for.
     *
     * @throws org.zedec.zxv.VerificationFailedException on a hash mismatch
     */
    public static byte[] encode(FreightHeader header, byte[] payload) {
        Buf.nonNull("header", header);
        Buf.nonNull("payload", payload);
        try (Arena a = Arena.ofConfined()) {
            MemorySegment out = a.allocate(BYTES);
            Ffi.check(Ffi.call(Native15.freight_encode, Buf.in(a, header.toBytes()), Buf.in(a, payload),
                    (long) payload.length, out), null);
            return out.toArray(JAVA_BYTE);
        }
    }

    /**
     * Rebuilds the payload from whichever packets arrived: 21 bytes each, in
     * any order; duplicates and row indices above 167 are ignored.
     *
     * @throws ErasureException                           with fewer than k distinct rows
     * @throws org.zedec.zxv.VerificationFailedException when the rebuilt payload fails the header's SHA-256
     */
    public static byte[] decode(FreightHeader header, byte[] packets) {
        Buf.nonNull("header", header);
        Buf.nonNull("packets", packets);
        try (Arena a = Arena.ofConfined()) {
            MemorySegment h = Buf.in(a, header.toBytes()), p = Buf.in(a, packets);
            long n = packets.length;
            return Buf.r4(null, header.payloadLength(),
                    (buf, cap, len) -> Ffi.call(Native15.freight_decode, h, p, n, buf, cap, len));
        }
    }

    /** Joins packets (each 21 bytes) into one buffer for {@link #decode}. */
    public static byte[] packets(byte[]... packets) {
        Buf.nonNull("packets", packets);
        byte[] out = new byte[packets.length * PACKET_BYTES];
        for (int i = 0; i < packets.length; i++) {
            Ffi.requireLength("packet", packets[i], PACKET_BYTES);
            System.arraycopy(packets[i], 0, out, i * PACKET_BYTES, PACKET_BYTES);
        }
        return out;
    }

    /** Packet {@code row} of an encoded freight (21 bytes, a copy). */
    public static byte[] packet(byte[] encoded, int row) {
        Ffi.requireLength("encoded", encoded, BYTES);
        if (row < 0 || row >= ROWS) throw Errors.usage(org.zedec.zxv.ZxvStatus.E_RANGE, "row " + row);
        byte[] p = new byte[PACKET_BYTES];
        System.arraycopy(encoded, row * PACKET_BYTES, p, 0, PACKET_BYTES);
        return p;
    }

    /**
     * Freights needed to carry {@code length} bytes at {@code k} (0 bytes still needs one).
     *
     * @throws org.zedec.zxv.ZxvUsageException ZXV_E_TOO_LONG when more than {@link #MAX_SET} would be needed
     */
    public static int setCount(long length, int k) {
        try (Arena a = Arena.ofConfined()) {
            MemorySegment n = a.allocate(JAVA_INT);
            Ffi.check(Ffi.call(Native15.freight_set_count, length, Buf.u8("k", k), n), null);
            return n.get(JAVA_INT, 0);
        }
    }

    /**
     * Member {@code seq} of the set carrying {@code data}: its header and packets.
     *
     * @throws org.zedec.zxv.ZxvUsageException ZXV_E_RANGE when {@code seq} is not below the set count
     */
    public static Member split(long freightId, int k, int flags, long expandLimit, byte[] data, int seq) {
        Buf.nonNull("data", data);
        try (Arena a = Arena.ofConfined()) {
            MemorySegment h = a.allocate(FreightHeader.BYTES), p = a.allocate(BYTES);
            Ffi.check(Ffi.call(Native15.freight_split, freightId, Buf.u8("k", k), Buf.u8("flags", flags), expandLimit,
                    Buf.in(a, data), (long) data.length, seq, h, p), null);
            return new Member(FreightHeader.parse(h.toArray(JAVA_BYTE)), p.toArray(JAVA_BYTE));
        }
    }
}
