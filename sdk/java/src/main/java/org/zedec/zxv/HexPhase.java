// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv;

/** The lifecycle of tagged work, bits 1-0 ({@code ZXV_HEX_PHASE_*}): a Gray cycle POTENTIAL, EMERGING, MANIFEST, DISSOLVING, then POTENTIAL again. */
public enum HexPhase {
    /** Code 0 (00). */
    POTENTIAL(0),
    /** Code 1 (01). */
    EMERGING(1),
    /** Code 2 (10). */
    DISSOLVING(2),
    /** Code 3 (11). */
    MANIFEST(3);

    private final int code;

    HexPhase(int code) {
        this.code = code;
    }

    /** The numeric code (bits of a {@link Hexagram} tag). */
    public int code() {
        return code;
    }

    /**
     * The value for a code.
     *
     * @throws ZxvUsageException for anything outside the enum
     */
    public static HexPhase fromCode(int code) {
        for (HexPhase v : values()) if (v.code == code) return v;
        throw new ZxvUsageException(-1, "ZXV_E_ARG", "not a HexPhase code: " + code);
    }

    /** The next phase on the cycle ({@code zxv_hex_phase_next}). */
    public HexPhase next() {
        return fromCode(Hexagram.nextPhaseCode(code));
    }
}
