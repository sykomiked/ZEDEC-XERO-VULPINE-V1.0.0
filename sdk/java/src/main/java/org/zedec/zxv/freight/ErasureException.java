// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.freight;

import org.zedec.zxv.ZxvRefusedException;

/**
 * Too few distinct freight rows arrived to rebuild the payload ({@code ZXV_E_ERASURE}, -36, class
 * REFUSED). Nothing was decoded; add the packets again once more rows arrive.
 */
public class ErasureException extends ZxvRefusedException {
    private static final long serialVersionUID = 1L;

    /**
     * Creates the exception.
     *
     * @param status     the negative status code (-36)
     * @param statusName its symbolic name ({@code ZXV_E_ERASURE})
     * @param detail     the diagnostic detail from {@code zxv_last_error}
     */
    public ErasureException(int status, String statusName, String detail) {
        super(status, statusName, detail);
    }
}
