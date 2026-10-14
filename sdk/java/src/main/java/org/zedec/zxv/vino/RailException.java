// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.vino;

import org.zedec.zxv.ZxvRefusedException;

/**
 * The voucher is not on the rail the call named ({@code ZXV_E_RAIL}, -46, class REFUSED).
 *
 * <p>A play-lane voucher sits on exactly one active rail at a time: {@link Rail#BEARER} (the note in
 * the holder's hand) or {@link Rail#GHOST} (its twin on the play ledger). A transfer must name the
 * active rail, so the same voucher can never be spent once as a note and again as its ledger twin.
 * {@link PlaySandbox#setRail} also raises this when the voucher is already on the rail asked for.
 * This is a refusal, not a fault: nothing changed. Read the voucher's rail with
 * {@link PlaySandbox#voucher(long)} and try again on that rail.
 */
public class RailException extends ZxvRefusedException {
    private static final long serialVersionUID = 1L;

    /**
     * Creates the exception. The SDK builds it from a native status; applications normally only
     * catch it.
     *
     * @param status     the negative status code (-46)
     * @param statusName its symbolic name ({@code ZXV_E_RAIL})
     * @param detail     the diagnostic detail from {@code zxv_last_error}
     */
    public RailException(int status, String statusName, String detail) {
        super(status, statusName, detail);
    }
}
