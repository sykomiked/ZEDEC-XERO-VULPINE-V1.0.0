// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv;

/** How the evidence of a {@link Hexagram} is moving, bits 3-2 ({@code ZXV_HEX_POL_*}). */
public enum HexPolarity {
    /** Diverging or stale evidence. */
    ENTROPIC(0),
    /** Steady. */
    NEUTRAL(1),
    /** A wall: absorbing; the work rolls back and is quarantined. */
    FORBIDDEN(2),
    /** Converging evidence. */
    NEGENTROPIC(3);

    private final int code;

    HexPolarity(int code) {
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
    public static HexPolarity fromCode(int code) {
        for (HexPolarity v : values()) if (v.code == code) return v;
        throw new ZxvUsageException(-1, "ZXV_E_ARG", "not a HexPolarity code: " + code);
    }
}
