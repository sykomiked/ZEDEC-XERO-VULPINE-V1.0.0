// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.paradox;

import static java.lang.foreign.ValueLayout.JAVA_BYTE;
import static java.lang.foreign.ValueLayout.JAVA_INT;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import org.zedec.zxv.Capability;
import org.zedec.zxv.Hexagram;
import org.zedec.zxv.ZxvContext;
import org.zedec.zxv.internal.Buf;
import org.zedec.zxv.internal.Caps;
import org.zedec.zxv.internal.Errors;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Lease;
import org.zedec.zxv.internal.Native15;
import org.zedec.zxv.internal.NativeResource;

/**
 * A complex paradox ({@code zxv_paradox_graph}): up to 64 classified nodes
 * with dependencies, resolved pass by pass in a fixed order
 * ({@link ParadoxBook#pass(ParadoxGraph)}) and escalated to the owner after the step limit.
 * Nodes are taken in id order, so every peer with the same signed inputs
 * reaches the same states and the same chain. Building the graph needs no
 * capability.
 *
 * <p>Threading: the C handle is not thread-safe, so every method here, and
 * every pass, synchronizes on it.
 */
public final class ParadoxGraph implements AutoCloseable {
    /**
     * One node, in id order.
     *
     * @param id    its 32-byte id
     * @param kind  its current kind
     * @param state its state
     */
    public record Node(byte[] id, ParadoxKind kind, NodeState state) {}

    /**
     * The graph's state.
     *
     * @param nodes        node count
     * @param passes       passes run
     * @param totalMag     net tension magnitude of the open nodes after the last pass
     * @param totalDir     its direction, -1, 0 or +1
     * @param escalated    whether it went to the owner
     * @param chain        the SHA-256 chain over every pass (zeros before the first)
     */
    public record Info(int nodes, int passes, int totalMag, int totalDir, boolean escalated, byte[] chain) {}

    final ZxvContext ctx;
    final NativeResource res;

    private ParadoxGraph(ZxvContext ctx, MemorySegment h) {
        this.ctx = ctx;
        this.res = new NativeResource("paradox graph", h, x -> Ffi.callVoid(Native15.paradox_graph_destroy, x), ctx,
                Caps.res(ctx));
    }

    /**
     * A graph that escalates after {@code stepLimit} (at least 1) passes without resolving.
     *
     * @param cap a capability with {@code PARADOX}
     */
    public static ParadoxGraph create(Capability cap, int stepLimit) {
        ZxvContext ctx = Caps.ctx(cap);
        try (Lease l = Lease.of(Caps.res(cap))) {
            return new ParadoxGraph(ctx, Buf.create(ctx, Native15.paradox_graph_create, l.h(0), stepLimit));
        }
    }

    /**
     * Adds a node before the first pass.
     *
     * @param id   the hash of its signed input (32 bytes; the sort key)
     * @param kind a classified pair's kind
     * @param tag  a classified pair's tag
     * @param mag  tension magnitude (uint32 bits)
     * @param dir  tension direction, -1, 0 or +1
     */
    public synchronized void addNode(byte[] id, ParadoxKind kind, Hexagram tag, int mag, int dir) {
        node(Native15.paradox_graph_add_node, id, kind, tag, mag, dir);
    }

    /** Node {@code id} depends on node {@code dep} (which may be added later, but before the first pass). */
    public synchronized void addDependency(byte[] id, byte[] dep) {
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            Ffi.check(Ffi.call(Native15.paradox_graph_add_dep, l.h(0), Buf.fixed(a, "id", id, 32),
                    Buf.fixed(a, "dep", dep, 32)), ctx);
        }
    }

    /**
     * Queues new evidence for a node, applied at the start of the next pass.
     * Several in one batch merge "worst wins", so order never matters.
     */
    public synchronized void update(byte[] id, ParadoxKind kind, Hexagram tag, int mag, int dir) {
        node(Native15.paradox_graph_update, id, kind, tag, mag, dir);
    }

    private void node(java.lang.invoke.MethodHandle mh, byte[] id, ParadoxKind kind, Hexagram tag, int mag, int dir) {
        Buf.nonNull("kind", kind);
        Buf.nonNull("tag", tag);
        if (dir < -1 || dir > 1) throw Errors.usage(-1, "dir must be -1, 0 or +1");
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            Ffi.check(Ffi.call(mh, l.h(0), Buf.fixed(a, "id", id, 32), kind.code(), tag.tag(), mag, dir), ctx);
        }
    }

    /** A node's state (OPEN before the first pass). */
    public synchronized NodeState state(byte[] id) {
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            MemorySegment s = a.allocate(JAVA_INT);
            Ffi.check(Ffi.call(Native15.paradox_graph_state, l.h(0), Buf.fixed(a, "id", id, 32), s), ctx);
            return NodeState.fromCode(s.get(JAVA_INT, 0));
        }
    }

    /**
     * Node {@code index} in id order.
     *
     * @throws org.zedec.zxv.ZxvRefusedException ZXV_E_STATE before the first pass
     */
    public synchronized Node node(int index) {
        Buf.index("index", index);
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            MemorySegment id = a.allocate(32), k = a.allocate(JAVA_INT), s = a.allocate(JAVA_INT);
            Ffi.check(Ffi.call(Native15.paradox_graph_node_at, l.h(0), index, id, k, s), ctx);
            return new Node(id.toArray(JAVA_BYTE), ParadoxKind.fromCode(k.get(JAVA_INT, 0)),
                    NodeState.fromCode(s.get(JAVA_INT, 0)));
        }
    }

    /** Node count, passes, net tension, escalation and the pass chain. */
    public synchronized Info info() {
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            MemorySegment n = a.allocate(JAVA_INT), p = a.allocate(JAVA_INT), m = a.allocate(JAVA_INT),
                    d = a.allocate(JAVA_INT), e = a.allocate(JAVA_INT), c = a.allocate(32);
            Ffi.check(Ffi.call(Native15.paradox_graph_info, l.h(0), n, p, m, d, e, c), ctx);
            return new Info(n.get(JAVA_INT, 0), p.get(JAVA_INT, 0), m.get(JAVA_INT, 0), d.get(JAVA_INT, 0),
                    e.get(JAVA_INT, 0) != 0, c.toArray(JAVA_BYTE));
        }
    }

    /** Whether {@link #close()} has run. */
    public boolean isClosed() {
        return res.isClosed();
    }

    /** Frees the graph. Idempotent. */
    @Override
    public synchronized void close() {
        res.close();
    }
}
