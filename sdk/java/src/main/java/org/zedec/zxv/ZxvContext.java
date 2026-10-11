// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv;

import static java.lang.foreign.ValueLayout.ADDRESS;
import static java.lang.foreign.ValueLayout.JAVA_BYTE;
import static java.lang.foreign.ValueLayout.JAVA_INT;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.EnumSet;
import java.util.List;
import java.util.Optional;
import java.util.Set;
import org.zedec.zxv.internal.Access;
import org.zedec.zxv.internal.Errors;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Lease;
import org.zedec.zxv.internal.Native;
import org.zedec.zxv.internal.NativeResource;

/**
 * One ZXV platform instance: a ledger, its enabled currencies, and the
 * root of every capability, budget registry, phase clock and swarm made for
 * it. Thread-safe (libzxv serializes calls on a context).
 *
 * <p>Always close it, ideally with try-with-resources. Closing it first
 * closes every object created from it (capabilities, budgets, swarms,
 * models, ...), each after its in-flight calls return.
 *
 * <pre>{@code
 * try (ZxvContext ctx = ZxvContext.create();
 *      Capability root = ctx.rootCapability(Permission.all())) {
 *     ...
 * }
 * }</pre>
 */
public final class ZxvContext implements AutoCloseable {
    static {
        Access.install(new Access.Core() {
            @Override
            public NativeResource resource(ZxvContext ctx) {
                return ctx.res;
            }

            @Override
            public NativeResource resource(Capability cap) {
                return cap.res;
            }

            @Override
            public ZxvContext context(Capability cap) {
                return cap.context;
            }

            @Override
            public void markDead(ZxvContext ctx, String why) {
                if (ctx.deadReason == null) ctx.deadReason = why;
            }

            @Override
            public String deadReason(ZxvContext ctx) {
                return ctx.deadReason;
            }
        });
    }

    /** Facts about a currency enabled in a context. */
    public record CurrencyInfo(String code, int minorUnits, boolean isPrivate, int flags) {}

    private final NativeResource res;
    private final FatalPolicy fatalPolicy;
    private volatile String deadReason;

    private ZxvContext(MemorySegment handle, FatalPolicy policy) {
        this.fatalPolicy = policy;
        this.res = new NativeResource("context", handle, h -> Ffi.callVoid(Native.ctx_destroy, h), this, null);
    }

    /** A new context with {@link FatalPolicy#THROW}: an empty ledger, no currencies enabled. */
    public static ZxvContext create() {
        return create(FatalPolicy.THROW);
    }

    /** A new context with the given fatal policy. */
    public static ZxvContext create(FatalPolicy policy) {
        try (Arena a = Arena.ofConfined()) {
            MemorySegment out = a.allocate(ADDRESS);
            Ffi.check(Ffi.call(Native.ctx_create, out), null);
            return new ZxvContext(out.get(ADDRESS, 0), policy);
        }
    }

    /** What happens when this context halts. */
    public FatalPolicy fatalPolicy() {
        return fatalPolicy;
    }

    /**
     * A root capability with process id 0 and an all-zero identity.
     * Its attestation starts {@link Truth#TRUE}.
     */
    public Capability rootCapability(Set<Permission> permissions) {
        return rootCapability(0, null, permissions);
    }

    /** {@link #rootCapability(Set)} with varargs. */
    public Capability rootCapability(Permission... permissions) {
        return rootCapability(permissions.length == 0 ? EnumSet.noneOf(Permission.class)
                : EnumSet.copyOf(Arrays.asList(permissions)));
    }

    /**
     * A root capability.
     *
     * @param processId   the kernel process id it acts for
     * @param identity    the 21-byte (168-bit) identity, or null for zeros
     * @param permissions what it may do
     */
    public Capability rootCapability(int processId, byte[] identity, Set<Permission> permissions) {
        if (identity != null) Ffi.requireLength("identity", identity, 21);
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            MemorySegment out = a.allocate(ADDRESS);
            Ffi.check(Ffi.call(Native.cap_create, l.h(0), processId, Ffi.bytes(a, identity),
                    Permission.mask(permissions), out), this);
            return new Capability(this, out.get(ADDRESS, 0), res);
        }
    }

    /**
     * True if the context halted (a broken invariant or {@link #halt}). A
     * halted context refuses every call; it can only be closed.
     */
    public boolean isPoisoned() {
        if (deadReason != null) return true;
        if (res.isClosed()) throw new IllegalStateException("this context is closed");
        try (Arena a = Arena.ofConfined()) {
            MemorySegment p = a.allocate(JAVA_INT);
            MemorySegment h = res.acquire();
            try {
                Ffi.check(Ffi.call(Native.ctx_is_poisoned, h, p), this);
            } finally {
                res.release();
            }
            return p.get(JAVA_INT, 0) != 0;
        }
    }

    /**
     * Halts the context on purpose: an operator kill switch, and how tests
     * exercise the fatal path. The next call on it, or on anything made from
     * it, raises {@link ZxvFatalError} (or halts the JVM under
     * {@link FatalPolicy#ABORT}).
     *
     * @param reason at most 150 bytes of UTF-8; may be null
     */
    public void halt(String reason) {
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            Ffi.check(Ffi.call(Native.ctx_halt, l.h(0), Ffi.cstr(a, reason)), this);
        }
    }

    /**
     * Enables an ISO 4217 currency for ledger accounts, with its minor units
     * from the ISO 4217 table. Needs {@code LEDGER_POST}.
     *
     * @param cap a capability of this context
     * @throws CapabilityDeniedException if {@code cap} lacks {@code LEDGER_POST}
     *         or belongs to a different context
     * @throws ZxvUsageException {@code ZXV_E_CURRENCY} if it is not payable or not ISO 4217
     */
    public void enableCurrency(Capability cap, String alpha) {
        try (Lease l = Lease.of(ownCap(cap).res); Arena a = Arena.ofConfined()) {
            Ffi.check(Ffi.call(Native.ccy_enable_cap, l.h(0), Ffi.cstr(a, alpha)), this);
        }
    }

    /**
     * Enables a currency asserting its minor units (a mismatch with the table
     * is {@code ZXV_E_CURRENCY}). Needs {@code LEDGER_POST}.
     *
     * @param cap a capability of this context
     */
    public void enableCurrency(Capability cap, String alpha, int minorUnits) {
        int mu = u8(minorUnits);
        try (Lease l = Lease.of(ownCap(cap).res); Arena a = Arena.ofConfined()) {
            Ffi.check(Ffi.call(Native.ccy_enable_with_minor_cap, l.h(0), Ffi.cstr(a, alpha), mu), this);
        }
    }

    /**
     * Registers a platform-private unit (for example {@code "VFV"}, 2 minor
     * units): usable in ledger accounts, refused as an ISO 20022 currency.
     * Needs {@code LEDGER_POST}.
     *
     * @param cap  a capability of this context
     * @param code 3 to 8 upper-case letters or digits, not an ISO 4217 alpha
     */
    public void registerPrivateCurrency(Capability cap, String code, int minorUnits) {
        int mu = u8(minorUnits);
        try (Lease l = Lease.of(ownCap(cap).res); Arena a = Arena.ofConfined()) {
            Ffi.check(Ffi.call(Native.ccy_register_private_cap, l.h(0), Ffi.cstr(a, code), mu), this);
        }
    }

    /**
     * Enables an ISO 4217 currency without a capability.
     *
     * @deprecated since ABI 1.2 (no capability): use {@link #enableCurrency(Capability, String)}.
     */
    @Deprecated(since = "1.2")
    public void enableCurrency(String alpha) {
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            Ffi.check(Ffi.call(Native.ccy_enable, l.h(0), Ffi.cstr(a, alpha)), this);
        }
    }

    /**
     * Enables a currency asserting its minor units, without a capability.
     *
     * @deprecated since ABI 1.2 (no capability): use {@link #enableCurrency(Capability, String, int)}.
     */
    @Deprecated(since = "1.2")
    public void enableCurrency(String alpha, int minorUnits) {
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            Ffi.check(Ffi.call(Native.ccy_enable_with_minor, l.h(0), Ffi.cstr(a, alpha), u8(minorUnits)), this);
        }
    }

    /**
     * Registers a platform-private unit without a capability.
     *
     * @deprecated since ABI 1.2 (no capability): use
     *             {@link #registerPrivateCurrency(Capability, String, int)}.
     */
    @Deprecated(since = "1.2")
    public void registerPrivateCurrency(String code, int minorUnits) {
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            Ffi.check(Ffi.call(Native.ccy_register_private, l.h(0), Ffi.cstr(a, code), u8(minorUnits)), this);
        }
    }

    /** {@code cap}, after checking it was made from this context (else ZXV_E_CAP). */
    private Capability ownCap(Capability cap) {
        if (cap == null) throw Errors.usage(-1, "cap must not be null");
        if (cap.context != this) {
            throw Errors.raise(-17, "the capability belongs to a different context", this);
        }
        return cap;
    }

    /** The enabled currency {@code code}, or empty if it is not enabled here. */
    public Optional<CurrencyInfo> currency(String code) {
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            MemorySegment mu = a.allocate(JAVA_BYTE), fl = a.allocate(JAVA_INT);
            int st = Ffi.call(Native.ccy_get, l.h(0), Ffi.cstr(a, code), mu, fl);
            if (st == -4 || st == -7) return Optional.empty();
            Ffi.check(st, this);
            int flags = fl.get(JAVA_INT, 0);
            return Optional.of(new CurrencyInfo(code, Byte.toUnsignedInt(mu.get(JAVA_BYTE, 0)), (flags & 0x10) != 0, flags));
        }
    }

    /** The codes of every currency enabled in this context, in enabling order. */
    public List<String> enabledCurrencies() {
        try (Lease l = Lease.of(res); Arena a = Arena.ofConfined()) {
            MemorySegment n = a.allocate(JAVA_INT);
            Ffi.check(Ffi.call(Native.ccy_enabled_count, l.h(0), n), this);
            List<String> out = new ArrayList<>();
            for (int i = 0; i < n.get(JAVA_INT, 0); i++) {
                final int idx = i;
                out.add(Ffi.text(this, (b, c, len) -> Ffi.call(Native.ccy_enabled_at, l.h(0), idx, b, c, len)));
            }
            return List.copyOf(out);
        }
    }

    /** A uint8_t argument, passed as an int 0..255 (never a signed byte: see Native). */
    private static int u8(int v) {
        if (v < 0 || v > 255) throw Errors.usage(-1, "minor units must be 0..255");
        return v;
    }

    /** Closes every object created from this context, then frees it. Idempotent. */
    @Override
    public void close() {
        res.close();
    }
}
