// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.freight;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import org.zedec.zxv.internal.Buf;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Lease;
import org.zedec.zxv.internal.Native15;
import org.zedec.zxv.internal.NativeResource;

/**
 * Builds an op stream op by op ({@code zxv_freight_ops_writer}). The first
 * failed put is sticky: every later put and {@link #finish()} fail with the
 * same status. Holds only its own memory: no context and no capability.
 *
 * <p>Threading: the C writer is not thread-safe, so every method here is
 * {@code synchronized}.
 */
public final class OpStreamWriter implements AutoCloseable {
    private final NativeResource res;

    private OpStreamWriter(MemorySegment h) {
        this.res = new NativeResource("op stream writer", h, x -> Ffi.callVoid(Native15.freight_ops_writer_destroy, x),
                null, null);
    }

    /**
     * A writer.
     *
     * @param declaredTotal what the ops must produce in all (unsigned)
     * @param sha256        the SHA-256 of that output, which expansion will verify, or null
     * @param maxBytes      the writer's buffer: 14 (46 with a SHA-256) .. {@link OpStream#OPS_MAX}
     */
    public static OpStreamWriter create(long declaredTotal, byte[] sha256, long maxBytes) {
        try (Arena a = Arena.ofConfined()) {
            return new OpStreamWriter(Buf.create(null, Native15.freight_ops_writer_create, declaredTotal,
                    Buf.fixedOrNull(a, "sha256", sha256, 32), maxBytes));
        }
    }

    /**
     * Appends a LITERAL op.
     *
     * @throws org.zedec.zxv.ZxvUsageException  ZXV_E_ARG for an empty run or one past the declared total
     * @throws org.zedec.zxv.ZxvRefusedException ZXV_E_CAPACITY when the writer's buffer is full
     */
    public synchronized OpStreamWriter literal(byte[] bytes) {
        Buf.nonNull("bytes", bytes);
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            Ffi.check(Ffi.call(Native15.freight_ops_put_literal, l.h(0), Buf.in(a, bytes), (long) bytes.length), null);
        }
        return this;
    }

    /** Appends a COPY op: {@code length} bytes from {@code distance} back (overlap allowed). */
    public synchronized OpStreamWriter copy(long distance, long length) {
        try (Lease l = Lease.of(res)) {
            Ffi.check(Ffi.call(Native15.freight_ops_put_copy, l.h(0), distance, length), null);
        }
        return this;
    }

    /** Appends a CIDREF op: {@code length} bytes the receiver resolves by content id (1..128 bytes). */
    public synchronized OpStreamWriter cidRef(byte[] cid, long length) {
        Buf.nonNull("cid", cid);
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            Ffi.check(Ffi.call(Native15.freight_ops_put_cidref, l.h(0), Buf.in(a, cid), (long) cid.length, length),
                    null);
        }
        return this;
    }

    /** Appends a SEED op: {@code length} bytes of a built-in generator. */
    public synchronized OpStreamWriter seed(Generator generator, byte[] params, long length) {
        Buf.nonNull("generator", generator);
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            long pl = params == null ? 0 : params.length;
            Ffi.check(Ffi.call(Native15.freight_ops_put_seed, l.h(0), generator.code(), Buf.in(a, params), pl, length),
                    null);
        }
        return this;
    }

    /**
     * The finished stream. The writer stays usable (finish again after more puts).
     *
     * @throws org.zedec.zxv.ZxvRefusedException ZXV_E_STATE when the ops produce fewer bytes than declared,
     *                                           or the sticky error of an earlier put
     */
    public synchronized byte[] finish() {
        try (Lease l = Lease.of(res)) {
            MemorySegment h = l.h(0);
            return Buf.r4(null, 4096, (buf, cap, len) -> Ffi.call(Native15.freight_ops_writer_finish, h, buf, cap, len));
        }
    }

    /** Whether {@link #close()} has run. */
    public boolean isClosed() {
        return res.isClosed();
    }

    /** Frees the writer. Idempotent. */
    @Override
    public synchronized void close() {
        res.close();
    }
}
