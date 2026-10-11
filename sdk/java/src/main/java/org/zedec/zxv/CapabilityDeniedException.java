// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv;

/** The capability lacks a needed permission, is revoked, or its attestation is not TRUE (ZXV_E_CAP). */
public class CapabilityDeniedException extends ZxvRefusedException {
    private static final long serialVersionUID = 1L;

    /**
     * Creates the exception.
     *
     * @param status     the negative status code
     * @param statusName its symbolic name
     * @param detail     the diagnostic detail
     */
    public CapabilityDeniedException(int status, String statusName, String detail) {
        super(status, statusName, detail);
    }
}
