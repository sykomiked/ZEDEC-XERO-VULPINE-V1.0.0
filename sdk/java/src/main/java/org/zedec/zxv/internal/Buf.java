// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.internal;

import static java.lang.foreign.ValueLayout.ADDRESS;
import static java.lang.foreign.ValueLayout.JAVA_BYTE;
import static java.lang.foreign.ValueLayout.JAVA_LONG;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.invoke.MethodHandle;
import java.util.Arrays;
import org.zedec.zxv.ZxvContext;

/**
 * Byte-buffer plumbing for the ABI 1.5 wrappers: the R4 two-call pattern for
 * BYTE results ("R4 for bytes": no NUL, {@code *out_len} always set, and
 * {@code ZXV_E_BUFFER} when the buffer is NULL or too small), fixed-size
 * arguments, narrow integers and handle creation. Internal.
 */
public final class Buf {
    private Buf() {}

    /** Most attempts of {@link #r4}: a result never grows between calls, so two are enough. */
    private static final int ATTEMPTS = 3;

    /**
     * Runs an R4 byte call: first with a buffer of {@code hint} bytes (so a
     * result of known maximum size takes one call), then, on
     * {@code ZXV_E_BUFFER}, again with exactly the length libzxv reported.
     *
     * @param ctx  the context to mark on a FATAL status, or null
     * @param hint the first buffer's size (0 is allowed: a 1-byte buffer is used)
     * @param f    the call
     * @return the result bytes
     */
    public static byte[] r4(ZxvContext ctx, long hint, Ffi.TextCall f) {
        try (Arena a = Arena.ofConfined()) {
            MemorySegment len = a.allocate(JAVA_LONG);
            long cap = Math.max(1, hint);
            MemorySegment buf = a.allocate(cap);
            int st = f.call(buf, cap, len);
            for (int i = 1; st == Ffi.E_BUFFER && i < ATTEMPTS; i++) {
                cap = Math.max(1, len.get(JAVA_LONG, 0));
                buf = a.allocate(cap);
                st = f.call(buf, cap, len);
            }
            Ffi.check(st, ctx);
            long n = len.get(JAVA_LONG, 0);
            return n == 0 ? new byte[0] : buf.asSlice(0, n).toArray(JAVA_BYTE);
        }
    }

    /**
     * Calls a {@code create(..., T **out)} function and returns the new handle.
     *
     * @param ctx  the context to mark on a FATAL status, or null
     * @param mh   the create function
     * @param head every argument before the out pointer
     */
    public static MemorySegment create(ZxvContext ctx, MethodHandle mh, Object... head) {
        try (Arena a = Arena.ofConfined()) {
            MemorySegment out = a.allocate(ADDRESS);
            Object[] all = Arrays.copyOf(head, head.length + 1);
            all[head.length] = out;
            Ffi.check(Ffi.call(mh, all), ctx);
            return out.get(ADDRESS, 0);
        }
    }

    /** An off-heap copy of {@code b}; NULL for null, a valid empty buffer for an empty array. */
    public static MemorySegment in(Arena a, byte[] b) {
        return Ffi.bytes(a, b);
    }

    /** An off-heap copy of a fixed-size argument, after checking its length (else ZXV_E_ARG). */
    public static MemorySegment fixed(Arena a, String what, byte[] b, int n) {
        Ffi.requireLength(what, b, n);
        return Ffi.bytes(a, b);
    }

    /** Like {@link #fixed}, but null passes NULL (for optional fixed-size arguments). */
    public static MemorySegment fixedOrNull(Arena a, String what, byte[] b, int n) {
        return b == null ? MemorySegment.NULL : fixed(a, what, b, n);
    }

    /** A copy of the first {@code n} bytes of {@code s}. */
    public static byte[] read(MemorySegment s, long n) {
        return n == 0 ? new byte[0] : s.asSlice(0, n).toArray(JAVA_BYTE);
    }

    /** A {@code uint8_t} argument passed as an int 0..255 (else ZXV_E_ARG). */
    public static int u8(String what, int v) {
        if (v < 0 || v > 0xFF) throw Errors.usage(-1, what + " " + v + " is not 0..255");
        return v;
    }

    /** A {@code uint16_t} argument passed as an int 0..65535 (else ZXV_E_ARG). */
    public static int u16(String what, int v) {
        if (v < 0 || v > 0xFFFF) throw Errors.usage(-1, what + " " + v + " is not 0..65535");
        return v;
    }

    /** A {@code uint32_t} count or index that must not be negative in Java (else ZXV_E_ARG). */
    public static int index(String what, int v) {
        if (v < 0) throw Errors.usage(-1, what + " " + v + " is negative");
        return v;
    }

    /** Requires a non-null argument (a NULL pointer in C is ZXV_E_ARG). */
    public static <T> T nonNull(String what, T v) {
        if (v == null) throw Errors.usage(-1, what + " must not be null");
        return v;
    }
}
