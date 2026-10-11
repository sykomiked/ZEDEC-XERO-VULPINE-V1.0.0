// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv;

/** An enforced budget is exhausted (ZXV_E_BUDGET). Nothing was consumed; see Budget#consumeAsync to wait for replenishment. */
public class BudgetExhaustedException extends ZxvRefusedException {
    private static final long serialVersionUID = 1L;

    /**
     * Creates the exception.
     *
     * @param status     the negative status code
     * @param statusName its symbolic name
     * @param detail     the diagnostic detail
     */
    public BudgetExhaustedException(int status, String statusName, String detail) {
        super(status, statusName, detail);
    }
}
