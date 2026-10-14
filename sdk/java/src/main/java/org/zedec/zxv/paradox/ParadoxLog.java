// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.paradox;

import org.zedec.zxv.internal.Errors;

/** The book's three append-only, hash-chained logs ({@code ZXV_PDX_LOG_*}). */
public enum ParadoxLog {
    /** The operator log: one record per operator run. */
    OPS(0),
    /** Special records, never edited. */
    FRONTIER(1),
    /** Contained work (the lpres_hex X6 log). */
    QUARANTINE(2);

    private final int code;

    ParadoxLog(int code) {
        this.code = code;
    }

    /** The value's code. */
    public int code() {
        return code;
    }

    /**
     * The value with {@code code}.
     *
     * @throws org.zedec.zxv.ZxvUsageException ZXV_E_ARG for an unknown code
     */
    public static ParadoxLog fromCode(int code) {
        for (ParadoxLog v : values()) if (v.code == code) return v;
        throw Errors.usage(-1, "unknown ParadoxLog " + code);
    }
}
