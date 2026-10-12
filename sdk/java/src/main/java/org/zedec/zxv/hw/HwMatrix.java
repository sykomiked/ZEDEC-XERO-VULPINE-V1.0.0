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
import java.util.ArrayList;
import java.util.Collections;
import java.util.EnumSet;
import java.util.List;
import java.util.Objects;
import java.util.Set;
import org.zedec.zxv.Capability;
import org.zedec.zxv.Hexagram;
import org.zedec.zxv.Truth;
import org.zedec.zxv.ZxvContext;
import org.zedec.zxv.internal.Caps;
import org.zedec.zxv.internal.Errors;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Lease;
import org.zedec.zxv.internal.Native;
import org.zedec.zxv.internal.NativeResource;

/**
 * The emulation matrix of ABI 1.4: functions (engine contracts) served by
 * interchangeable implementations, HARD silicon, a FABRIC or MICRO region,
 * a PEER component, a SOFT emulator or a REMOTE machine. An implementation
 * serves only after it reproduces the function's golden vectors bit for bit
 * ({@link #attest}); the cheapest proven one serves ({@link #bound}), and a
 * fault moves the function to the next.
 *
 * <p>Every matrix starts with the {@link #RES_HOST} resource and the standard
 * engines ({@link HwFunction}), each served by a proven SOFT
 * implementation, so {@link #call} works on any device. Apps state placement
 * constraints ({@link #setPolicy}), never an implementation; key-bearing work
 * is always local. Every call needs {@code HW_MATRIX}; trusting a key and
 * applying contract packages need {@code HW_RECONF}.
 *
 * <p>Thread-safe (libzxv serializes calls). An {@link Implementation} runs on
 * the calling thread inside the native call and may call this matrix again
 * for functions it declared as dependencies ({@link #addDependency}). Its
 * exceptions never cross into C: a {@link org.zedec.zxv.ZxvException} becomes
 * its status, anything else is rethrown from the call that ran it.
 *
 * <pre>{@code
 * try (HwMatrix mx = HwMatrix.create(cap)) {
 *     byte[] digest = mx.call(HwFunction.SHA256, 0, "abc".getBytes(UTF_8));
 * }
 * }</pre>
 */
public final class HwMatrix implements AutoCloseable {
    /** The resource every matrix starts with: this machine's CPU ({@code ZXV_HW_RES_HOST}). */
    public static final int RES_HOST = 0xFFFFFFF0;
    /** Default output capacity of {@link #call} (the largest golden-vector output). */
    public static final int DEFAULT_OUTPUT = 512;

    /**
     * An implementation of a function: the device side of a matrix entry.
     * Return the output bytes (at most the caller's output capacity), or throw
     * a {@link org.zedec.zxv.ZxvException} whose status the caller of
     * {@link #call} receives.
     */
    @FunctionalInterface
    public interface Implementation {
        /** Runs operation {@code op} on {@code input}. */
        byte[] call(int op, byte[] input);
    }

    /** Placement policy flags ({@code ZXV_HW_POLICY_*}). */
    public enum Policy {
        /** Always local, never memoized; once set it cannot be cleared. */
        KEY_BEARING(0x1),
        /** No REMOTE implementation. */
        LOCAL_ONLY(0x2);

        private final int bit;

        Policy(int bit) {
            this.bit = bit;
        }

        /** The flag bit. */
        public int bit() {
            return bit;
        }

        static int mask(Set<Policy> s) {
            int m = 0;
            for (Policy p : s) m |= p.bit;
            return m;
        }

        static Set<Policy> fromMask(int m) {
            EnumSet<Policy> s = EnumSet.noneOf(Policy.class);
            for (Policy p : values()) if ((m & p.bit) != 0) s.add(p);
            return Collections.unmodifiableSet(s);
        }
    }

    /** Implementation status flags ({@code ZXV_HW_PF_*}). */
    public enum ProviderFlag {
        /** Its resource is faulted or offline. */
        FAULT(0x01),
        /** A shadow check found it disagreeing. */
        DISPUTED(0x02),
        /** It failed the golden vectors. */
        FAILED(0x04),
        /** Not proven against the current contract. */
        UNPROVEN(0x08),
        /** Disabled. */
        DISABLED(0x10),
        /** The function's policy excludes it. */
        POLICY(0x20),
        /** Serving the function now. */
        SERVING(0x40);

        private final int bit;

        ProviderFlag(int bit) {
            this.bit = bit;
        }

        /** The flag bit. */
        public int bit() {
            return bit;
        }

        static Set<ProviderFlag> fromMask(int m) {
            EnumSet<ProviderFlag> s = EnumSet.noneOf(ProviderFlag.class);
            for (ProviderFlag p : values()) if ((m & p.bit) != 0) s.add(p);
            return Collections.unmodifiableSet(s);
        }
    }

    /** Coarse power classes ({@code ZXV_HW_POWER_*}), in order. */
    public enum Power {
        /** Not declared. */
        UNKNOWN,
        /** At most 10 mW. */
        MW_10,
        /** At most 100 mW. */
        MW_100,
        /** At most 1 W. */
        W_1,
        /** At most 10 W. */
        W_10,
        /** At most 100 W. */
        W_100,
        /** Above 100 W. */
        MORE;

        static Power fromCode(int c) {
            return c >= 0 && c < values().length ? values()[c] : UNKNOWN;
        }
    }

    /** Resource states ({@code ZXV_HW_RES_*}). */
    public enum ResourceState {
        /** Working. */
        OK,
        /** Faulted: its implementations stop serving. */
        FAULTED,
        /** Being reconfigured. */
        RECONFIGURING,
        /** Offline. */
        OFFLINE
    }

    /**
     * A function's contract and policy.
     *
     * @param contract the contract version
     * @param policy   its placement policy flags
     * @param vectors  how many golden vectors the contract holds
     */
    public record FunctionInfo(int contract, Set<Policy> policy, int vectors) {}

    /**
     * What discovery reports for one implementation.
     *
     * @param functionId      the function it implements
     * @param kind            how it is built
     * @param proof           TRUE serves, FALSE failed a vector, NEITHER unproven, BOTH disputed
     * @param contract        the contract version it last proved
     * @param flags           its status flags
     * @param throughputClass 0 (slowest) .. 15
     * @param power           its declared power class
     * @param calls           how often it ran
     * @param costSpent       the cost it accrued
     */
    public record ProviderInfo(int functionId, HwKind kind, Truth proof, int contract, Set<ProviderFlag> flags,
            int throughputClass, Power power, long calls, long costSpent) {}

    /**
     * The result of a shadow call.
     *
     * @param output  the bound implementation's output
     * @param checked true if a second implementation ran and agreed
     */
    public record ShadowResult(byte[] output, boolean checked) {}

    /**
     * Memo counters.
     *
     * @param hits   repeated inputs answered from the memo
     * @param misses inputs that ran an implementation
     */
    public record MemoStats(long hits, long misses) {}

    private static final MethodHandle ON_CALL;
    private static final FunctionDescriptor CALL_FN =
            FunctionDescriptor.of(JAVA_INT, ADDRESS, JAVA_INT, ADDRESS, JAVA_LONG, ADDRESS, JAVA_LONG, ADDRESS);

    static {
        try {
            ON_CALL = MethodHandles.lookup().findVirtual(Stub.class, "call", MethodType.methodType(int.class,
                    MemorySegment.class, int.class, MemorySegment.class, long.class, MemorySegment.class, long.class,
                    MemorySegment.class));
        } catch (ReflectiveOperationException e) {
            throw new ExceptionInInitializerError(e);
        }
    }

    /** Bridges one {@link Implementation} to {@code zxv_hw_call_fn}. Kept alive by the matrix's arena. */
    private static final class Stub {
        private final Implementation impl;

        Stub(Implementation impl) {
            this.impl = impl;
        }

        // Runs inside a native matrix call. Never throws into C.
        @SuppressWarnings("unused")
        int call(MemorySegment user, int op, MemorySegment in, long inLen, MemorySegment out, long outCap,
                MemorySegment outLen) {
            try {
                byte[] r = impl.call(op, Upcalls.in(in, inLen));
                if (r == null) throw new NullPointerException("an Implementation returned null");
                if (r.length > outCap) return Upcalls.E_BUFFER;
                if (r.length > 0) MemorySegment.copy(r, 0, out.reinterpret(outCap), JAVA_BYTE, 0, r.length);
                outLen.reinterpret(JAVA_LONG.byteSize()).set(JAVA_LONG, 0, r.length);
                return Ffi.OK;
            } catch (Throwable t) {
                return Upcalls.fail(t);
            }
        }
    }

    final Capability cap;
    final ZxvContext ctx;
    final NativeResource res;
    private final Arena stubs;

    private HwMatrix(Capability cap, ZxvContext ctx, MemorySegment h, Arena stubs) {
        this.cap = cap;
        this.ctx = ctx;
        this.stubs = stubs;
        this.res = new NativeResource("hardware matrix", h, x -> {
            try {
                Ffi.callVoid(Native.hw_matrix_destroy, x);
            } finally {
                stubs.close(); // the upcall stubs outlive the native matrix
            }
        }, ctx, Caps.res(ctx));
    }

    /** A new matrix with the host resource and the standard engines; calls go through {@code cap} ({@code HW_MATRIX}). */
    public static HwMatrix create(Capability cap) {
        ZxvContext ctx = Caps.ctx(cap);
        Arena stubs = Arena.ofShared();
        try (Lease l = Lease.of(Caps.res(cap)); Arena a = Arena.ofConfined()) {
            MemorySegment out = a.allocate(ADDRESS);
            Ffi.check(Ffi.call(Native.hw_matrix_create_cap, l.h(0), out), ctx);
            return new HwMatrix(cap, ctx, out.get(ADDRESS, 0), stubs);
        } catch (RuntimeException | Error e) {
            stubs.close();
            throw e;
        }
    }

    /** Calls {@code mh(cap, matrix, rest...)}; callback failures are rethrown. */
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

    /** Adds a function; {@code name} is UTF-8, at most 23 bytes, may be null. */
    public void addFunction(int fnId, String name) {
        try (Arena a = Arena.ofConfined()) {
            run(Native.hw_function_add, fnId, Ffi.cstr(a, name));
        }
    }

    /** Adds a golden vector to the function's current contract; every implementation must prove itself again. */
    public void addVector(int fnId, HwVector v) {
        try (Arena a = Arena.ofConfined()) {
            run(Native.hw_vector_add, fnId, v.op(), Ffi.bytes(a, v.input()), (long) v.input().length,
                    Ffi.bytes(a, v.expect()), (long) v.expect().length);
        }
    }

    /**
     * Starts contract {@code version}: the vectors are cleared and every
     * implementation must prove itself again.
     *
     * @throws org.zedec.zxv.ZxvRefusedException (ZXV_E_ROLLBACK) unless strictly newer
     */
    public void startContract(int fnId, int version) {
        run(Native.hw_function_contract, fnId, version);
    }

    /**
     * Sets placement: flags, a latency ceiling and a power budget (0: none).
     * Only implementations that declared values within them qualify.
     *
     * @throws org.zedec.zxv.ZxvRefusedException (ZXV_E_POLICY) clearing KEY_BEARING once set
     */
    public void setPolicy(int fnId, Set<Policy> flags, int maxLatencyNs, int powerBudgetMw) {
        run(Native.hw_policy_set, fnId, Policy.mask(flags), maxLatencyNs, powerBudgetMw);
    }

    /** A function's contract version, policy and vector count. */
    public FunctionInfo functionInfo(int fnId) {
        try (Lease l = Lease.of(Caps.res(cap), res); Arena a = Arena.ofConfined()) {
            MemorySegment c = a.allocate(JAVA_INT), p = a.allocate(JAVA_INT), n = a.allocate(JAVA_INT);
            Ffi.check(Ffi.call(Native.hw_function_info, l.h(0), l.h(1), fnId, c, p, n), ctx);
            return new FunctionInfo(c.get(JAVA_INT, 0), Policy.fromMask(p.get(JAVA_INT, 0)), n.get(JAVA_INT, 0));
        }
    }

    /** Adds a resource (a component implementations run on). */
    public void addResource(int resId) {
        run(Native.hw_resource_add, resId);
    }

    /** Sets a resource's state; a fault moves its functions to other implementations. */
    public void setResourceState(int resId, ResourceState state) {
        run(Native.hw_resource_set, resId, state.ordinal());
    }

    /**
     * Adds an implementation of {@code fnId} on resource {@code resId}. It does
     * not serve until {@link #attest} proves it. The callback is kept alive
     * for the life of this matrix.
     *
     * @return its provider index
     */
    public int addProvider(int fnId, int resId, HwKind kind, int cost, Implementation impl) {
        Objects.requireNonNull(impl, "impl");
        MemorySegment stub;
        synchronized (stubs) {
            stub = Native.linker().upcallStub(ON_CALL.bindTo(new Stub(impl)), CALL_FN, stubs);
        }
        try (Arena a = Arena.ofConfined()) {
            MemorySegment out = a.allocate(JAVA_INT);
            run(Native.hw_provider_add, fnId, resId, kind.code(), cost, stub, MemorySegment.NULL, out);
            return out.get(JAVA_INT, 0);
        }
    }

    /**
     * Declares traits used by placement and discovery: typical latency per
     * call, power while serving, throughput class 0 .. 15.
     */
    public void setTraits(int provider, int latencyNs, int powerMw, int throughputClass) {
        run(Native.hw_provider_traits, provider, latencyNs, powerMw, throughputClass);
    }

    /** Declares that an implementation calls function {@code depFnId}. */
    public void addDependency(int provider, int depFnId) {
        run(Native.hw_provider_dep, provider, depFnId);
    }

    /** Enables or disables an implementation. */
    public void setEnabled(int provider, boolean on) {
        run(Native.hw_provider_enable, provider, on ? 1 : 0);
    }

    /** Runs the golden vectors: TRUE (serves), FALSE (a vector failed) or NEITHER (no vectors). */
    public Truth attest(int provider) {
        try (Arena a = Arena.ofConfined()) {
            MemorySegment lp = a.allocate(JAVA_INT);
            run(Native.hw_attest, provider, lp);
            return Truth.fromCode(lp.get(JAVA_INT, 0));
        }
    }

    /** The number of implementations (indexed 0 .. count - 1). */
    public int providerCount() {
        try (Lease l = Lease.of(Caps.res(cap), res); Arena a = Arena.ofConfined()) {
            return Upcalls.int32(a, Native.hw_provider_count, ctx, l.h(0), l.h(1));
        }
    }

    /** Discovery of one implementation. */
    public ProviderInfo providerInfo(int provider) {
        try (Lease l = Lease.of(Caps.res(cap), res); Arena a = Arena.ofConfined()) {
            MemorySegment fn = a.allocate(JAVA_INT), kind = a.allocate(JAVA_INT), lp = a.allocate(JAVA_INT),
                    con = a.allocate(JAVA_INT), flags = a.allocate(JAVA_INT), tput = a.allocate(JAVA_INT),
                    pwr = a.allocate(JAVA_INT), calls = a.allocate(JAVA_LONG), cost = a.allocate(JAVA_LONG);
            Ffi.check(Ffi.call(Native.hw_provider_info, l.h(0), l.h(1), provider, fn, kind, lp, con, flags, tput, pwr,
                    calls, cost), ctx);
            return new ProviderInfo(fn.get(JAVA_INT, 0), HwKind.fromCode(kind.get(JAVA_INT, 0)),
                    Truth.fromCode(lp.get(JAVA_INT, 0)), con.get(JAVA_INT, 0),
                    ProviderFlag.fromMask(flags.get(JAVA_INT, 0)), tput.get(JAVA_INT, 0),
                    Power.fromCode(pwr.get(JAVA_INT, 0)), calls.get(JAVA_LONG, 0), cost.get(JAVA_LONG, 0));
        }
    }

    /** Every implementation, by index. */
    public List<ProviderInfo> providers() {
        int n = providerCount();
        List<ProviderInfo> out = new ArrayList<>(n);
        for (int i = 0; i < n; i++) out.add(providerInfo(i));
        return List.copyOf(out);
    }

    /**
     * The implementation serving the function now.
     *
     * @throws org.zedec.zxv.ZxvRefusedException (ZXV_E_NOPROVIDER) if none may
     */
    public int bound(int fnId) {
        try (Arena a = Arena.ofConfined()) {
            MemorySegment out = a.allocate(JAVA_INT);
            run(Native.hw_bound, fnId, out);
            return out.get(JAVA_INT, 0);
        }
    }

    /** {@link #call(int, int, byte[], int)} with {@link #DEFAULT_OUTPUT} bytes of output room. */
    public byte[] call(int fnId, int op, byte[] input) {
        return call(fnId, op, input, DEFAULT_OUTPUT);
    }

    /**
     * Runs operation {@code op} of function {@code fnId} on the implementation
     * placement picks.
     *
     * @param maxOutput room for the output
     * @throws org.zedec.zxv.ZxvRefusedException (ZXV_E_NOPROVIDER, ZXV_E_POLICY) when nothing may serve
     */
    public byte[] call(int fnId, int op, byte[] input, int maxOutput) {
        if (maxOutput < 0) throw Errors.usage(-1, "maxOutput must be >= 0");
        try (Arena a = Arena.ofConfined()) {
            MemorySegment out = a.allocate(Math.max(1, maxOutput)), len = a.allocate(JAVA_LONG);
            run(Native.hw_call, fnId, op, Ffi.bytes(a, input), (long) input.length, out, (long) maxOutput, len);
            return out.asSlice(0, len.get(JAVA_LONG, 0)).toArray(JAVA_BYTE);
        }
    }

    /**
     * A call that also runs the next-best implementation and compares.
     *
     * @throws org.zedec.zxv.ZxvRefusedException (ZXV_E_DISPUTE) on a mismatch; both stop serving
     */
    public ShadowResult shadow(int fnId, int op, byte[] input) {
        try (Arena a = Arena.ofConfined()) {
            MemorySegment out = a.allocate(DEFAULT_OUTPUT), len = a.allocate(JAVA_LONG), checked = a.allocate(JAVA_INT);
            run(Native.hw_shadow, fnId, op, Ffi.bytes(a, input), (long) input.length, out, (long) DEFAULT_OUTPUT, len,
                    checked);
            return new ShadowResult(out.asSlice(0, len.get(JAVA_LONG, 0)).toArray(JAVA_BYTE), checked.get(JAVA_INT, 0) != 0);
        }
    }

    /**
     * Declares the function pure and gives it a memo of {@code entries} (a power
     * of two up to 65536; 0 removes it). Returns the counters.
     *
     * @throws org.zedec.zxv.ZxvRefusedException (ZXV_E_POLICY) for a key-bearing function
     */
    public MemoStats memo(int fnId, int entries) {
        try (Arena a = Arena.ofConfined()) {
            MemorySegment h = a.allocate(JAVA_LONG), m = a.allocate(JAVA_LONG);
            run(Native.hw_memo, fnId, entries, h, m);
            return new MemoStats(h.get(JAVA_LONG, 0), m.get(JAVA_LONG, 0));
        }
    }

    /** Trusts a release key for contract packages (copied; at most 4). Needs {@code HW_RECONF}. */
    public void trustKey(byte[] publicKey) {
        Ffi.requireLength("public key", publicKey, org.zedec.zxv.crypto.HwPackageSigner.PUBLIC_KEY_BYTES);
        try (Arena a = Arena.ofConfined()) {
            run(Native.hw_matrix_trust_key, Ffi.bytes(a, publicKey));
        }
    }

    /**
     * Applies a signed {@code VECTORS} package: verifies both signatures,
     * requires a newer version and svn at or above the floor, installs the
     * vectors, proves every implementation again and records the change.
     * Needs {@code HW_RECONF}.
     *
     * @return how many implementations prove the new contract
     */
    public int applyContract(byte[] pkg) {
        try (Arena a = Arena.ofConfined()) {
            MemorySegment proven = a.allocate(JAVA_INT);
            run(Native.hw_contract_apply, Ffi.bytes(a, pkg), (long) pkg.length, proven);
            return proven.get(JAVA_INT, 0);
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
            run(Native.hw_matrix_log, head, n);
            return new HwAuditRecord.Head(head.toArray(JAVA_BYTE), n.get(JAVA_INT, 0));
        }
    }

    /** Audit record {@code index} (0 = oldest). */
    public HwAuditRecord logEntry(int index) {
        return entry(Native.hw_matrix_log_entry, this::run, index);
    }

    @FunctionalInterface
    interface Runner {
        void run(MethodHandle mh, Object... rest);
    }

    static HwAuditRecord entry(MethodHandle mh, Runner r, int index) {
        try (Arena a = Arena.ofConfined()) {
            MemorySegment ev = a.allocate(JAVA_INT), reason = a.allocate(JAVA_INT), id = a.allocate(JAVA_INT),
                    ver = a.allocate(JAVA_INT), tag = a.allocate(JAVA_INT), subj = a.allocate(32), hash = a.allocate(32);
            r.run(mh, index, ev, reason, id, ver, tag, subj, hash);
            return new HwAuditRecord(HwEvent.fromCode(ev.get(JAVA_INT, 0)), reason.get(JAVA_INT, 0), id.get(JAVA_INT, 0),
                    ver.get(JAVA_INT, 0), Hexagram.fromTag(tag.get(JAVA_INT, 0)), subj.toArray(JAVA_BYTE),
                    hash.toArray(JAVA_BYTE));
        }
    }

    /** Frees the matrix (and the regions built on it) and its callbacks. Idempotent. */
    @Override
    public void close() {
        res.close();
    }
}
