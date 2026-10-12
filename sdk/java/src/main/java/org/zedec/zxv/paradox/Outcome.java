// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.paradox;

import org.zedec.zxv.internal.Errors;

/** Operator outcomes ({@code ZXV_PDX_OUT_*}). REFUSED and PROPOSAL_FOR_OWNER are decisions, not errors. */
public enum Outcome {
    /** A constructive update was applied. */
    APPLIED(0),
    /** An applied update survived evaluation. */
    KEPT(1),
    /** Out of bounds or unapproved: nothing changed; the owner decides. */
    PROPOSAL_FOR_OWNER(2),
    /** Tension grew, or the tag was forbidden: restored. */
    ROLLED_BACK(3),
    /** Destructive: quarantined and rolled back. */
    CONTAINED(4),
    /** Neutral: still holding every branch. */
    DEFERRED(5),
    /** Neutral: one branch chosen. */
    SETTLED(6),
    /** Special: a frontier record was written. */
    RECORDED(7),
    /** A reviewed draft joined the registry. */
    ACCEPTED(8),
    /** Graph: more passes needed. */
    PENDING(9),
    /** Graph: every node is final. */
    RESOLVED(10),
    /** Graph: the step limit was reached. */
    ESCALATE_TO_OWNER(11),
    /** Wrong kind: nothing changed. */
    REFUSED(12),
    /** Never reported (a full log is ZXV_E_CAPACITY). */
    LOG_FULL(13);

    private final int code;

    Outcome(int code) {
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
    public static Outcome fromCode(int code) {
        for (Outcome v : values()) if (v.code == code) return v;
        throw Errors.usage(-1, "unknown Outcome " + code);
    }
}
