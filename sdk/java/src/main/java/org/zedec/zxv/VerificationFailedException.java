// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv;

/** A signature or tag did not verify (ZXV_E_VERIFY). Not a fault: the input is not authentic. */
public class VerificationFailedException extends ZxvRefusedException {
    private static final long serialVersionUID = 1L;

    /**
     * Creates the exception.
     *
     * @param status     the negative status code
     * @param statusName its symbolic name
     * @param detail     the diagnostic detail
     */
    public VerificationFailedException(int status, String statusName, String detail) {
        super(status, statusName, detail);
    }
}
