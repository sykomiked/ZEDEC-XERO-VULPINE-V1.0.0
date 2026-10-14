// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.treaty;

/** What a treaty does when it runs ({@code ZXV_TREATY_OP_*}). */
public enum TreatyOp {
    /** An empty treaty has no operation yet (C value 0). */
    NONE(0),
    /** Net the obligations only. */
    NET(1),
    /** Net, post the legs with the 0.08889% assurance fee, and pay each claim back out. */
    SETL(2);

    private final int code;

    TreatyOp(int code) {
        this.code = code;
    }

    /** The C value. */
    public int code() {
        return code;
    }

    /** The operation for a C value; anything unknown reads as {@link #NONE}. */
    public static TreatyOp fromCode(int code) {
        for (TreatyOp o : values()) if (o.code == code) return o;
        return NONE;
    }
}
