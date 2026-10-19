// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.hw;

import static java.lang.foreign.ValueLayout.JAVA_BYTE;
import static java.lang.foreign.ValueLayout.JAVA_INT;
import static java.lang.foreign.ValueLayout.JAVA_LONG;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.invoke.MethodHandle;
import java.util.Arrays;
import org.zedec.zxv.ZxvContext;
import org.zedec.zxv.ZxvException;
import org.zedec.zxv.internal.Ffi;

/**
 * Shared plumbing for the hardware wrappers: R4 byte reads, small out
 * parameters, and the failure channel of the FFM upcalls (provider and
 * device callbacks). An upcall never lets an exception cross into C: a
 * {@link ZxvException} becomes its status, anything else becomes
 * {@code ZXV_E_INTERNAL} and is rethrown from the native call that ran the
 * callback (calls are synchronous, so it is the same thread). Internal.
 */
final class Upcalls {
    private Upcalls() {}

    static final int E_ARG = -1;
    static final int E_BUFFER = -2;
    static final int E_NOT_FOUND = -4;
    static final int E_INTERNAL = -99;

    private static final ThreadLocal<Throwable> FAILURE = new ThreadLocal<>();

    /** Converts a callback failure to a status; never throws. */
    static int fail(Throwable t) {
        try {
            if (t instanceof ZxvException z && z.status() < 0) return z.status();
            if (FAILURE.get() == null) FAILURE.set(t);
        } catch (Throwable ignored) {
            // Nothing may escape into C.
        }
        return E_INTERNAL;
    }

    /** Clears the failure channel before a native call that may run callbacks. */
    static void clear() {
        FAILURE.remove();
    }

    /**
     * Maps the status of a native call that may have run callbacks: a callback
     * failure is rethrown as itself when the call failed; otherwise the usual
     * status mapping applies.
     */
    static void check(int status, ZxvContext ctx) {
        Throwable t = FAILURE.get();
        FAILURE.remove();
        if (status != Ffi.OK && t != null) {
            if (t instanceof RuntimeException re) throw re;
            if (t instanceof Error er) throw er;
            throw new IllegalStateException("hardware callback failed", t);
        }
        Ffi.check(status, ctx);
    }

    /** Input bytes handed to a callback (the pointer may be NULL when empty). */
    static byte[] in(MemorySegment p, long len) {
        if (len <= 0) return new byte[0];
        return p.reinterpret(len).toArray(JAVA_BYTE);
    }

    /** Reads an R4 byte result: ZXV_E_BUFFER with the needed length, then the bytes. */
    static byte[] bytes(ZxvContext ctx, Ffi.TextCall f) {
        try (Arena a = Arena.ofConfined()) {
            MemorySegment len = a.allocate(JAVA_LONG);
            int st = f.call(MemorySegment.NULL, 0, len);
            long n = len.get(JAVA_LONG, 0);
            if (st == Ffi.OK && n == 0) return new byte[0];
            if (st != Ffi.OK && st != Ffi.E_BUFFER) Ffi.check(st, ctx);
            MemorySegment buf = a.allocate(Math.max(1, n));
            Ffi.check(f.call(buf, n, len), ctx);
            return buf.asSlice(0, len.get(JAVA_LONG, 0)).toArray(JAVA_BYTE);
        }
    }

    /** A uint32 / int32 out parameter read as int bits. */
    static int int32(Arena a, MethodHandle mh, ZxvContext ctx, Object... head) {
        MemorySegment out = a.allocate(JAVA_INT);
        Object[] all = Arrays.copyOf(head, head.length + 1);
        all[head.length] = out;
        Ffi.check(Ffi.call(mh, all), ctx);
        return out.get(JAVA_INT, 0);
    }

    /** Requires a uint32 range for an int argument given as a long. */
    static int u32(String what, long v) {
        if (v < 0 || v > 0xFFFFFFFFL) {
            throw new org.zedec.zxv.ZxvUsageException(-8, "ZXV_E_RANGE", what + " " + v + " is not a uint32");
        }
        return (int) v;
    }
}
