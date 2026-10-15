// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.vino;

import org.zedec.zxv.internal.Errors;

/** Where a play-lane voucher is in its life ({@code ZXV_VINO_V_*}). */
public enum VoucherState {
    /** Issued and not yet redeemed: it can be transferred, switched or redeemed ({@code ZXV_VINO_V_ACTIVE}, 1). */
    ACTIVE(1),
    /**
     * Returned to its issuer and retired ({@code ZXV_VINO_V_REDEEMED}, 2). Redeeming pays nothing out;
     * a redeemed voucher stays readable by id but every change to it is refused.
     */
    REDEEMED(2);

    private final int code;

    VoucherState(int code) {
        this.code = code;
    }

    /**
     * The C value of this state.
     *
     * @return 1 for {@link #ACTIVE}, 2 for {@link #REDEEMED}
     */
    public int code() {
        return code;
    }

    /**
     * The state with a C value.
     *
     * @param code 1 or 2
     * @return the state
     * @throws org.zedec.zxv.ZxvUsageException ({@code ZXV_E_ARG}) for any other value
     */
    public static VoucherState fromCode(int code) {
        for (VoucherState s : values()) if (s.code == code) return s;
        throw Errors.usage(-1, "voucher state " + code + " is not ACTIVE (1) or REDEEMED (2)");
    }
}
