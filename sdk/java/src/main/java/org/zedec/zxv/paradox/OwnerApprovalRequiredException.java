// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.paradox;

import org.zedec.zxv.ZxvRefusedException;

/**
 * The action needs the owner's approval ({@code ZXV_E_PARADOX_OWNER}, -40, class REFUSED): a
 * registry entry flagged {@link ParadoxFlag#MONEY} or {@link ParadoxFlag#DEVICE} was added without
 * {@code ownerOk}. Nothing changed. "Owner ok" is the caller's assertion: inside one process this
 * library cannot check who the owner is.
 */
public class OwnerApprovalRequiredException extends ZxvRefusedException {
    private static final long serialVersionUID = 1L;

    /**
     * Creates the exception.
     *
     * @param status     the negative status code (-40)
     * @param statusName its symbolic name ({@code ZXV_E_PARADOX_OWNER})
     * @param detail     the diagnostic detail from {@code zxv_last_error}
     */
    public OwnerApprovalRequiredException(int status, String statusName, String detail) {
        super(status, statusName, detail);
    }
}
