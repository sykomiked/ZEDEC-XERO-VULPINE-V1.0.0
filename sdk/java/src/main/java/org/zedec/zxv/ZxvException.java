// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv;

/**
 * Base of every recoverable error the SDK raises for a negative libzxv
 * status. Branch on the subtype or on {@link #status()}, never on the text.
 *
 * <ul>
 *   <li>{@link ZxvUsageException}: a caller bug (bad argument, format, range)</li>
 *   <li>{@link ZxvRefusedException}: the platform declined (capability, budget, funds, signature)</li>
 *   <li>{@link ZxvSystemException}: environment (I/O, memory, internal)</li>
 * </ul>
 * The terminal case is not here: it is {@link ZxvFatalError}, an {@link Error}.
 */
public class ZxvException extends RuntimeException {
    private static final long serialVersionUID = 1L;
    private final int status;
    private final String statusName;
    private final String detail;

    /**
     * Creates an exception.
     *
     * @param status     the negative status code
     * @param statusName its symbolic name, e.g. {@code ZXV_E_FUNDS}
     * @param detail     the diagnostic text from {@code zxv_last_error}
     */
    public ZxvException(int status, String statusName, String detail) {
        super(statusName + (detail == null || detail.isEmpty() ? "" : ": " + detail));
        this.status = status;
        this.statusName = statusName;
        this.detail = detail == null ? "" : detail;
    }

    /** The status code, e.g. -6. */
    public int status() {
        return status;
    }

    /** The symbolic status name, e.g. {@code ZXV_E_FUNDS}. */
    public String statusName() {
        return statusName;
    }

    /** The diagnostic detail text (may be empty). */
    public String detail() {
        return detail;
    }

    /** The class of this status. */
    public StatusClass statusClass() {
        return Zxv.statusClass(status);
    }
}
