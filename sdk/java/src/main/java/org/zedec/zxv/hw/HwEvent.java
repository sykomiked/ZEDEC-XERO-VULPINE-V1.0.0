// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.hw;

/** A step of a hardware update or contract change ({@code ZXV_HW_EV_*}): reported to a {@link HwRegion.EventListener} and recorded in an audit log ({@link HwAuditRecord}). */
public enum HwEvent {
    /** Verified and loaded into the inactive slot. */
    STAGED(1),
    /** Switched to and proven: probation begins. */
    TESTED(2),
    /** Committed: permanent, floor raised. */
    ACTIVATED(3),
    /** Back to the previous image (or to S0). */
    ROLLED_BACK(4),
    /** Refused before loading (signature, replay, target). */
    REJECTED(5),
    /** Failed in hardware; the payload is blocked forever. */
    QUARANTINED(6),
    /** A function's golden vectors replaced (matrix log). */
    CONTRACT(7);

    private final int code;

    HwEvent(int code) {
        this.code = code;
    }

    /** The numeric code ({@code ZXV_HW_EV_*}). */
    public int code() {
        return code;
    }

    /**
     * The value for a code.
     *
     * @throws org.zedec.zxv.ZxvUsageException for anything outside the enum
     */
    public static HwEvent fromCode(int code) {
        for (HwEvent v : values()) if (v.code == code) return v;
        throw new org.zedec.zxv.ZxvUsageException(-1, "ZXV_E_ARG", "not a HwEvent code: " + code);
    }
}
