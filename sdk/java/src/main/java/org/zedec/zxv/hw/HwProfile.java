// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.hw;

import static java.lang.foreign.ValueLayout.ADDRESS;
import static java.lang.foreign.ValueLayout.JAVA_BYTE;
import static java.lang.foreign.ValueLayout.JAVA_INT;
import static java.lang.foreign.ValueLayout.JAVA_LONG;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.invoke.MethodHandle;
import java.util.ArrayList;
import java.util.List;
import java.util.OptionalLong;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Lease;
import org.zedec.zxv.internal.Native;
import org.zedec.zxv.internal.NativeResource;

/**
 * A ZHP-1 machine profile, the "hardware ROM" of ABI 1.4: a canonical tree
 * of abstract components (a {@link HwClass} each) with integer capability
 * descriptors ({@link HwCapKey}) and an opaque private extension per node.
 * It has one encoding and one SHA-256 identity ({@link #digest()}).
 *
 * <p>{@link #probeHost()} describes the machine you are on (only what the OS
 * reports reliably), {@link #reference()} is the public VULPINE-class
 * reference machine R1 that ZXV software targets, and
 * {@link #present(HwProfile, HwProfile)} says what serves each R1 component
 * on a host. Profiles are plain data: no context or capability. Thread-safe;
 * close it when done.
 *
 * <pre>{@code
 * try (HwProfile host = HwProfile.probeHost(); HwProfile r1 = HwProfile.reference()) {
 *     HwProfile.Match m = HwProfile.match(host, r1);   // native vs emulated components
 * }
 * }</pre>
 */
public final class HwProfile implements AutoCloseable {
    /** The parent id of a root node ({@code ZXV_HW_ROOT}). */
    public static final int ROOT = 0xFFFF;
    /** Most private extension bytes per node ({@code ZXV_HW_EXT_MAX}). */
    public static final int EXT_MAX = 256;

    /** How a target component is served on a host ({@code ZXV_HW_NATIVE}, {@code _EMULATE}, {@code _GROUP}). */
    public enum PairKind {
        /** A host component satisfies it. */
        NATIVE,
        /** Nothing does: the matrix emulates it. */
        EMULATE,
        /** A device or package node: nothing to run. */
        GROUP;

        static PairKind fromCode(int code) {
            if (code < 0 || code >= values().length) {
                throw new org.zedec.zxv.ZxvUsageException(-1, "ZXV_E_ARG", "not a pair kind: " + code);
            }
            return values()[code];
        }
    }

    /**
     * One node.
     *
     * @param id             its id
     * @param parent         its parent's id, or {@link #ROOT}
     * @param componentClass its {@link HwClass} (uint32 bits)
     * @param capCount       how many capabilities it carries
     */
    public record Node(int id, int parent, int componentClass, int capCount) {}

    /**
     * How a target matches a host.
     *
     * @param nativeCount   target components a host component runs natively
     * @param emulatedCount target components the matrix must emulate
     */
    public record Match(int nativeCount, int emulatedCount) {}

    /**
     * The pairing of one target node with a host.
     *
     * @param targetId the target node id
     * @param kind     how it is served
     * @param hostId   the host node id (for {@link PairKind#NATIVE})
     * @param unmetKey the first capability the host lacks (for
     *                 {@link PairKind#EMULATE}; 0 if the host has no component of that class)
     */
    public record Pairing(int targetId, PairKind kind, int hostId, int unmetKey) {}

    private final NativeResource res;

    private HwProfile(MemorySegment h) {
        this.res = new NativeResource("hardware profile", h, x -> Ffi.callVoid(Native.hw_profile_destroy, x), null, null);
    }

    private static HwProfile make(MethodHandle mh, Object... head) {
        try (Arena a = Arena.ofConfined()) {
            MemorySegment out = a.allocate(ADDRESS);
            Object[] all = java.util.Arrays.copyOf(head, head.length + 1);
            all[head.length] = out;
            Ffi.check(Ffi.call(mh, all), null);
            return new HwProfile(out.get(ADDRESS, 0));
        }
    }

    /** An empty profile to build with {@link #addNode}. */
    public static HwProfile create() {
        return make(Native.hw_profile_create);
    }

    /**
     * This machine: CPU cluster (ISA, cores, SIMD), memory, cache line. Units
     * that cannot be detected portably are left out and will be emulated.
     */
    public static HwProfile probeHost() {
        return make(Native.hw_profile_probe_host);
    }

    /** A copy of the public VULPINE-class reference machine R1. */
    public static HwProfile reference() {
        return make(Native.hw_profile_reference);
    }

    /**
     * Parses a canonical ZHP-1 encoding.
     *
     * @throws org.zedec.zxv.ZxvUsageException (ZXV_E_FORMAT) for anything non-canonical
     */
    public static HwProfile decode(byte[] encoding) {
        try (Arena a = Arena.ofConfined()) {
            return make(Native.hw_profile_decode, Ffi.bytes(a, encoding), (long) encoding.length);
        }
    }

    /**
     * The target as served on the host: a copy of {@code target} in which every
     * node that runs something carries {@link HwCapKey#KIND} ({@link HwKind#HARD}
     * where the host has it, {@link HwKind#SOFT} where it is emulated).
     */
    public static HwProfile present(HwProfile host, HwProfile target) {
        try (Lease l = Lease.of(host.res, target.res)) {
            return make(Native.hw_profile_present, l.h(0), l.h(1));
        }
    }

    /** How many target components run natively on the host and how many are emulated. */
    public static Match match(HwProfile host, HwProfile target) {
        try (Lease l = Lease.of(host.res, target.res); Arena a = Arena.ofConfined()) {
            MemorySegment n = a.allocate(JAVA_INT), e = a.allocate(JAVA_INT);
            Ffi.check(Ffi.call(Native.hw_profile_match, l.h(0), l.h(1), n, e), null);
            return new Match(n.get(JAVA_INT, 0), e.get(JAVA_INT, 0));
        }
    }

    /**
     * The pairing of target node {@code index} (in target order).
     *
     * @throws org.zedec.zxv.ZxvUsageException (ZXV_E_NOT_FOUND) past the last node
     */
    public static Pairing pair(HwProfile host, HwProfile target, int index) {
        try (Lease l = Lease.of(host.res, target.res); Arena a = Arena.ofConfined()) {
            MemorySegment need = a.allocate(JAVA_INT), kind = a.allocate(JAVA_INT), have = a.allocate(JAVA_INT),
                    unmet = a.allocate(JAVA_INT);
            Ffi.check(Ffi.call(Native.hw_profile_pair, l.h(0), l.h(1), index, need, kind, have, unmet), null);
            return new Pairing(need.get(JAVA_INT, 0), PairKind.fromCode(kind.get(JAVA_INT, 0)), have.get(JAVA_INT, 0),
                    unmet.get(JAVA_INT, 0));
        }
    }

    /** The canonical ZHP-1 encoding. */
    public byte[] encode() {
        try (Lease l = Lease.of(res)) {
            return Upcalls.bytes(null, (buf, cap, len) -> Ffi.call(Native.hw_profile_encode, l.h(0), buf, cap, len));
        }
    }

    /** The SHA-256 of the canonical encoding: the profile's identity. */
    public byte[] digest() {
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            MemorySegment d = a.allocate(32);
            Ffi.check(Ffi.call(Native.hw_profile_digest, l.h(0), d), null);
            return d.toArray(JAVA_BYTE);
        }
    }

    /**
     * Adds a node: ids strictly increase, parent first (or {@link #ROOT}).
     *
     * @param componentClass a {@link HwClass} (uint32 bits)
     */
    public void addNode(int id, int parent, int componentClass) {
        try (Lease l = Lease.of(res)) {
            Ffi.check(Ffi.call(Native.hw_profile_node_add, l.h(0), id, parent, componentClass), null);
        }
    }

    /** Sets capability {@code key} ({@link HwCapKey}) of a node; {@code value} is uint64 bits. */
    public void setCap(int nodeId, int key, long value) {
        try (Lease l = Lease.of(res)) {
            Ffi.check(Ffi.call(Native.hw_profile_cap_set, l.h(0), nodeId, key, value), null);
        }
    }

    /** Capability {@code key} of a node (uint64 bits), or empty if the node lacks it. */
    public OptionalLong cap(int nodeId, int key) {
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            MemorySegment v = a.allocate(JAVA_LONG);
            int st = Ffi.call(Native.hw_profile_cap_get, l.h(0), nodeId, key, v);
            if (st == Upcalls.E_NOT_FOUND) return OptionalLong.empty();
            Ffi.check(st, null);
            return OptionalLong.of(v.get(JAVA_LONG, 0));
        }
    }

    /**
     * Sets a node's private extension: opaque bytes (at most {@link #EXT_MAX})
     * that ZXV carries, encodes and digests but never interprets.
     */
    public void setExtension(int nodeId, byte[] ext) {
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            Ffi.check(Ffi.call(Native.hw_profile_ext_set, l.h(0), nodeId, Ffi.bytes(a, ext), (long) ext.length), null);
        }
    }

    /** A node's private extension (empty if none). */
    public byte[] extension(int nodeId) {
        try (Lease l = Lease.of(res)) {
            return Upcalls.bytes(null, (buf, cap, len) -> Ffi.call(Native.hw_profile_ext_get, l.h(0), nodeId, buf, cap, len));
        }
    }

    /** The number of nodes. */
    public int nodeCount() {
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            return Upcalls.int32(a, Native.hw_profile_count, null, l.h(0));
        }
    }

    /** Node {@code index} (0 .. {@link #nodeCount()} - 1). */
    public Node node(int index) {
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            MemorySegment id = a.allocate(JAVA_INT), parent = a.allocate(JAVA_INT), cls = a.allocate(JAVA_INT),
                    n = a.allocate(JAVA_INT);
            Ffi.check(Ffi.call(Native.hw_profile_node, l.h(0), index, id, parent, cls, n), null);
            return new Node(id.get(JAVA_INT, 0), parent.get(JAVA_INT, 0), cls.get(JAVA_INT, 0), n.get(JAVA_INT, 0));
        }
    }

    /** Every node, in order. */
    public List<Node> nodes() {
        int n = nodeCount();
        List<Node> out = new ArrayList<>(n);
        for (int i = 0; i < n; i++) out.add(node(i));
        return List.copyOf(out);
    }

    /** Frees the profile. Idempotent. */
    @Override
    public void close() {
        res.close();
    }
}
