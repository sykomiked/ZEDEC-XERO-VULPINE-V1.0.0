// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.freight;

import static java.lang.foreign.ValueLayout.ADDRESS;
import static java.lang.foreign.ValueLayout.JAVA_BYTE;
import static java.lang.foreign.ValueLayout.JAVA_INT;
import static java.lang.foreign.ValueLayout.JAVA_LONG;

import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemorySegment;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import org.zedec.zxv.internal.Buf;
import org.zedec.zxv.internal.Errors;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Native15;

/**
 * Op streams: a "ZXOP" header, then LITERAL, COPY (an LZ77-style back
 * reference), CIDREF (bytes the receiver resolves by content id) and SEED (a
 * built-in generator) ops that the receiver expands. Expansion checks the
 * declared total against the caller's limit before writing a byte, so a
 * stream cannot expand past what the receiver allowed.
 *
 * <p>Pure and thread-safe; no context or capability.
 *
 * <p>Honest limits: no size guarantee. Random data encodes slightly larger
 * than itself, COPY helps only data that repeats, CIDREF only names bytes
 * the receiver already holds or fetches, and SEED only reproduces the
 * built-in generators (no encoder searches for seeds). No information is
 * created.
 */
public final class OpStream {
    private OpStream() {}

    /** Largest stream or expansion handled here ({@code ZXV_FREIGHT_OPS_MAX}, 64 MiB). */
    public static final long OPS_MAX = 67108864L;
    /** Longest content id in a CIDREF ({@code ZXV_FREIGHT_CID_MAX}). */
    public static final int CID_MAX = 128;
    /** Longest SEED params ({@code ZXV_FREIGHT_GEN_PARAM_MAX}). */
    public static final int GEN_PARAM_MAX = 32;

    /**
     * A stream's header.
     *
     * @param total  the declared output length (unsigned)
     * @param hasSha whether it carries the output's SHA-256
     */
    public record Peek(long total, boolean hasSha) {}

    /**
     * Reads a stream's declared total without expanding it.
     *
     * @throws org.zedec.zxv.ZxvUsageException ZXV_E_FORMAT for a bad header
     */
    public static Peek peek(byte[] ops) {
        Buf.nonNull("ops", ops);
        try (Arena a = Arena.ofConfined()) {
            MemorySegment t = a.allocate(JAVA_LONG), s = a.allocate(JAVA_INT);
            Ffi.check(Ffi.call(Native15.freight_ops_peek, Buf.in(a, ops), (long) ops.length, t, s), null);
            return new Peek(t.get(JAVA_LONG, 0), s.get(JAVA_INT, 0) != 0);
        }
    }

    /** Expands a stream with no resolver (a CIDREF then fails with ZXV_E_NOT_FOUND). */
    public static byte[] expand(byte[] ops, long limit) {
        return expand(ops, limit, null);
    }

    /**
     * Expands a stream.
     *
     * @param limit    the most bytes the caller accepts (at most {@link #OPS_MAX})
     * @param resolver supplies CIDREF bytes, or null
     * @throws org.zedec.zxv.ZxvUsageException          ZXV_E_TOO_LONG when the declared total is above {@code limit};
     *                                                  ZXV_E_FORMAT for a malformed stream; ZXV_E_NOT_FOUND
     *                                                  for an unresolved CIDREF
     * @throws org.zedec.zxv.VerificationFailedException when the output fails the stream's SHA-256
     */
    public static byte[] expand(byte[] ops, long limit, CidResolver resolver) {
        Buf.nonNull("ops", ops);
        if (ACTIVE.get() != null) throw new IllegalStateException("a CidResolver must not expand another stream");
        long hint;
        try {
            Peek p = peek(ops);
            hint = Long.compareUnsigned(p.total(), Math.min(Math.max(limit, 0), OPS_MAX)) <= 0 ? p.total() : 0;
        } catch (RuntimeException e) {
            hint = 0; // the expansion reports the real error
        }
        long h = hint;
        try (Arena a = Arena.ofConfined()) {
            MemorySegment o = Buf.in(a, ops);
            MemorySegment fn = resolver == null ? MemorySegment.NULL : RESOLVE_STUB;
            if (resolver != null) ACTIVE.set(new Call(resolver));
            try {
                return Buf.r4(null, h, (buf, cap, len) -> {
                    int st = Ffi.call(Native15.freight_ops_expand, o, (long) ops.length, limit, fn,
                            MemorySegment.NULL, buf, cap, len);
                    Call c = ACTIVE.get();
                    if (c != null && c.failure != null && st != Ffi.OK) {
                        Throwable t = c.failure;
                        if (t instanceof RuntimeException re) throw re;
                        if (t instanceof Error er) throw er;
                        throw new IllegalStateException("CidResolver failed", t);
                    }
                    return st;
                });
            } finally {
                ACTIVE.remove();
            }
        }
    }

    /**
     * Encodes {@code input} as LITERAL/COPY ops (greedy LZ77, deterministic).
     * The result may be larger than the input.
     *
     * @param withSha whether to carry the output's SHA-256
     */
    public static byte[] encode(byte[] input, boolean withSha) {
        Buf.nonNull("input", input);
        try (Arena a = Arena.ofConfined()) {
            MemorySegment in = Buf.in(a, input);
            long hint = Math.min(bound(input.length, withSha), 1L << 20);
            return Buf.r4(null, hint, (buf, cap, len) -> Ffi.call(Native15.freight_ops_encode, in,
                    (long) input.length, withSha ? 1 : 0, buf, cap, len));
        }
    }

    /** The worst-case encoded size of {@code n} input bytes (all literals). */
    public static long bound(long n, boolean withSha) {
        try (Arena a = Arena.ofConfined()) {
            MemorySegment b = a.allocate(JAVA_LONG);
            Ffi.check(Ffi.call(Native15.freight_ops_bound, n, withSha ? 1 : 0, b), null);
            return b.get(JAVA_LONG, 0);
        }
    }

    /**
     * Runs one SEED generator directly.
     *
     * @param params the generator's params (see {@link Generator}); at most 32 bytes
     * @param length bytes to produce
     * @throws org.zedec.zxv.ZxvUsageException ZXV_E_ARG for params the generator refuses
     */
    public static byte[] generate(Generator generator, byte[] params, int length) {
        Buf.nonNull("generator", generator);
        Buf.index("length", length);
        try (Arena a = Arena.ofConfined()) {
            MemorySegment out = a.allocate(Math.max(1, length));
            long pl = params == null ? 0 : params.length;
            Ffi.check(Ffi.call(Native15.freight_gen_run, generator.code(), Buf.in(a, params), pl, out, (long) length),
                    null);
            return Buf.read(out, length);
        }
    }

    // ---- the CIDREF upcall

    private static final class Call {
        final CidResolver resolver;
        Throwable failure;

        Call(CidResolver r) {
            this.resolver = r;
        }
    }

    private static final ThreadLocal<Call> ACTIVE = new ThreadLocal<>();
    private static final int E_INTERNAL = -99;
    private static final MemorySegment RESOLVE_STUB;

    static {
        try {
            var mh = MethodHandles.lookup().findStatic(OpStream.class, "resolveUpcall",
                    MethodType.methodType(int.class, MemorySegment.class, MemorySegment.class, long.class,
                            MemorySegment.class, long.class));
            RESOLVE_STUB = Linker.nativeLinker().upcallStub(mh,
                    FunctionDescriptor.of(JAVA_INT, ADDRESS, ADDRESS, JAVA_LONG, ADDRESS, JAVA_LONG), Arena.global());
        } catch (ReflectiveOperationException e) {
            throw new ExceptionInInitializerError(e);
        }
    }

    /** Runs the active resolver; never throws into C. */
    private static int resolveUpcall(MemorySegment user, MemorySegment cid, long cidLen, MemorySegment out, long len) {
        Call c = ACTIVE.get();
        if (c == null) return E_INTERNAL;
        try {
            byte[] id = cidLen <= 0 ? new byte[0] : cid.reinterpret(cidLen).toArray(JAVA_BYTE);
            byte[] data = c.resolver.resolve(id, len);
            if (data == null) return org.zedec.zxv.ZxvStatus.E_NOT_FOUND;
            if (data.length != len) {
                c.failure = Errors.usage(org.zedec.zxv.ZxvStatus.E_ARG,
                        "CidResolver returned " + data.length + " bytes, the op needs " + len);
                return E_INTERNAL;
            }
            if (len > 0) MemorySegment.copy(data, 0, out.reinterpret(len), JAVA_BYTE, 0, data.length);
            return Ffi.OK;
        } catch (Throwable t) {
            try {
                if (c.failure == null) c.failure = t;
            } catch (Throwable ignored) {
                // Nothing may escape into C.
            }
            return E_INTERNAL;
        }
    }
}
