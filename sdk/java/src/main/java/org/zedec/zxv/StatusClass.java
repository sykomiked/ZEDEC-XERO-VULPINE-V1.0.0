// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv;

/**
 * The class of a libzxv status code ({@code zxv_status_class}). Every SDK
 * maps the classes to the same exception types; see {@link ZxvException}.
 */
public enum StatusClass {
    /** {@code ZXV_OK}. */
    OK,
    /** A caller bug; retrying the same call fails again. See {@link ZxvUsageException}. */
    USAGE,
    /** A well-formed request the platform declined; handle it. See {@link ZxvRefusedException}. */
    REFUSED,
    /** The context halted; never retry. See {@link ZxvFatalError}. */
    FATAL,
    /** Out of memory, I/O or an internal fault. See {@link ZxvSystemException}. */
    SYSTEM;

    /** The class for the C value {@code ZXV_CLASS_*}; unknown values are SYSTEM. */
    public static StatusClass fromCode(int code) {
        return code >= 0 && code < values().length ? values()[code] : SYSTEM;
    }
}
