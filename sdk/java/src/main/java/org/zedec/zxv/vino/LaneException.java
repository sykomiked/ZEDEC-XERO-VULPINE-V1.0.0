// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.vino;

import org.zedec.zxv.ZxvUsageException;

/**
 * The lane is wrong for this call, or is neither PLAY nor LIVE ({@code ZXV_E_LANE}, -48, class
 * USAGE).
 *
 * <p>Raised when a payment request's lane byte is not {@link Lane#PLAY} (1) or {@link Lane#LIVE}
 * (2), for example while decoding bytes from elsewhere. It is a caller error: the same input will
 * always be refused.
 */
public class LaneException extends ZxvUsageException {
    private static final long serialVersionUID = 1L;

    /**
     * Creates the exception. The SDK builds it from a native status; applications normally only
     * catch it.
     *
     * @param status     the negative status code (-48)
     * @param statusName its symbolic name ({@code ZXV_E_LANE})
     * @param detail     the diagnostic detail from {@code zxv_last_error}
     */
    public LaneException(int status, String statusName, String detail) {
        super(status, statusName, detail);
    }
}
