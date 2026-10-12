// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.coin;

import static java.lang.foreign.ValueLayout.JAVA_BYTE;
import static java.lang.foreign.ValueLayout.JAVA_INT;
import static java.lang.foreign.ValueLayout.JAVA_LONG;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import org.zedec.zxv.internal.Buf;
import org.zedec.zxv.internal.Errors;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Native15;

/**
 * The Fibonacci code of token amounts: the Zeckendorf bits of value+1,
 * lowest first, then a closing 1, so every code ends in "11" and needs no
 * length field. Bit i of a buffer is bit {@code i % 8} of byte {@code i / 8}.
 * Values are unsigned 64-bit; {@code 0xFFFFFFFFFFFFFFFF} is not encodable.
 * Pure and thread-safe.
 */
public final class FibonacciCode {
    private FibonacciCode() {}

    /** Longest code of one amount, in bits ({@code ZXV_TOKEN_CODE_MAX_BITS}). */
    public static final int MAX_BITS = 93;
    /** Largest buffer the code functions take ({@code ZXV_TOKEN_CODE_MAX_BYTES}). */
    public static final int MAX_BYTES = 536870911;

    /**
     * Bits written and the buffer holding them.
     *
     * @param bytes the buffer, {@code ceil(bits / 8)} bytes, unused high bits zero
     * @param bits  the number of bits written
     */
    public record Encoded(byte[] bytes, long bits) {}

    /**
     * One value read and where the next code starts.
     *
     * @param value   the value (unsigned)
     * @param nextBit the bit just past this code
     */
    public record Decoded(long value, long nextBit) {}

    /**
     * The length in bits (2..93) of the code of {@code value}.
     *
     * @throws org.zedec.zxv.ZxvUsageException ZXV_E_RANGE for {@code 0xFFFFFFFFFFFFFFFF}
     */
    public static int bits(long value) {
        try (Arena a = Arena.ofConfined()) {
            MemorySegment b = a.allocate(JAVA_INT);
            Ffi.check(Ffi.call(Native15.token_code_bits, value, b), null);
            return b.get(JAVA_INT, 0);
        }
    }

    /** The codes of {@code values}, back to back, in the smallest buffer that holds them. */
    public static Encoded encode(long... values) {
        Buf.nonNull("values", values);
        long total = 0;
        for (long v : values) total += bits(v);
        if ((total + 7) / 8 > MAX_BYTES) throw Errors.usage(org.zedec.zxv.ZxvStatus.E_TOO_LONG, "codes too long");
        byte[] buf = new byte[(int) ((total + 7) / 8)];
        long bit = 0;
        for (long v : values) bit = put(buf, bit, v);
        return new Encoded(buf, bit);
    }

    /**
     * Writes the code of {@code value} into {@code buf} at {@code bit}; only
     * the code's bits change.
     *
     * @return the bit just past the code
     * @throws org.zedec.zxv.ZxvUsageException ZXV_E_BUFFER when the code does not fit (nothing written)
     */
    public static long put(byte[] buf, long bit, long value) {
        Buf.nonNull("buf", buf);
        try (Arena a = Arena.ofConfined()) {
            MemorySegment b = a.allocate(Math.max(1, buf.length));
            MemorySegment.copy(buf, 0, b, JAVA_BYTE, 0, buf.length);
            MemorySegment pos = a.allocate(JAVA_INT);
            pos.set(JAVA_INT, 0, bitArg(bit));
            Ffi.check(Ffi.call(Native15.token_code_put, b, (long) buf.length, pos, value), null);
            MemorySegment.copy(b, JAVA_BYTE, 0, buf, 0, buf.length);
            return Integer.toUnsignedLong(pos.get(JAVA_INT, 0));
        }
    }

    /**
     * Reads one code from {@code buf} at {@code bit}.
     *
     * @throws org.zedec.zxv.ZxvUsageException ZXV_E_FORMAT when there is no closing "11" before the end,
     *         ZXV_E_RANGE when {@code bit} is past the end
     */
    public static Decoded get(byte[] buf, long bit) {
        Buf.nonNull("buf", buf);
        try (Arena a = Arena.ofConfined()) {
            MemorySegment pos = a.allocate(JAVA_INT), v = a.allocate(JAVA_LONG);
            pos.set(JAVA_INT, 0, bitArg(bit));
            Ffi.check(Ffi.call(Native15.token_code_get, Buf.in(a, buf), (long) buf.length, pos, v), null);
            return new Decoded(v.get(JAVA_LONG, 0), Integer.toUnsignedLong(pos.get(JAVA_INT, 0)));
        }
    }

    /** Reads {@code count} codes from the start of {@code buf}. */
    public static long[] decode(byte[] buf, int count) {
        Buf.index("count", count);
        long[] out = new long[count];
        long bit = 0;
        for (int i = 0; i < count; i++) {
            Decoded d = get(buf, bit);
            out[i] = d.value();
            bit = d.nextBit();
        }
        return out;
    }

    private static int bitArg(long bit) {
        if (bit < 0 || bit > 0xFFFFFFFFL) throw Errors.usage(org.zedec.zxv.ZxvStatus.E_RANGE, "bit " + bit + " out of range");
        return (int) bit;
    }
}
