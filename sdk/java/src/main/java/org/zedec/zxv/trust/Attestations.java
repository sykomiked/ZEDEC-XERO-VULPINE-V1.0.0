// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.trust;

import static java.lang.foreign.ValueLayout.ADDRESS;
import static java.lang.foreign.ValueLayout.JAVA_INT;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.util.OptionalInt;
import org.zedec.zxv.Capability;
import org.zedec.zxv.Truth;
import org.zedec.zxv.ZxvContext;
import org.zedec.zxv.internal.Caps;
import org.zedec.zxv.internal.Errors;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Lease;
import org.zedec.zxv.internal.Native;
import org.zedec.zxv.internal.NativeResource;

/**
 * An attestation registry (the kernel's {@code lpres}): named claims about
 * sources, models, sensors, keys, ... each holding a four-valued
 * {@link Truth} that follows the evidence. Recording needs {@code ATTEST}.
 *
 * <p>The registry's {@link #isGateClear() gate} is clear only when every
 * attestation is certain (TRUE or FALSE): any BOTH or NEITHER blocks actuator
 * authority.
 *
 * <pre>{@code
 * try (Attestations reg = Attestations.create(cap)) {
 *     int m = reg.attest("model.gguf", Subject.MODEL_PACK, Truth.TRUE, "loader", digest);
 *     reg.evidence(m, false);              // a contradicting report
 *     reg.state(m).truth();                // BOTH: held, not collapsed
 * }
 * }</pre>
 */
public final class Attestations implements AutoCloseable {
    /**
     * The state of one attestation.
     *
     * @param truth    the four-valued state
     * @param positive supporting evidence count (attesting TRUE counts as one)
     * @param negative contradicting evidence count
     */
    public record State(Truth truth, int positive, int negative) {}

    private final Capability cap;
    private final ZxvContext ctx;
    private final NativeResource res;

    private Attestations(Capability cap, ZxvContext ctx, MemorySegment h) {
        this.cap = cap;
        this.ctx = ctx;
        this.res = new NativeResource("attestation registry", h, x -> Ffi.callVoid(Native.lpres_destroy, x), ctx, Caps.res(ctx));
    }

    /** A new, empty registry in the capability's context, recording through that capability. */
    public static Attestations create(Capability cap) {
        ZxvContext ctx = Caps.ctx(cap);
        try (Lease l = Lease.of(Caps.res(cap)); Arena a = Arena.ofConfined()) {
            MemorySegment out = a.allocate(ADDRESS);
            Ffi.check(Ffi.call(Native.lpres_create_cap, l.h(0), out), ctx);
            return new Attestations(cap, ctx, out.get(ADDRESS, 0));
        }
    }

    /**
     * Records an attestation.
     *
     * @param name     at most 31 bytes
     * @param subject  what it is about
     * @param state    the initial claim
     * @param attester who claims it, at most 31 bytes
     * @param digest   up to 32 bytes identifying the subject (may be null)
     * @return the attestation's index in this registry
     */
    public int attest(String name, Subject subject, Truth state, String attester, byte[] digest) {
        if (digest != null && digest.length > 32) throw Errors.usage(-1, "digest is at most 32 bytes");
        try (Lease l = Lease.of(Caps.res(cap), res); Arena a = Arena.ofConfined()) {
            MemorySegment idx = a.allocate(JAVA_INT);
            Ffi.check(Ffi.call(Native.lpres_attest, l.h(0), l.h(1), Ffi.cstr(a, name), subject.code(), state.code(),
                    Ffi.cstr(a, attester), Ffi.bytes(a, digest), digest == null ? 0 : digest.length, idx), ctx);
            return idx.get(JAVA_INT, 0);
        }
    }

    /** Adds one piece of evidence; the state follows {@link Truth#fromEvidence}. */
    public void evidence(int index, boolean positive) {
        try (Lease l = Lease.of(Caps.res(cap), res)) {
            Ffi.check(Ffi.call(Native.lpres_evidence, l.h(0), l.h(1), index, positive ? 1 : 0), ctx);
        }
    }

    /** The index of a named attestation, if present. */
    public OptionalInt find(String name) {
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            MemorySegment idx = a.allocate(JAVA_INT);
            int st = Ffi.call(Native.lpres_find, l.h(0), Ffi.cstr(a, name), idx);
            if (st == -4) return OptionalInt.empty();
            Ffi.check(st, ctx);
            return OptionalInt.of(idx.get(JAVA_INT, 0));
        }
    }

    /** The current state of an attestation. */
    public State state(int index) {
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            MemorySegment st = a.allocate(JAVA_INT), p = a.allocate(JAVA_INT), n = a.allocate(JAVA_INT);
            Ffi.check(Ffi.call(Native.lpres_state, l.h(0), index, st, p, n), ctx);
            return new State(Truth.fromCode(st.get(JAVA_INT, 0)), p.get(JAVA_INT, 0), n.get(JAVA_INT, 0));
        }
    }

    /** True if every attestation is certain (TRUE or FALSE): no BOTH, no NEITHER. */
    public boolean isGateClear() {
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            MemorySegment c = a.allocate(JAVA_INT);
            Ffi.check(Ffi.call(Native.lpres_gate, l.h(0), c), ctx);
            return c.get(JAVA_INT, 0) != 0;
        }
    }

    /** Frees the registry. Idempotent. */
    @Override
    public void close() {
        res.close();
    }
}
