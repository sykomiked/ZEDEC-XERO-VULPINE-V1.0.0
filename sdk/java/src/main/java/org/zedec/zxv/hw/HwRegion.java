// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.hw;

import static java.lang.foreign.ValueLayout.ADDRESS;
import static java.lang.foreign.ValueLayout.JAVA_BYTE;
import static java.lang.foreign.ValueLayout.JAVA_INT;
import static java.lang.foreign.ValueLayout.JAVA_LONG;

import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.MemorySegment;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.util.Objects;
import org.zedec.zxv.Capability;
import org.zedec.zxv.ZxvContext;
import org.zedec.zxv.internal.Caps;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Lease;
import org.zedec.zxv.internal.Native;
import org.zedec.zxv.internal.NativeResource;

/**
 * A reconfigurable region of ABI 1.4: hardware updates delivered as signed
 * software. An image (signed with ML-DSA-87 AND SLH-DSA-SHAKE-256s, built by
 * {@link org.zedec.zxv.crypto.HwPackageSigner}) is verified and loaded into
 * the inactive slot ({@link #stage}), switched to and proven against the
 * matrix's golden vectors ({@link #trial}), then made permanent
 * ({@link #commit}) or rolled back. Failures are quarantined (S0) and the
 * image is blocked forever. Every step is an {@link HwEvent} and a record in a
 * hash-chained audit log ({@link #log()}). Every call needs {@code HW_RECONF}.
 *
 * <p>The device is reached only through a {@link Device} (load, read back,
 * activate). Its callbacks and the {@link EventListener} run on the calling
 * thread inside the region call and are kept alive for the life of the region.
 * Device exceptions never cross into C: a {@link org.zedec.zxv.ZxvException}
 * becomes its status, anything else is rethrown from the call that ran it.
 * Listener exceptions are handed to the thread's uncaught-exception handler.
 * Thread-safe.
 */
public final class HwRegion implements AutoCloseable {
    /** Region states ({@code ZXV_HW_REGION_*}). */
    public enum State {
        /** No image: S0. */
        EMPTY,
        /** An image is active. */
        ACTIVE,
        /** An image is staged in the inactive slot. */
        STAGED,
        /** The tested image is on probation. */
        PROBATION,
        /** Halted: the audit log is full (fail closed). */
        HALTED;

        static State fromCode(int c) {
            if (c < 0 || c >= values().length) {
                throw new org.zedec.zxv.ZxvUsageException(-1, "ZXV_E_ARG", "not a region state: " + c);
            }
            return values()[c];
        }
    }

    /**
     * The device side of a region. Each method signals failure by throwing
     * (a {@link org.zedec.zxv.ZxvException} passes its status to libzxv).
     */
    public interface Device {
        /** Loads {@code payload} into {@code slot} (0 or 1). */
        void load(int slot, byte[] payload);

        /** The SHA-256 (32 bytes) of what {@code slot} really holds. */
        byte[] readback(int slot);

        /** Makes {@code slot} the active one. */
        void activate(int slot);
    }

    /** Receives each recorded step, after the region call has released its locks. */
    @FunctionalInterface
    public interface EventListener {
        /**
         * One step.
         *
         * @param regionId the region
         * @param event    what happened
         * @param reason   the failure status, 0 for none
         * @param version  the image version
         */
        void onEvent(int regionId, HwEvent event, int reason, int version);
    }

    /**
     * A region's state.
     *
     * @param state       its state
     * @param version     the active image's version
     * @param floor       the anti-rollback floor
     * @param quarantined how many images were refused or quarantined
     */
    public record Info(State state, int version, int floor, int quarantined) {}

    private static final MethodHandle ON_LOAD, ON_READBACK, ON_ACTIVATE, ON_EVENT;

    static {
        try {
            MethodHandles.Lookup lk = MethodHandles.lookup();
            ON_LOAD = lk.findVirtual(Bridge.class, "load",
                    MethodType.methodType(int.class, MemorySegment.class, int.class, MemorySegment.class, long.class));
            ON_READBACK = lk.findVirtual(Bridge.class, "readback",
                    MethodType.methodType(int.class, MemorySegment.class, int.class, MemorySegment.class));
            ON_ACTIVATE = lk.findVirtual(Bridge.class, "activate",
                    MethodType.methodType(int.class, MemorySegment.class, int.class));
            ON_EVENT = lk.findVirtual(Listener.class, "event",
                    MethodType.methodType(void.class, MemorySegment.class, int.class, int.class, int.class, int.class));
        } catch (ReflectiveOperationException e) {
            throw new ExceptionInInitializerError(e);
        }
    }

    /** Bridges a {@link Device} to the three C callbacks. Never throws into C. */
    private static final class Bridge {
        private final Device dev;

        Bridge(Device dev) {
            this.dev = dev;
        }

        @SuppressWarnings("unused")
        int load(MemorySegment user, int slot, MemorySegment payload, long len) {
            try {
                dev.load(slot, Upcalls.in(payload, len));
                return Ffi.OK;
            } catch (Throwable t) {
                return Upcalls.fail(t);
            }
        }

        @SuppressWarnings("unused")
        int readback(MemorySegment user, int slot, MemorySegment digest) {
            try {
                byte[] d = dev.readback(slot);
                if (d == null || d.length != 32) return Upcalls.E_ARG;
                MemorySegment.copy(d, 0, digest.reinterpret(32), JAVA_BYTE, 0, 32);
                return Ffi.OK;
            } catch (Throwable t) {
                return Upcalls.fail(t);
            }
        }

        @SuppressWarnings("unused")
        int activate(MemorySegment user, int slot) {
            try {
                dev.activate(slot);
                return Ffi.OK;
            } catch (Throwable t) {
                return Upcalls.fail(t);
            }
        }
    }

    /** Bridges an {@link EventListener} to {@code zxv_hw_event_fn}. Never throws into C. */
    private static final class Listener {
        private final EventListener l;

        Listener(EventListener l) {
            this.l = l;
        }

        @SuppressWarnings("unused")
        void event(MemorySegment user, int region, int event, int reason, int version) {
            try {
                l.onEvent(region, HwEvent.fromCode(event), reason, version);
            } catch (Throwable t) {
                try {
                    Thread th = Thread.currentThread();
                    th.getUncaughtExceptionHandler().uncaughtException(th, t);
                } catch (Throwable ignored) {
                    // Nothing may escape into C.
                }
            }
        }
    }

    private final Capability cap;
    private final ZxvContext ctx;
    private final NativeResource res;
    private final Arena stubs;

    private HwRegion(Capability cap, ZxvContext ctx, MemorySegment h, Arena stubs, NativeResource parent) {
        this.cap = cap;
        this.ctx = ctx;
        this.stubs = stubs;
        this.res = new NativeResource("hardware region", h, x -> {
            try {
                Ffi.callVoid(Native.hw_region_destroy, x);
            } finally {
                stubs.close();
            }
        }, ctx, parent);
    }

    /**
     * Creates a region.
     *
     * @param cap          a capability with {@code HW_RECONF}; every region call goes through it
     * @param matrix       the matrix whose golden vectors test each new image, or null (no test);
     *                     closing it closes this region
     * @param provider     the region's FABRIC or MICRO implementation in {@code matrix}
     * @param regionId     the region's id in events and records
     * @param targetClass  the hardware class images must name
     * @param targetDigest the exact-part digest images must name (32 bytes), or null (zeros)
     * @param device       the device callbacks
     * @param logCapacity  audit log size; when it fills, the region halts and the context is poisoned
     */
    public static HwRegion create(Capability cap, HwMatrix matrix, int provider, int regionId, int targetClass,
            byte[] targetDigest, Device device, int logCapacity) {
        Objects.requireNonNull(device, "device");
        if (targetDigest != null) Ffi.requireLength("target digest", targetDigest, 32);
        if (matrix != null && Caps.ctx(matrix.cap) != Caps.ctx(cap)) {
            throw org.zedec.zxv.internal.Errors.usage(-1, "the matrix belongs to another context");
        }
        ZxvContext ctx = Caps.ctx(cap);
        Arena stubs = Arena.ofShared();
        try (Lease l = matrix == null ? Lease.of(Caps.res(cap)) : Lease.of(Caps.res(cap), matrix.res);
             Arena a = Arena.ofConfined()) {
            Bridge b = new Bridge(device);
            MemorySegment load = Native.linker().upcallStub(ON_LOAD.bindTo(b),
                    FunctionDescriptor.of(JAVA_INT, ADDRESS, JAVA_INT, ADDRESS, JAVA_LONG), stubs);
            MemorySegment rb = Native.linker().upcallStub(ON_READBACK.bindTo(b),
                    FunctionDescriptor.of(JAVA_INT, ADDRESS, JAVA_INT, ADDRESS), stubs);
            MemorySegment act = Native.linker().upcallStub(ON_ACTIVATE.bindTo(b),
                    FunctionDescriptor.of(JAVA_INT, ADDRESS, JAVA_INT), stubs);
            MemorySegment out = a.allocate(ADDRESS);
            Upcalls.clear();
            Upcalls.check(Ffi.call(Native.hw_region_create_cap, l.h(0), matrix == null ? MemorySegment.NULL : l.h(1),
                    provider, regionId, targetClass, Ffi.bytes(a, targetDigest), load, rb, act, MemorySegment.NULL,
                    logCapacity, out), ctx);
            return new HwRegion(cap, ctx, out.get(ADDRESS, 0), stubs, matrix == null ? Caps.res(ctx) : matrix.res);
        } catch (RuntimeException | Error e) {
            stubs.close();
            throw e;
        }
    }

    private void run(MethodHandle mh, Object... rest) {
        try (Lease l = Lease.of(Caps.res(cap), res)) {
            Object[] args = new Object[rest.length + 2];
            args[0] = l.h(0);
            args[1] = l.h(1);
            System.arraycopy(rest, 0, args, 2, rest.length);
            Upcalls.clear();
            Upcalls.check(Ffi.call(mh, args), ctx);
        }
    }

    /** Sets (or, with null, clears) the listener for each recorded step. */
    public void onEvent(EventListener listener) {
        MemorySegment fn = MemorySegment.NULL;
        if (listener != null) {
            synchronized (stubs) {
                fn = Native.linker().upcallStub(ON_EVENT.bindTo(new Listener(listener)),
                        FunctionDescriptor.ofVoid(ADDRESS, JAVA_INT, JAVA_INT, JAVA_INT, JAVA_INT), stubs);
            }
        }
        run(Native.hw_region_events, fn, MemorySegment.NULL);
    }

    /** Trusts a release key (copied; at most 4 per region). */
    public void trustKey(byte[] publicKey) {
        Ffi.requireLength("public key", publicKey, org.zedec.zxv.crypto.HwPackageSigner.PUBLIC_KEY_BYTES);
        try (Arena a = Arena.ofConfined()) {
            run(Native.hw_region_trust_key, Ffi.bytes(a, publicKey));
        }
    }

    /**
     * Verifies an image and loads it into the inactive slot. Refusals
     * (ZXV_E_VERIFY, ZXV_E_ROLLBACK, ZXV_E_TARGET) are recorded; a read-back
     * mismatch is recorded, quarantined and blocked (ZXV_E_QUARANTINED).
     */
    public void stage(byte[] image) {
        try (Arena a = Arena.ofConfined()) {
            run(Native.hw_region_stage, Ffi.bytes(a, image), (long) image.length);
        }
    }

    /**
     * Switches to the staged image and proves it against the golden vectors;
     * passing starts a probation of {@code ticks}.
     *
     * @throws org.zedec.zxv.ZxvRefusedException (ZXV_E_QUARANTINED) if it fails: the previous image (or S0) is back
     */
    public void trial(int ticks) {
        run(Native.hw_region_trial, ticks);
    }

    /** Makes the tested image permanent and raises the floor. */
    public void commit() {
        run(Native.hw_region_commit);
    }

    /** Rolls back to the previous image (or S0). */
    public void rollback() {
        run(Native.hw_region_rollback);
    }

    /** One health tick; an expiring probation rolls back (ZXV_E_QUARANTINED). */
    public void tick() {
        run(Native.hw_region_tick);
    }

    /** The region's state, version, floor and quarantine count. */
    public Info info() {
        try (Arena a = Arena.ofConfined()) {
            MemorySegment st = a.allocate(JAVA_INT), v = a.allocate(JAVA_INT), f = a.allocate(JAVA_INT),
                    q = a.allocate(JAVA_INT);
            run(Native.hw_region_info, st, v, f, q);
            return new Info(State.fromCode(st.get(JAVA_INT, 0)), v.get(JAVA_INT, 0), f.get(JAVA_INT, 0), q.get(JAVA_INT, 0));
        }
    }

    /**
     * The audit log's head and length.
     *
     * @throws org.zedec.zxv.ZxvFatalError if the chain does not verify (the context halts)
     */
    public HwAuditRecord.Head log() {
        try (Arena a = Arena.ofConfined()) {
            MemorySegment head = a.allocate(32), n = a.allocate(JAVA_INT);
            run(Native.hw_region_log, head, n);
            return new HwAuditRecord.Head(head.toArray(JAVA_BYTE), n.get(JAVA_INT, 0));
        }
    }

    /** Audit record {@code index} (0 = oldest). */
    public HwAuditRecord logEntry(int index) {
        return HwMatrix.entry(Native.hw_region_log_entry, this::run, index);
    }

    /** Frees the region and its callbacks. Idempotent. */
    @Override
    public void close() {
        res.close();
    }
}
