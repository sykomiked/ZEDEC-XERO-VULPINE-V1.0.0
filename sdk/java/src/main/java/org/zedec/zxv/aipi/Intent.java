// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.aipi;

/**
 * What an AIPI function is for ({@code ZXV_AIPI_INTENT_*}). The intent picks the
 * function CID's codec: {@link #codec()}.
 */
public enum Intent {
    /** A general function ({@code zxv-fn}). */
    FN(0),
    /** Integer tensor inference ({@code zxv-tensor-infer}). */
    TENSOR_INFER(1),
    /** Ledger settlement ({@code zxv-settle-ledger}). */
    SETTLE_LEDGER(2),
    /** Swarm routing ({@code zxv-swarm-route}). */
    SWARM_ROUTE(3);

    private final int code;

    Intent(int code) {
        this.code = code;
    }

    /** The C value. */
    public int code() {
        return code;
    }

    /**
     * The value for a C code.
     *
     * @throws org.zedec.zxv.ZxvUsageException ZXV_E_ARG for a code that is not listed
     */
    public static Intent fromCode(int code) {
        for (Intent v : values()) if (v.code == code) return v;
        throw new org.zedec.zxv.ZxvUsageException(-1, "ZXV_E_ARG", "not a Intent code: " + code);
    }

    /** The multicodec of a function CID with this intent ({@code ZXV_AIPI_MC_FN + intent}). */
    public long codec() {
        return org.zedec.zxv.cid.Multicodec.AIPI_FN + code;
    }
}
