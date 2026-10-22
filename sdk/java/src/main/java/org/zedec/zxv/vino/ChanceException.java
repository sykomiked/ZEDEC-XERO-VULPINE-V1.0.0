// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.vino;

import org.zedec.zxv.ZxvRefusedException;

/**
 * A LIVE payment request marks a paid chance outcome ({@code ZXV_E_CHANCE}, -50, class REFUSED).
 *
 * <p>A purchase whose result is decided by chance (a loot box or a paid draw) may not be paid for
 * with live Vino: such a request is refused when it is encoded or decoded. The
 * {@link PaymentRequest#FLAG_CHANCE} flag is allowed on the PLAY lane, where play units are not
 * money. The separate allotment module is not a payment request and is not affected.
 */
public class ChanceException extends ZxvRefusedException {
    private static final long serialVersionUID = 1L;

    /**
     * Creates the exception. The SDK builds it from a native status; applications normally only
     * catch it.
     *
     * @param status     the negative status code (-50)
     * @param statusName its symbolic name ({@code ZXV_E_CHANCE})
     * @param detail     the diagnostic detail from {@code zxv_last_error}
     */
    public ChanceException(int status, String statusName, String detail) {
        super(status, statusName, detail);
    }
}
