// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.paradox;

import org.zedec.zxv.internal.Errors;

/** Graph node states ({@code ZXV_PDX_NS_*}). Final: CLEAR, CONTAINED, ROLLED_BACK, TAINTED, APPLIED. */
public enum NodeState {
    /** Not resolved yet. */
    OPEN(0),
    /** Kind NONE: not a paradox. */
    CLEAR(1),
    /** Destructive: contained (sticky). */
    CONTAINED(2),
    /** Forbidden: refused and rolled back (sticky). */
    ROLLED_BACK(3),
    /** Depends on contained work (sticky). */
    TAINTED(4),
    /** Special: recorded, awaiting review. */
    SANDBOXED(5),
    /** Depends on a sandboxed node. */
    WAITING(6),
    /** Neutral: branches kept. */
    HELD(7),
    /** Constructive, reached by held or waiting work. */
    BLOCKED(8),
    /** Constructive: applied. */
    APPLIED(9);

    private final int code;

    NodeState(int code) {
        this.code = code;
    }

    /** The value's code. */
    public int code() {
        return code;
    }

    /**
     * The value with {@code code}.
     *
     * @throws org.zedec.zxv.ZxvUsageException ZXV_E_ARG for an unknown code
     */
    public static NodeState fromCode(int code) {
        for (NodeState v : values()) if (v.code == code) return v;
        throw Errors.usage(-1, "unknown NodeState " + code);
    }
}
