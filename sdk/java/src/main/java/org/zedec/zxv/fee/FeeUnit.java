// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.fee;

/** The unit a gross amount is measured in ({@code ZXV_FEE_UNIT_*}), in index order. */
public enum FeeUnit {
    /** Minor units of a ledger asset. */
    MONEY,
    /** Token-cycles. */
    COMPUTE,
    /** Bytes. */
    BANDWIDTH,
    /** Byte-hours. */
    STORAGE;

    /** The unit's index (0..3). */
    public int index() {
        return ordinal();
    }
}
