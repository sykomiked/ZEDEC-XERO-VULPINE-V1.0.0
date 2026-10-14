// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.paradox;

import org.zedec.zxv.internal.Errors;

/** What to do with a classified paradox ({@code ZXV_PDX_DO_*}). */
public enum ParadoxAction {
    /** Not a paradox: use the hexagram action. */
    NONE(0),
    /** Constructive: {@link ParadoxBook#constructive}. */
    SYNTHESIZE(1),
    /** Destructive: {@link ParadoxBook#destructive}. */
    CONTAIN(2),
    /** Neutral: {@link ParadoxBook#neutral}. */
    HOLD(3),
    /** Special: {@link ParadoxBook#special} (sandbox). */
    RECORD(4),
    /** Forbidden: refuse, roll back, quarantine. */
    ROLLBACK(5);

    private final int code;

    ParadoxAction(int code) {
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
    public static ParadoxAction fromCode(int code) {
        for (ParadoxAction v : values()) if (v.code == code) return v;
        throw Errors.usage(-1, "unknown ParadoxAction " + code);
    }
}
