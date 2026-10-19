// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.aipi;

/**
 * How a remote result of an AIPI function is judged ({@code ZXV_AIPI_VERIFY_*}).
 */
public enum VerifyMode {
    /** A checker export judges (input, output); the record names it. */
    CHECKER(1),
    /** As {@link #CHECKER}, with a certificate from a certifier export. */
    CERT(2),
    /** k of n executors must agree byte for byte (needs a deterministic function). */
    QUORUM(3),
    /** Never accept a remote result: run it here. */
    LOCAL(4);

    private final int code;

    VerifyMode(int code) {
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
    public static VerifyMode fromCode(int code) {
        for (VerifyMode v : values()) if (v.code == code) return v;
        throw new org.zedec.zxv.ZxvUsageException(-1, "ZXV_E_ARG", "not a VerifyMode code: " + code);
    }
}
