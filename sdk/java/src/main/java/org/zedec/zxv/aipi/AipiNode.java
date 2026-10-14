// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.aipi;

import static java.lang.foreign.ValueLayout.JAVA_INT;
import static java.lang.foreign.ValueLayout.JAVA_LONG;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import org.zedec.zxv.Capability;
import org.zedec.zxv.ZxvContext;
import org.zedec.zxv.cid.Cid;
import org.zedec.zxv.internal.Buf;
import org.zedec.zxv.internal.Caps;
import org.zedec.zxv.internal.Errors;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Lease;
import org.zedec.zxv.internal.Native15;
import org.zedec.zxv.internal.NativeResource;

/**
 * An offline, in-memory IPFS node with the AIPI context over it
 * ({@code zxv_aipi}): a block store with pins, function records and route
 * planning. It belongs to the context of the capability that created it and
 * fails closed with it. Storing ({@link #put}, {@link #putPrivate},
 * {@link #publish}) needs {@link org.zedec.zxv.Permission#AIPI}; reads take
 * only the node.
 *
 * <p>Honest limits: there is no network. A block not stored here is
 * {@code NOT_FOUND}, and {@link #plan} only ever answers {@link Route#LOCAL}
 * or {@link Route#UNAVAILABLE}. Running a function is the AIPI kit's job, not
 * this library's.
 *
 * <p>Threading: libzxv serializes the node with its own mutex, so any thread
 * may call. {@link #close()} waits for calls in flight. The optional node key
 * is copied into native memory and wiped on close; it is never logged.
 */
public final class AipiNode implements AutoCloseable {
    /** Smallest store size in bytes ({@code ZXV_AIPI_STORE_MIN}). */
    public static final long STORE_MIN = 4096;
    /** Largest store size in bytes ({@code ZXV_AIPI_STORE_MAX}, 256 MiB). */
    public static final long STORE_MAX = 268435456L;
    /** Most blocks a node can index ({@code ZXV_AIPI_BLOCKS_MAX}). */
    public static final int BLOCKS_MAX = 65536;

    /**
     * Audit counters.
     *
     * @param blocks        blocks indexed
     * @param storeUsed     store bytes in use
     * @param getsVerified  GETs whose bytes matched their hash
     * @param forbidden     GETs whose stored bytes failed their hash (dropped)
     */
    public record Info(int blocks, long storeUsed, int getsVerified, int forbidden) {}

    private final Capability cap;
    private final ZxvContext ctx;
    private final NativeResource res;

    private AipiNode(Capability cap, ZxvContext ctx, MemorySegment h) {
        this.cap = cap;
        this.ctx = ctx;
        this.res = new NativeResource("AIPI node", h, x -> Ffi.callVoid(Native15.aipi_destroy, x), ctx, Caps.res(ctx));
    }

    /**
     * A node holding at most {@code maxBlocks} blocks in {@code storeBytes} of
     * memory (each block costs its length plus about 100 bytes).
     *
     * @param cap        a capability with {@code AIPI}; later stores use it too
     * @param storeBytes {@link #STORE_MIN}..{@link #STORE_MAX}
     * @param maxBlocks  1..{@link #BLOCKS_MAX}
     * @param nodeKey    32 bytes that seal private blocks (copied), or null
     *                   for a node that refuses private puts
     * @return the node
     * @throws org.zedec.zxv.CapabilityDeniedException without {@code AIPI}
     */
    public static AipiNode create(Capability cap, long storeBytes, int maxBlocks, byte[] nodeKey) {
        ZxvContext ctx = Caps.ctx(cap);
        Buf.index("maxBlocks", maxBlocks);
        if (storeBytes < 0) throw Errors.usage(-1, "storeBytes is negative");
        try (Lease l = Lease.of(Caps.res(cap)); Arena a = Arena.ofConfined()) {
            MemorySegment key = Buf.fixedOrNull(a, "nodeKey", nodeKey, 32);
            return new AipiNode(cap, ctx, Buf.create(ctx, Native15.aipi_create, l.h(0), storeBytes, maxBlocks, key));
        }
    }

    /** Stores and pins {@code data} as one public raw block; returns its CID (the same bytes again is a no-op). */
    public Cid put(byte[] data) {
        return put(data, false);
    }

    /**
     * Stores {@code data} sealed under the node key, never announceable.
     * Storing the same bytes public after private (or the reverse) is refused
     * with {@code ZXV_E_STATE}: making something public is deliberate.
     */
    public Cid putPrivate(byte[] data) {
        return put(data, true);
    }

    private Cid put(byte[] data, boolean priv) {
        Buf.nonNull("data", data);
        try (Lease l = Lease.of(Caps.res(cap), res); Arena a = Arena.ofConfined()) {
            MemorySegment out = a.allocate(Cid.MAX_BYTES), len = a.allocate(JAVA_LONG);
            Ffi.check(Ffi.call(Native15.aipi_put, l.h(0), l.h(1), Buf.in(a, data), (long) data.length,
                    priv ? 1 : 0, out, len), ctx);
            return Cid.of(Buf.read(out, len.get(JAVA_LONG, 0)));
        }
    }

    /**
     * The bytes {@code cid} names, re-hashed before they are returned.
     *
     * @throws org.zedec.zxv.ZxvUsageException      ZXV_E_NOT_FOUND when the block is not held here
     * @throws org.zedec.zxv.cid.CidException     for an identity CID or stored bytes that no longer match
     */
    public byte[] get(Cid cid) {
        Buf.nonNull("cid", cid);
        byte[] c = cid.bytes();
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            MemorySegment cs = Buf.in(a, c);
            return Buf.r4(ctx, 4096, (buf, n, len) -> Ffi.call(Native15.aipi_get, l.h(0), cs, (long) c.length, buf, n, len));
        }
    }

    /** Stores a function record (sealed when its privacy is LOCAL) and returns its function CID. Needs {@code AIPI}. */
    public Cid publish(FunctionRecord record) {
        Buf.nonNull("record", record);
        byte[] r = record.bytes();
        try (Lease l = Lease.of(Caps.res(cap), res); Arena a = Arena.ofConfined()) {
            MemorySegment out = a.allocate(Cid.MAX_BYTES), len = a.allocate(JAVA_LONG);
            Ffi.check(Ffi.call(Native15.aipi_publish_fn, l.h(0), l.h(1), Buf.in(a, r), (long) r.length, out, len), ctx);
            return Cid.of(Buf.read(out, len.get(JAVA_LONG, 0)));
        }
    }

    /** The function record a function CID names, verified by hash and decoded strictly. */
    public FunctionRecord loadFunction(Cid fn) {
        Buf.nonNull("fn", fn);
        byte[] c = fn.bytes();
        byte[] rec;
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            MemorySegment cs = Buf.in(a, c);
            rec = Buf.r4(ctx, FunctionRecord.MAX_BYTES,
                    (buf, n, len) -> Ffi.call(Native15.aipi_load_fn, l.h(0), cs, (long) c.length, buf, n, len));
        }
        return FunctionRecord.decode(rec);
    }

    /**
     * The route a call of {@code fn} on {@code input} would take now. Pure:
     * nothing runs and nothing changes.
     *
     * @param inputPrivate whether the input is private
     */
    public Route plan(Cid fn, byte[] input, boolean inputPrivate) {
        Buf.nonNull("fn", fn);
        byte[] c = fn.bytes();
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            MemorySegment route = a.allocate(JAVA_INT);
            long n = input == null ? 0 : input.length;
            Ffi.check(Ffi.call(Native15.aipi_plan, l.h(0), Buf.in(a, c), (long) c.length, Buf.in(a, input), n,
                    inputPrivate ? 1 : 0, route), ctx);
            return Route.fromCode(route.get(JAVA_INT, 0));
        }
    }

    /** The audit counters. */
    public Info info() {
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            MemorySegment b = a.allocate(JAVA_INT), u = a.allocate(JAVA_LONG), g = a.allocate(JAVA_INT),
                    f = a.allocate(JAVA_INT);
            Ffi.check(Ffi.call(Native15.aipi_info, l.h(0), b, u, g, f), ctx);
            return new Info(b.get(JAVA_INT, 0), u.get(JAVA_LONG, 0), g.get(JAVA_INT, 0), f.get(JAVA_INT, 0));
        }
    }

    /** Whether {@link #close()} has run. */
    public boolean isClosed() {
        return res.isClosed();
    }

    /** Frees the node and wipes its key. Idempotent. */
    @Override
    public void close() {
        res.close();
    }
}
