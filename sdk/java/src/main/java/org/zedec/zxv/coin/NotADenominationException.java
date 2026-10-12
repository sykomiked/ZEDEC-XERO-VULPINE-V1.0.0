// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.coin;

import org.zedec.zxv.ZxvUsageException;

/**
 * A face that is not on the Fibonacci ladder ({@code ZXV_E_DENOM}, -32, class USAGE): a coin face
 * or token face such as 4, 6, 7 or 100, or zero. To pay an arbitrary amount, split it with
 * {@link Coins#split(long)}.
 */
public class NotADenominationException extends ZxvUsageException {
    private static final long serialVersionUID = 1L;

    /**
     * Creates the exception.
     *
     * @param status     the negative status code (-32)
     * @param statusName its symbolic name ({@code ZXV_E_DENOM})
     * @param detail     the diagnostic detail from {@code zxv_last_error}
     */
    public NotADenominationException(int status, String statusName, String detail) {
        super(status, statusName, detail);
    }
}
