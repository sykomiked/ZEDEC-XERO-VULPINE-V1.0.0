// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.paradox;

import org.zedec.zxv.ZxvRefusedException;

/**
 * A paradox graph reached its step limit without resolving ({@code ZXV_E_PARADOX_STEP}, -39, class
 * REFUSED). The pass that reached the limit WAS logged and the graph stays readable; it now goes to
 * the owner ({@link ParadoxGraph#info()} reports it as escalated).
 */
public class StepLimitException extends ZxvRefusedException {
    private static final long serialVersionUID = 1L;

    /**
     * Creates the exception.
     *
     * @param status     the negative status code (-39)
     * @param statusName its symbolic name ({@code ZXV_E_PARADOX_STEP})
     * @param detail     the diagnostic detail from {@code zxv_last_error}
     */
    public StepLimitException(int status, String statusName, String detail) {
        super(status, statusName, detail);
    }
}
