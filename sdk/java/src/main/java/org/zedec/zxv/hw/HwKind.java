// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.hw;

/** How an implementation in the emulation matrix is built ({@code ZXV_HW_KIND_*}), in the order the matrix prefers them at equal cost. Also the value of {@link HwCapKey#KIND} in a presented profile. */
public enum HwKind {
    /** Silicon built for the function. */
    HARD(0),
    /** A reconfigurable fabric region. */
    FABRIC(1),
    /** A microcoded unit or soft core. */
    MICRO(2),
    /** Another local component standing in. */
    PEER(3),
    /** A software emulator. */
    SOFT(4),
    /** Another machine; never serves key-bearing work. */
    REMOTE(5);

    private final int code;

    HwKind(int code) {
        this.code = code;
    }

    /** The numeric code ({@code ZXV_HW_KIND_*}). */
    public int code() {
        return code;
    }

    /**
     * The value for a code.
     *
     * @throws org.zedec.zxv.ZxvUsageException for anything outside the enum
     */
    public static HwKind fromCode(int code) {
        for (HwKind v : values()) if (v.code == code) return v;
        throw new org.zedec.zxv.ZxvUsageException(-1, "ZXV_E_ARG", "not a HwKind code: " + code);
    }
}
