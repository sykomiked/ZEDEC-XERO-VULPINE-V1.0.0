// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.wire;

/** What the W5 correlation resolver reads from a UBH-168 frame ({@code ZXV_TRUTH_*}). */
public enum WireTruth {
    /** No signal. */
    UNKNOWN,
    /** S+ and S- agree on presence. */
    TRUE,
    /** They agree on absence. */
    FALSE,
    /** Neutral (the idle pilot). */
    NEUTRAL,
    /** A contradiction, held. */
    GLUT,
    /** A persistent GLUT. */
    PARADOX;

    static WireTruth fromCode(int c) {
        return c >= 0 && c < values().length ? values()[c] : UNKNOWN;
    }
}
