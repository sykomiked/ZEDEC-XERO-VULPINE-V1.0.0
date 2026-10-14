// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.vino;

import org.zedec.zxv.vhub.LimitException;

/**
 * The amount would take today's total past the device's daily cap ({@code ZXV_E_LIMIT}, -38, class
 * REFUSED), raised by {@link SpendMeter#admit}.
 *
 * <p>Nothing was counted. {@link #remainingToday()} says how much the device may still approve
 * today, so an app can offer a smaller amount. The meter is a counter that a deployment configures;
 * it is not a legal check and makes no exemption claim.
 *
 * <p>It extends {@link LimitException}, the SDK's type for every {@code ZXV_E_LIMIT}, so a handler
 * written for limit refusals in general also catches this one.
 */
public class DailyCapException extends LimitException {
    private static final long serialVersionUID = 1L;

    /** What the device may still approve on the refused day (unsigned 64-bit). */
    private final long remainingToday;

    /**
     * Creates the exception.
     *
     * @param status         the negative status code (-38)
     * @param statusName     its symbolic name ({@code ZXV_E_LIMIT})
     * @param detail         the diagnostic detail from {@code zxv_last_error}
     * @param remainingToday what the device may still approve on that day (unsigned 64-bit)
     */
    public DailyCapException(int status, String statusName, String detail, long remainingToday) {
        super(status, statusName, detail);
        this.remainingToday = remainingToday;
    }

    /**
     * What the device may still approve on the day that was refused: the daily cap minus what was
     * already admitted that day. Unsigned 64-bit; never above the cap.
     *
     * @return the amount left today
     */
    public long remainingToday() {
        return remainingToday;
    }
}
