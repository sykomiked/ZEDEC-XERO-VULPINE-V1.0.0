// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.cid;

import org.zedec.zxv.ZxvRefusedException;

/**
 * Content does not match its CID, or the bytes or text are not a canonical CID
 * ({@code ZXV_E_CID}, -34, class REFUSED). A mismatch is not a fault in this library: it means
 * the bytes you hold are not the ones the CID names; drop them and fetch again.
 */
public class CidException extends ZxvRefusedException {
    private static final long serialVersionUID = 1L;

    /**
     * Creates the exception.
     *
     * @param status     the negative status code (-34)
     * @param statusName its symbolic name ({@code ZXV_E_CID})
     * @param detail     the diagnostic detail from {@code zxv_last_error}
     */
    public CidException(int status, String statusName, String detail) {
        super(status, statusName, detail);
    }
}
