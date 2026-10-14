// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.vhub;

import org.zedec.zxv.ZxvRefusedException;

/**
 * No valid signed peg for this currency pair ({@code ZXV_E_PEG}, -37, class REFUSED): a peg is
 * still unset, or a new peg's epoch is not above the current one (a stale or replayed peg).
 * Nothing changed.
 */
public class PegException extends ZxvRefusedException {
    private static final long serialVersionUID = 1L;

    /**
     * Creates the exception.
     *
     * @param status     the negative status code (-37)
     * @param statusName its symbolic name ({@code ZXV_E_PEG})
     * @param detail     the diagnostic detail from {@code zxv_last_error}
     */
    public PegException(int status, String statusName, String detail) {
        super(status, statusName, detail);
    }
}
