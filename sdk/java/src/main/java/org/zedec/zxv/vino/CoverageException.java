// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.vino;

import org.zedec.zxv.ZxvRefusedException;

/**
 * The call would take a play issuer below 1.8x coverage ({@code ZXV_E_COVERAGE}, -47, class
 * REFUSED).
 *
 * <p>An issuer may only have vouchers outstanding while {@code 10 * backing >= 18 * outstanding}
 * (exact integers, no rounding). {@link PlaySandbox#issue} raises this when the new voucher's face
 * would break the rule, and {@link PlaySandbox#unback} when withdrawing backing would. Nothing
 * changed. Post more backing ({@link PlaySandbox#back}) or wait for vouchers to be redeemed.
 */
public class CoverageException extends ZxvRefusedException {
    private static final long serialVersionUID = 1L;

    /**
     * Creates the exception. The SDK builds it from a native status; applications normally only
     * catch it.
     *
     * @param status     the negative status code (-47)
     * @param statusName its symbolic name ({@code ZXV_E_COVERAGE})
     * @param detail     the diagnostic detail from {@code zxv_last_error}
     */
    public CoverageException(int status, String statusName, String detail) {
        super(status, statusName, detail);
    }
}
