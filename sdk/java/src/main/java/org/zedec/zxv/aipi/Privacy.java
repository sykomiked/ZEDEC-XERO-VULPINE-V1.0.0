// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.aipi;

/**
 * Whether a function or block may leave the node ({@code ZXV_AIPI_PRIV_*}).
 */
public enum Privacy {
    /** May be announced and served to peers. */
    PUBLIC(0),
    /** Private or key-bearing: sealed with the node key and never leaves the node. */
    LOCAL(1);

    private final int code;

    Privacy(int code) {
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
    public static Privacy fromCode(int code) {
        for (Privacy v : values()) if (v.code == code) return v;
        throw new org.zedec.zxv.ZxvUsageException(-1, "ZXV_E_ARG", "not a Privacy code: " + code);
    }
}
