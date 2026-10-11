// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv;

/** What happens next to tagged work ({@code ZXV_HEX_ACT_*}). */
public enum HexAction {
    /** Go ahead. */
    PROCEED(0),
    /** Hold (unknown, a weakening truth, or a converging dispute). */
    WAIT(1),
    /** An ordinary refusal; nothing to roll back. */
    REFUSE(2),
    /** A dispute for a person to settle. */
    ESCALATE(3),
    /** Roll back and quarantine ({@link org.zedec.zxv.trust.QuarantineLog}). */
    ROLLBACK(4);

    private final int code;

    HexAction(int code) {
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
    public static HexAction fromCode(int code) {
        for (HexAction v : values()) if (v.code == code) return v;
        throw new ZxvUsageException(-1, "ZXV_E_ARG", "not a HexAction code: " + code);
    }
}
