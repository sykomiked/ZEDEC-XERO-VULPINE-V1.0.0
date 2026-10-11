// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv;

/** The four-valued base of a {@link Hexagram}, bits 5-4 ({@code ZXV_HEX_BASE_*}). Numbered by the hexagram line patterns (False 00, Neither 01, Both 10, True 11), NOT like {@link Truth}: convert with {@link Hexagram#toTruth()} and {@link Hexagram#fromTruth}. */
public enum HexBase {
    /** False, code 0 (lines 00). */
    FALSE(0),
    /** Neither, code 1 (01). */
    NEITHER(1),
    /** Both, code 2 (10). */
    BOTH(2),
    /** True, code 3 (11). */
    TRUE(3);

    private final int code;

    HexBase(int code) {
        this.code = code;
    }

    /** The numeric code (bits of a {@link Hexagram} tag). */
    public int code() {
        return code;
    }

    /**
     * The value for a code.
     *
     * @throws ZxvUsageException for anything outside the enum
     */
    public static HexBase fromCode(int code) {
        for (HexBase v : values()) if (v.code == code) return v;
        throw new ZxvUsageException(-1, "ZXV_E_ARG", "not a HexBase code: " + code);
    }
}
