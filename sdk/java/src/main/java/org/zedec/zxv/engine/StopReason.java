// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.engine;

/** Why a generation stopped ({@code ZXV_STOP_*}). */
public enum StopReason {
    /** The end-of-sequence token. */
    EOS,
    /** {@code maxTokens} reached. */
    MAX_TOKENS,
    /** Reserved: a stop string. */
    STOP_STRING,
    /** The token listener (or a cancelled stream/future) asked to stop. */
    CALLBACK,
    /** The context window is full. */
    CONTEXT_FULL,
    /** The bound budget ran out. */
    BUDGET,
    /** A code this SDK does not know (a newer library). */
    UNKNOWN;

    static StopReason fromCode(int c) {
        return switch (c) {
            case 1 -> EOS;
            case 2 -> MAX_TOKENS;
            case 3 -> STOP_STRING;
            case 4 -> CALLBACK;
            case 5 -> CONTEXT_FULL;
            case 6 -> BUDGET;
            default -> UNKNOWN;
        };
    }
}
