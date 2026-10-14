// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.vhub;

import org.zedec.zxv.ZxvRefusedException;

/**
 * A hub limit rule refused the swap ({@code ZXV_E_LIMIT}, -38, class REFUSED). This is a policy
 * decision, not a fault; nothing was posted. A new rule epoch ({@link VHub#nextEpoch()}) starts
 * every sender at zero again.
 */
public class LimitException extends ZxvRefusedException {
    private static final long serialVersionUID = 1L;

    /**
     * Creates the exception.
     *
     * @param status     the negative status code (-38)
     * @param statusName its symbolic name ({@code ZXV_E_LIMIT})
     * @param detail     the diagnostic detail from {@code zxv_last_error}
     */
    public LimitException(int status, String statusName, String detail) {
        super(status, statusName, detail);
    }
}
