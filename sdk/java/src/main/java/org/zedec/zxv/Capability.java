// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv;

import static java.lang.foreign.ValueLayout.ADDRESS;
import static java.lang.foreign.ValueLayout.JAVA_INT;
import static java.lang.foreign.ValueLayout.JAVA_LONG;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.util.Arrays;
import java.util.Collections;
import java.util.EnumSet;
import java.util.Set;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Lease;
import org.zedec.zxv.internal.Native;
import org.zedec.zxv.internal.NativeResource;

/**
 * An unforgeable-by-convention grant of permissions over one
 * {@link ZxvContext}: what the kernel's language context carries (a process
 * id, a 168-bit identity, a permission set and an attestation state).
 *
 * <p>Every object that makes gated calls ({@code Ledger}, {@code Budgets},
 * {@code Model}, {@code Ed25519}, ...) holds a capability and passes it on
 * every call, and libzxv checks it every time: the handle is live (neither it
 * nor an ancestor revoked), the needed permission is set, and the attestation
 * is {@link Truth#TRUE}. Hand narrower capabilities ({@link #attenuate}) to
 * the parts of a program that need less. There is no global "current"
 * capability.
 *
 * <p>Honest limit: inside one process this is least-privilege discipline, not
 * a sandbox; code in the same JVM can create its own root capability from a
 * context it can reach.
 *
 * <p>Closing a capability closes the capabilities attenuated from it. Closing
 * the context closes all of its capabilities.
 */
public final class Capability implements AutoCloseable {
    /** A snapshot of a capability's state. */
    public record Info(Set<Permission> permissions, int processId, Truth attestation, boolean revoked) {}

    final ZxvContext context;
    final NativeResource res;

    Capability(ZxvContext context, MemorySegment handle, NativeResource parent) {
        this.context = context;
        this.res = new NativeResource("capability", handle,
                h -> Ffi.callVoid(Native.cap_destroy, h), context, parent);
    }

    /** The context this capability governs. */
    public ZxvContext context() {
        return context;
    }

    /**
     * A child capability holding only {@code keep} (intersected with this
     * one's permissions: attenuation never widens). This capability must
     * outlive the child; closing it closes the child.
     */
    public Capability attenuate(Set<Permission> keep) {
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            MemorySegment out = a.allocate(ADDRESS);
            Ffi.check(Ffi.call(Native.cap_attenuate, l.h(0), Permission.mask(keep), out), context);
            return new Capability(context, out.get(ADDRESS, 0), res);
        }
    }

    /** {@link #attenuate(Set)} with varargs. */
    public Capability attenuate(Permission... keep) {
        return attenuate(keep.length == 0 ? EnumSet.noneOf(Permission.class) : EnumSet.copyOf(Arrays.asList(keep)));
    }

    /** Permanently revokes this capability and every capability attenuated from it. */
    public void revoke() {
        try (Lease l = Lease.of(res)) {
            Ffi.check(Ffi.call(Native.cap_revoke, l.h(0)), context);
        }
    }

    /**
     * Sets the attestation. Anything but {@link Truth#TRUE} blocks every gated
     * call; {@link Truth#BOTH} is reported as a contradictory attestation.
     */
    public void setAttestation(Truth state) {
        try (Lease l = Lease.of(res)) {
            Ffi.check(Ffi.call(Native.cap_set_attestation, l.h(0), state.code()), context);
        }
    }

    /** The current permissions, process id, attestation and revocation state. */
    public Info info() {
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            MemorySegment caps = a.allocate(JAVA_LONG), pid = a.allocate(JAVA_INT),
                    att = a.allocate(JAVA_INT), rev = a.allocate(JAVA_INT);
            Ffi.check(Ffi.call(Native.cap_info, l.h(0), caps, pid, att, rev), context);
            return new Info(Collections.unmodifiableSet(Permission.fromMask(caps.get(JAVA_LONG, 0))),
                    pid.get(JAVA_INT, 0), Truth.fromCode(att.get(JAVA_INT, 0)), rev.get(JAVA_INT, 0) != 0);
        }
    }

    /** The permissions this capability holds. */
    public Set<Permission> permissions() {
        return info().permissions();
    }

    /**
     * True if a call needing {@code needed} would pass the gate right now
     * (permission held, live, attested TRUE).
     */
    public boolean allows(Permission... needed) {
        try (Lease l = Lease.of(res)) {
            int st = Ffi.call(Native.cap_check, l.h(0), Permission.mask(Arrays.asList(needed)));
            if (st == -17) return false;
            Ffi.check(st, context);
            return true;
        }
    }

    /**
     * Like {@link #allows} but throws.
     *
     * @throws CapabilityDeniedException with libzxv's reason if the gate would refuse
     */
    public void require(Permission... needed) {
        try (Lease l = Lease.of(res)) {
            Ffi.check(Ffi.call(Native.cap_check, l.h(0), Permission.mask(Arrays.asList(needed))), context);
        }
    }

    /** Frees the handle (and those of capabilities attenuated from it). Idempotent. */
    @Override
    public void close() {
        res.close();
    }
}
