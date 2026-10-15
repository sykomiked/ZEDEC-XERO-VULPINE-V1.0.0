// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.vino;

import static java.lang.foreign.ValueLayout.JAVA_LONG;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import org.zedec.zxv.Capability;
import org.zedec.zxv.ZxvContext;
import org.zedec.zxv.internal.Buf;
import org.zedec.zxv.internal.Caps;
import org.zedec.zxv.internal.Errors;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Lease;
import org.zedec.zxv.internal.Native16;
import org.zedec.zxv.internal.NativeResource;

/**
 * One device's daily spend counter ({@code zxv_vino_meter}): it counts what the device approved on
 * each day and refuses an amount that would take that day's total past a cap.
 *
 * <p>A deployment may set a per-device daily cap; this is how an app or wallet keeps to it. Days
 * are whole numbers the caller chooses (for example Unix time divided by 86400). A day above the
 * last one seen starts a fresh count; a day below it is refused, because it means the clock went
 * back. <b>The meter is a counter, not a legal check</b>, and it makes no exemption claim. It lives
 * in process memory only.
 *
 * <p>The meter keeps the {@link Capability} it was created with and uses it for every call; it must
 * hold {@link org.zedec.zxv.Permission#VINO}. Close it before its context. Any thread may call.
 *
 * <pre>{@code
 * try (SpendMeter meter = SpendMeter.create(cap, 10_000)) {
 *     long day = Instant.now().getEpochSecond() / 86_400;
 *     try {
 *         long left = meter.admit(day, 2_500);   // counted; left is what remains today
 *     } catch (DailyCapException e) {
 *         long left = e.remainingToday();         // nothing counted; offer at most this
 *     }
 * }
 * }</pre>
 */
public final class SpendMeter implements AutoCloseable {
    private final Capability cap;
    private final ZxvContext ctx;
    private final NativeResource res;
    private final long dailyCap;

    private SpendMeter(Capability cap, ZxvContext ctx, MemorySegment h, long dailyCap) {
        this.cap = cap;
        this.ctx = ctx;
        this.dailyCap = dailyCap;
        this.res = new NativeResource("Vino spend meter", h, x -> Ffi.callVoid(Native16.vino_meter_destroy, x), ctx,
                Caps.res(ctx));
    }

    /**
     * Creates a meter with a daily cap.
     *
     * @param cap      a capability holding {@link org.zedec.zxv.Permission#VINO}; the meter keeps it
     *                 and uses it for every later call
     * @param dailyCap the most the device may approve per day, 1 to {@link Vino#AMOUNT_MAX} (unsigned)
     * @return the meter; close it before its context
     * @throws org.zedec.zxv.ZxvUsageException         ({@code ZXV_E_RANGE}) for a cap of 0 or above the maximum
     * @throws org.zedec.zxv.CapabilityDeniedException without {@code VINO}
     * @throws org.zedec.zxv.ZxvFatalError              if the context has halted ({@code ZXV_E_POISONED})
     */
    public static SpendMeter create(Capability cap, long dailyCap) {
        ZxvContext ctx = Caps.ctx(cap);
        try (Lease l = Lease.of(Caps.res(cap))) {
            return new SpendMeter(cap, ctx, Buf.create(ctx, Native16.vino_meter_create, l.h(0), dailyCap), dailyCap);
        }
    }

    /**
     * The daily cap this meter was created with.
     *
     * @return the cap (unsigned 64-bit)
     */
    public long dailyCap() {
        return dailyCap;
    }

    /**
     * Counts {@code amount} against day {@code day}, if it fits under the cap.
     *
     * @param day    the day index (for example Unix time / 86400); not below the last day seen
     * @param amount the amount to count, 1 to {@link Vino#AMOUNT_MAX} (unsigned)
     * @return what is left today after counting (unsigned 64-bit)
     * @throws DailyCapException               when the day's total would pass the cap; nothing is
     *                                         counted, and {@link DailyCapException#remainingToday()}
     *                                         says what is left
     * @throws org.zedec.zxv.ZxvUsageException ({@code ZXV_E_RANGE}) when {@code day} is below the last
     *                                         day seen (the clock went back), or the amount is 0 or
     *                                         too large
     */
    public long admit(long day, long amount) {
        try (Lease l = Lease.of(Caps.res(cap), res); Arena a = Arena.ofConfined()) {
            MemorySegment left = a.allocate(JAVA_LONG);
            int st = Ffi.call(Native16.vino_meter_admit, l.h(0), l.h(1), day, amount, left);
            if (st == -38) {
                throw new DailyCapException(st, Errors.name(st), Ffi.lastError(), left.get(JAVA_LONG, 0));
            }
            Ffi.check(st, ctx);
            return left.get(JAVA_LONG, 0);
        }
    }

    /**
     * Whether {@link #close()} has run (or the context was closed).
     *
     * @return true once closed
     */
    public boolean isClosed() {
        return res.isClosed();
    }

    /** Frees the meter and its counts. Idempotent. */
    @Override
    public void close() {
        res.close();
    }
}
