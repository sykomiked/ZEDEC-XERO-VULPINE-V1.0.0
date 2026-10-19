// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.web4;

import static java.lang.foreign.ValueLayout.JAVA_BYTE;
import static java.lang.foreign.ValueLayout.JAVA_INT;
import static java.lang.foreign.ValueLayout.JAVA_LONG;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.util.ArrayList;
import java.util.List;
import java.util.OptionalInt;
import org.zedec.zxv.ZxvStatus;
import org.zedec.zxv.internal.Buf;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Lease;
import org.zedec.zxv.internal.Native15;
import org.zedec.zxv.internal.NativeResource;

/**
 * A signed tool manifest ({@code zxv_w4_manifest}): up to 16 tools with a
 * price, a rate limit and flags, valid in [issued, expires). Signing fixes
 * the agent and peer ids; adding a tool afterwards drops the signature.
 * Holds only caller state: no context or capability (signing goes through
 * {@link AgentKey#sign}).
 *
 * <p>Threading: the C manifest is not thread-safe, so every method here is
 * {@code synchronized}.
 */
public final class ToolManifest implements AutoCloseable {
    /**
     * Header fields (ids are zero until signed).
     *
     * @param agentId   the signing agent's id
     * @param peerId    the signing agent's node id
     * @param version   the manifest version (uint32 bits)
     * @param issuedMs  start of validity
     * @param expiresMs end of validity (exclusive)
     * @param toolCount the number of tools
     * @param signed    whether it carries a signature
     */
    public record Info(byte[] agentId, byte[] peerId, int version, long issuedMs, long expiresMs, int toolCount,
            boolean signed) {}

    private final NativeResource res;

    private ToolManifest(MemorySegment h) {
        this.res = new NativeResource("tool manifest", h, x -> Ffi.callVoid(Native15.w4_manifest_destroy, x), null, null);
    }

    /**
     * An empty, unsigned manifest valid in [issuedMs, expiresMs).
     *
     * @throws org.zedec.zxv.ZxvUsageException ZXV_E_ARG when {@code expiresMs <= issuedMs}
     */
    public static ToolManifest create(int version, long issuedMs, long expiresMs) {
        return new ToolManifest(Buf.create(null, Native15.w4_manifest_create, version, issuedMs, expiresMs));
    }

    /**
     * Parses a manifest's wire form. The signature is kept but NOT checked:
     * call {@link #verify}.
     *
     * @throws org.zedec.zxv.ZxvUsageException ZXV_E_FORMAT for a malformed manifest
     */
    public static ToolManifest decode(byte[] bytes) {
        Buf.nonNull("bytes", bytes);
        try (Arena a = Arena.ofConfined()) {
            return new ToolManifest(Buf.create(null, Native15.w4_manifest_decode, Buf.in(a, bytes), (long) bytes.length));
        }
    }

    /**
     * Adds a tool (dropping any signature).
     *
     * @throws org.zedec.zxv.ZxvUsageException  ZXV_E_DUPLICATE for a name already listed
     * @throws org.zedec.zxv.ZxvRefusedException ZXV_E_CAPACITY with 16 tools
     */
    public synchronized ToolManifest addTool(Tool tool) {
        Buf.nonNull("tool", tool);
        Buf.nonNull("tool name", tool.name());
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            Ffi.check(Ffi.call(Native15.w4_manifest_add_tool, l.h(0), Ffi.cstr(a, tool.name()), tool.priceVfv(),
                    tool.ratePerMin(), tool.burst(), tool.flags(), tool.maxInput()), null);
        }
        return this;
    }

    synchronized void signWith(AgentKey key) {
        key.signManifest(res);
    }

    /**
     * Checks that {@code publicKey} signed this manifest, is its agent, and
     * that it is valid at {@code nowMs}.
     *
     * @throws org.zedec.zxv.VerificationFailedException for a wrong key or a bad (or no) signature
     * @throws org.zedec.zxv.ZxvRefusedException         ZXV_E_STATE when the signature is valid but not at {@code nowMs}
     */
    public synchronized void verify(byte[] publicKey, long nowMs) {
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            Ffi.check(Ffi.call(Native15.w4_manifest_verify, l.h(0),
                    Buf.fixed(a, "publicKey", publicKey, Web4.PUBLIC_KEY_BYTES), nowMs), null);
        }
    }

    /**
     * The signed wire form.
     *
     * @throws org.zedec.zxv.ZxvRefusedException ZXV_E_STATE when not signed
     */
    public synchronized byte[] encode() {
        try (Lease l = Lease.of(res)) {
            MemorySegment h = l.h(0);
            return Buf.r4(null, Web4.MANIFEST_MAX, (buf, n, len) -> Ffi.call(Native15.w4_manifest_encode, h, buf, n, len));
        }
    }

    /** The Web 2 discovery face (for example /.well-known/web4-agent.json) as JSON; prices stay integers. */
    public synchronized String toJson() {
        try (Lease l = Lease.of(res)) {
            MemorySegment h = l.h(0);
            return Ffi.text(null, (buf, n, len) -> Ffi.call(Native15.w4_manifest_to_json, h, buf, n, len));
        }
    }

    /** The header fields. */
    public synchronized Info info() {
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            MemorySegment id = a.allocate(32), peer = a.allocate(32), v = a.allocate(JAVA_INT), i = a.allocate(JAVA_LONG),
                    e = a.allocate(JAVA_LONG), c = a.allocate(JAVA_INT), s = a.allocate(JAVA_INT);
            Ffi.check(Ffi.call(Native15.w4_manifest_info, l.h(0), id, peer, v, i, e, c, s), null);
            return new Info(id.toArray(JAVA_BYTE), peer.toArray(JAVA_BYTE), v.get(JAVA_INT, 0), i.get(JAVA_LONG, 0),
                    e.get(JAVA_LONG, 0), c.get(JAVA_INT, 0), s.get(JAVA_INT, 0) != 0);
        }
    }

    /**
     * Tool {@code index} (0-based).
     *
     * @throws org.zedec.zxv.ZxvUsageException ZXV_E_NOT_FOUND past the last tool
     */
    public synchronized Tool tool(int index) {
        Buf.index("index", index);
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            MemorySegment name = a.allocate(32), p = a.allocate(JAVA_LONG), r = a.allocate(JAVA_INT),
                    b = a.allocate(JAVA_INT), f = a.allocate(JAVA_INT), m = a.allocate(JAVA_INT);
            Ffi.check(Ffi.call(Native15.w4_manifest_tool, l.h(0), index, name, p, r, b, f, m), null);
            return new Tool(W4.name(name, 32), p.get(JAVA_LONG, 0), r.get(JAVA_INT, 0), b.get(JAVA_INT, 0),
                    f.get(JAVA_INT, 0), m.get(JAVA_INT, 0));
        }
    }

    /** Every tool, in order. */
    public synchronized List<Tool> tools() {
        int n = info().toolCount();
        List<Tool> out = new ArrayList<>(n);
        for (int i = 0; i < n; i++) out.add(tool(i));
        return List.copyOf(out);
    }

    /** The index of the tool named {@code name}, if listed. */
    public synchronized OptionalInt find(String name) {
        Buf.nonNull("name", name);
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            MemorySegment i = a.allocate(JAVA_INT);
            int st = Ffi.call(Native15.w4_manifest_find, l.h(0), Ffi.cstr(a, name), i);
            if (st == ZxvStatus.E_NOT_FOUND) return OptionalInt.empty();
            Ffi.check(st, null);
            return OptionalInt.of(i.get(JAVA_INT, 0));
        }
    }

    /** Whether {@link #close()} has run. */
    public boolean isClosed() {
        return res.isClosed();
    }

    /** Frees the manifest. Idempotent. */
    @Override
    public synchronized void close() {
        res.close();
    }
}
