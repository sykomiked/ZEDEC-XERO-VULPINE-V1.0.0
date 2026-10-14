// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.cid;

/** How {@link Cid#toText(CidBase)} spells a CID ({@code ZXV_CID_BASE_*}). */
public enum CidBase {
    /** What Kubo prints: CIDv1 as "b" + base32 lower case, CIDv0 as "Qm..." base58btc. */
    DEFAULT(0),
    /**
     * "z" + base58btc, the AIPI form (also {@code ipfs cid format -b base58btc}). CIDv1
     * only: a CIDv0 has no multibase form and is refused with {@link CidException}.
     */
    BASE58BTC(1);

    private final int code;

    CidBase(int code) {
        this.code = code;
    }

    /** The C value. */
    public int code() {
        return code;
    }
}
