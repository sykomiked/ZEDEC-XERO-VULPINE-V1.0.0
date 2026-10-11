// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv;

/** A caller bug (USAGE class): fix the arguments; retrying the same call fails again. */
public class ZxvUsageException extends ZxvException {
    private static final long serialVersionUID = 1L;

    /**
     * Creates the exception.
     *
     * @param status     the negative status code
     * @param statusName its symbolic name
     * @param detail     the diagnostic detail
     */
    public ZxvUsageException(int status, String statusName, String detail) {
        super(status, statusName, detail);
    }
}
