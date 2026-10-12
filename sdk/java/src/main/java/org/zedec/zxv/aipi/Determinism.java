// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.aipi;

/**
 * Whether a function gives the same output for the same input ({@code ZXV_AIPI_DET_*}).
 */
public enum Determinism {
    /** Outputs may differ between runs. */
    NONDETERMINISTIC(0),
    /** The same input always gives the same output bytes. */
    DETERMINISTIC(1);

    private final int code;

    Determinism(int code) {
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
    public static Determinism fromCode(int code) {
        for (Determinism v : values()) if (v.code == code) return v;
        throw new org.zedec.zxv.ZxvUsageException(-1, "ZXV_E_ARG", "not a Determinism code: " + code);
    }
}
