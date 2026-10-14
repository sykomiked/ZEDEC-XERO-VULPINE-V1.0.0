// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.web4;

import org.zedec.zxv.internal.Errors;

/** Envelope kinds ({@code ZXV_W4_MSG_*}). */
public enum EnvelopeKind {
    /** A request to call a tool. */
    REQUEST(1),
    /** A response. */
    RESPONSE(2),
    /** One chunk of a streamed answer. */
    STREAM(3),
    /** Cancels a request. */
    CANCEL(4),
    /** An error answer. */
    ERROR(5);

    private final int code;

    EnvelopeKind(int code) {
        this.code = code;
    }

    /** The kind's code. */
    public int code() {
        return code;
    }

    /**
     * The kind with {@code code}.
     *
     * @throws org.zedec.zxv.ZxvUsageException ZXV_E_ARG for an unknown kind
     */
    public static EnvelopeKind fromCode(int code) {
        for (EnvelopeKind k : values()) if (k.code == code) return k;
        throw Errors.usage(-1, "unknown envelope kind " + code);
    }
}
