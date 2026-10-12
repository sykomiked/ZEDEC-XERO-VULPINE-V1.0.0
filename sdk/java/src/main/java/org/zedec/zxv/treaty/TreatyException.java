// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.treaty;

import org.zedec.zxv.ZxvUsageException;

/**
 * A treaty fails validation ({@code ZXV_E_TREATY}, -33, class USAGE): fix the
 * treaty; the same input is refused again. {@link #fault()} says which rule
 * was broken and, for a parse, {@link #offset()} is the byte offset of the
 * first fault in the Sutra text or the copybook record.
 */
public class TreatyException extends ZxvUsageException {
    private static final long serialVersionUID = 1L;
    /** The rule that was broken. */
    private final TreatyFault fault;
    /** The byte offset of the first fault of a parse, or -1. */
    private final long offset;

    /**
     * Creates the exception without fault details (as the generic status mapping does).
     *
     * @param status     the negative status code (-33)
     * @param statusName its symbolic name ({@code ZXV_E_TREATY})
     * @param detail     the diagnostic detail from {@code zxv_last_error}
     */
    public TreatyException(int status, String statusName, String detail) {
        this(status, statusName, detail, TreatyFault.UNKNOWN, -1);
    }

    /**
     * Creates the exception.
     *
     * @param status     the negative status code (-33)
     * @param statusName its symbolic name ({@code ZXV_E_TREATY})
     * @param detail     the diagnostic detail from {@code zxv_last_error}
     * @param fault      the rule that was broken
     * @param offset     the byte offset of the first fault for a parse, else -1
     */
    public TreatyException(int status, String statusName, String detail, TreatyFault fault, long offset) {
        super(status, statusName, detail);
        this.fault = fault == null ? TreatyFault.UNKNOWN : fault;
        this.offset = offset;
    }

    /** The rule that was broken ({@link TreatyFault#UNKNOWN} if the call gave none). */
    public TreatyFault fault() {
        return fault;
    }

    /** The byte offset of the first fault in the parsed input, or -1 when this was not a parse. */
    public long offset() {
        return offset;
    }
}
