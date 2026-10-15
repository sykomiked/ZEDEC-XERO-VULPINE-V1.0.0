// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.vino;

import org.zedec.zxv.ZxvRefusedException;

/**
 * The payment request has expired, or the approval time is outside its window
 * ({@code ZXV_E_EXPIRED}, -49, class REFUSED).
 *
 * <p>A request carries {@code createdAt} and {@code expiresAt}. A wallet may only approve it at a
 * time within {@code [createdAt, expiresAt]} ({@link Receipt#sign}), and an app only accepts a
 * receipt while {@code now <= expiresAt} ({@link Receipt#verify}). Build a fresh request to ask
 * again.
 */
public class ExpiredException extends ZxvRefusedException {
    private static final long serialVersionUID = 1L;

    /**
     * Creates the exception. The SDK builds it from a native status; applications normally only
     * catch it.
     *
     * @param status     the negative status code (-49)
     * @param statusName its symbolic name ({@code ZXV_E_EXPIRED})
     * @param detail     the diagnostic detail from {@code zxv_last_error}
     */
    public ExpiredException(int status, String statusName, String detail) {
        super(status, statusName, detail);
    }
}
