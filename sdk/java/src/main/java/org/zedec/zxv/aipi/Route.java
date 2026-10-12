// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.aipi;

/**
 * The route a call of a function would take ({@code ZXV_AIPI_ROUTE_*}). This
 * library is offline, so {@link AipiNode#plan} only ever reports {@link #LOCAL} or
 * {@link #UNAVAILABLE}; the other values are the kernel's routes for a networked host,
 * listed so their numbers are fixed.
 */
public enum Route {
    /** Answered from a result cache (networked hosts only). */
    CACHE(1),
    /** The module (and contract) are held here: it can run locally. */
    LOCAL(2),
    /** Fetch the code, then run locally (networked hosts only). */
    FETCH_LOCAL(3),
    /** Run on the mesh (networked hosts only). */
    MESH(4),
    /** The code is not held here, and there is no network to fetch it. */
    UNAVAILABLE(5);

    private final int code;

    Route(int code) {
        this.code = code;
    }

    /** The C value. */
    public int code() {
        return code;
    }

    /**
     * The value for a C code.
     *
     * @throws org.zedec.zxv.ZxvUsageException ZXV_E_ARG for a code that is not listed
     */
    public static Route fromCode(int code) {
        for (Route v : values()) if (v.code == code) return v;
        throw new org.zedec.zxv.ZxvUsageException(-1, "ZXV_E_ARG", "not a Route code: " + code);
    }
}
