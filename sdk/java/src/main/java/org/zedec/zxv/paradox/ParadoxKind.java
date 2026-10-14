// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.paradox;

import org.zedec.zxv.internal.Errors;

/** The four paradox kinds and NONE ({@code ZXV_PDX_KIND_*}). */
public enum ParadoxKind {
    /** Both, evidence margin growing: a bounded parameter update inside published bounds. */
    CONSTRUCTIVE(0),
    /** Both, margin shrinking or stale: contain, quarantine, roll back, prove. */
    DESTRUCTIVE(1),
    /** Both and steady past the potential phase, or Neither: hold every branch. */
    NEUTRAL(2),
    /** Both, steady, potential phase, no registry signature: a frontier record for review. */
    SPECIAL(3),
    /** True or False: not a paradox. */
    NONE(4);

    private final int code;

    ParadoxKind(int code) {
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
    public static ParadoxKind fromCode(int code) {
        for (ParadoxKind v : values()) if (v.code == code) return v;
        throw Errors.usage(-1, "unknown ParadoxKind " + code);
    }
}
