// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.fee;

/** The four buckets a fee is split into ({@code ZXV_FEE_*}), in index order. */
public enum FeeBucket {
    /** 50%, plus every remainder unit (backs Vino). */
    RESERVE_FLOOR,
    /** 25%: the V-Bill dividend pool. */
    VBILL_DIVIDEND,
    /** 15%: infrastructure and node bounties. */
    INFRA_BOUNTY,
    /** 10%: regenerative capital. */
    REGEN_CAPITAL;

    /** The bucket's index (0..3). */
    public int index() {
        return ordinal();
    }
}
