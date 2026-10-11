// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv;

/**
 * The context halted (FATAL class: {@code ZXV_E_INVARIANT} or
 * {@code ZXV_E_POISONED}). The context is poisoned in C and marked dead in
 * the SDK: every later call on it, or on any object made from it, throws this
 * again. Never retry; close the context and start over from trusted state.
 *
 * <p>This is an {@link Error}, not an exception, so that ordinary
 * {@code catch (Exception e)} blocks let it through. For an uncatchable stop,
 * create the context with {@link FatalPolicy#ABORT}.
 */
public final class ZxvFatalError extends Error {
    private static final long serialVersionUID = 1L;
    private final int status;
    private final String statusName;
    private final String detail;

    /**
     * Creates the error.
     *
     * @param status     the status code (-10 or -18)
     * @param statusName its symbolic name
     * @param detail     the diagnostic detail
     */
    public ZxvFatalError(int status, String statusName, String detail) {
        super(statusName + (detail == null || detail.isEmpty() ? "" : ": " + detail));
        this.status = status;
        this.statusName = statusName;
        this.detail = detail == null ? "" : detail;
    }

    /** The status code. */
    public int status() {
        return status;
    }

    /** The symbolic status name. */
    public String statusName() {
        return statusName;
    }

    /** The diagnostic detail. */
    public String detail() {
        return detail;
    }
}
