// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.phase;

/** The K6 coordinator's answer to an admission request. */
public enum Decision {
    /** Admitted: a non-zero token was issued. */
    ADMIT,
    /** Rejected. */
    VETO,
    /** Parked in S0 until conditions change. */
    DEFER,
    /** Try again. */
    RETRY;

    static Decision fromCode(int c) {
        return c >= 0 && c < 4 ? values()[c] : VETO;
    }
}
