// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv;

/** The feature is not built into this library yet (ZXV_E_NOTIMPL); see Zxv#features(). */
public class NotImplementedException extends ZxvRefusedException {
    private static final long serialVersionUID = 1L;

    /**
     * Creates the exception.
     *
     * @param status     the negative status code
     * @param statusName its symbolic name
     * @param detail     the diagnostic detail
     */
    public NotImplementedException(int status, String statusName, String detail) {
        super(status, statusName, detail);
    }
}
