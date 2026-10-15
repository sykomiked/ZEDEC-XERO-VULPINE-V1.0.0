// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.vino;

import org.zedec.zxv.internal.Errors;

/**
 * The rail a play-lane voucher is held on ({@code ZXV_VINO_RAIL_*}).
 *
 * <p>Every active voucher is on exactly one rail at a time. A transfer must name that rail
 * ({@link PlaySandbox#transfer}), so one voucher can never be spent twice, once as a note and once
 * as its ledger twin. The holder may switch rails with {@link PlaySandbox#setRail}; switching moves
 * nothing and changes no totals.
 */
public enum Rail {
    /** The note in the holder's hand ({@code ZXV_VINO_RAIL_BEARER}, 0). */
    BEARER(0),
    /** Its twin on the play ledger ({@code ZXV_VINO_RAIL_GHOST}, 1). */
    GHOST(1);

    private final int code;

    Rail(int code) {
        this.code = code;
    }

    /**
     * The C value of this rail.
     *
     * @return 0 for {@link #BEARER}, 1 for {@link #GHOST}
     */
    public int code() {
        return code;
    }

    /**
     * The rail with a C value.
     *
     * @param code 0 or 1
     * @return the rail
     * @throws org.zedec.zxv.ZxvUsageException ({@code ZXV_E_ARG}) for any other value
     */
    public static Rail fromCode(int code) {
        for (Rail r : values()) if (r.code == code) return r;
        throw Errors.usage(-1, "rail " + code + " is not BEARER (0) or GHOST (1)");
    }
}
