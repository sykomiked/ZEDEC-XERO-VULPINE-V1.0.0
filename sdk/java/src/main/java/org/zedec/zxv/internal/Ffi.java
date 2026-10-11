// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.internal;

import static java.lang.foreign.ValueLayout.JAVA_LONG;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.invoke.MethodHandle;
import java.nio.charset.StandardCharsets;
import org.zedec.zxv.ZxvContext;

/** Small helpers shared by every wrapper: invoke, check status, strings. Internal. */
public final class Ffi {
    private Ffi() {}

    /** ZXV_OK. */
    public static final int OK = 0;
    /** ZXV_E_BUFFER. */
    public static final int E_BUFFER = -2;

    /** Invokes a downcall that returns int32 status. */
    public static int call(MethodHandle mh, Object... args) {
        try {
            return (int) mh.invokeWithArguments(args);
        } catch (RuntimeException | Error e) {
            throw e;
        } catch (Throwable t) {
            throw new IllegalStateException("native call failed", t);
        }
    }

    /** Invokes a void downcall (the destroy functions). */
    public static void callVoid(MethodHandle mh, Object... args) {
        try {
            mh.invokeWithArguments(args);
        } catch (RuntimeException | Error e) {
            throw e;
        } catch (Throwable t) {
            throw new IllegalStateException("native call failed", t);
        }
    }

    /** Invokes a downcall and maps a negative status to the SDK exception (no context). */
    public static void checked(MethodHandle mh, Object... args) {
        check(call(mh, args), null);
    }

    /** Maps a negative status to an exception; marks {@code ctx} dead on FATAL. */
    public static void check(int status, ZxvContext ctx) {
        if (status == OK) return;
        throw Errors.raise(status, lastError(), ctx);
    }

    /** Copies the calling thread's last error detail. */
    public static String lastError() {
        try (Arena a = Arena.ofConfined()) {
            MemorySegment len = a.allocate(JAVA_LONG);
            int st = call(Native.last_error, MemorySegment.NULL, 0L, len);
            long n = len.get(JAVA_LONG, 0);
            if (st != E_BUFFER && st != OK) return "";
            if (n == 0) return "";
            MemorySegment buf = a.allocate(n + 1);
            if (call(Native.last_error, buf, n + 1, len) != OK) return "";
            return buf.getString(0, StandardCharsets.UTF_8);
        }
    }

    /** A native function following the R4 two-call text pattern. */
    @FunctionalInterface
    public interface TextCall {
        /** Calls the function with the given buffer, capacity and out-length pointer. */
        int call(MemorySegment buf, long cap, MemorySegment outLen);
    }

    /** Runs the R4 two-call pattern and decodes the UTF-8 result. */
    public static String text(ZxvContext ctx, TextCall f) {
        try (Arena a = Arena.ofConfined()) {
            MemorySegment len = a.allocate(JAVA_LONG);
            int st = f.call(MemorySegment.NULL, 0, len);
            if (st == OK) return "";
            if (st != E_BUFFER) check(st, ctx);
            long n = len.get(JAVA_LONG, 0);
            MemorySegment buf = a.allocate(n + 1);
            check(f.call(buf, n + 1, len), ctx);
            return buf.getString(0, StandardCharsets.UTF_8);
        }
    }

    /** A NUL-terminated UTF-8 copy of {@code s}, or NULL for null. */
    public static MemorySegment cstr(Arena a, String s) {
        return s == null ? MemorySegment.NULL : a.allocateFrom(s, StandardCharsets.UTF_8);
    }

    /** An off-heap copy of {@code bytes} (or NULL for null). */
    public static MemorySegment bytes(Arena a, byte[] bytes) {
        if (bytes == null) return MemorySegment.NULL;
        if (bytes.length == 0) return a.allocate(1).asSlice(0, 0); // a valid, non-NULL, empty buffer
        return a.allocateFrom(java.lang.foreign.ValueLayout.JAVA_BYTE, bytes);
    }

    /** Requires an exact length for a fixed-size byte argument. */
    public static void requireLength(String what, byte[] b, long n) {
        if (b == null) throw new NullPointerException(what);
        if (b.length != n) throw Errors.usage(-1, what + " must be " + n + " bytes, got " + b.length);
    }

    /** Requires an exact length for a fixed-size segment argument. */
    public static void requireLength(String what, MemorySegment b, long n) {
        if (b == null) throw new NullPointerException(what);
        if (b.byteSize() != n) throw Errors.usage(-1, what + " must be " + n + " bytes, got " + b.byteSize());
    }

    /** Requires a native (off-heap) segment, for calls not linked as critical. */
    public static MemorySegment nativeOrCopy(Arena a, MemorySegment s) {
        if (s == null || s.equals(MemorySegment.NULL)) return MemorySegment.NULL;
        if (s.isNative()) return s;
        MemorySegment c = a.allocate(Math.max(1, s.byteSize()));
        MemorySegment.copy(s, 0, c, 0, s.byteSize());
        return c;
    }
}
