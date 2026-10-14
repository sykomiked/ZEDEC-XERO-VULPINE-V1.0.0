// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.web4;

import org.zedec.zxv.ZxvRefusedException;

/**
 * No valid consent for this agent action ({@code ZXV_E_CONSENT}, -35, class REFUSED): no token, a
 * token for another human, agent, action, scope or amount, one outside its validity window, one whose
 * signature fails, or one already used or revoked. {@link #detail()} says which; nothing was allowed.
 */
public class ConsentException extends ZxvRefusedException {
    private static final long serialVersionUID = 1L;

    /**
     * Creates the exception.
     *
     * @param status     the negative status code (-35)
     * @param statusName its symbolic name ({@code ZXV_E_CONSENT})
     * @param detail     the diagnostic detail from {@code zxv_last_error}
     */
    public ConsentException(int status, String statusName, String detail) {
        super(status, statusName, detail);
    }
}
