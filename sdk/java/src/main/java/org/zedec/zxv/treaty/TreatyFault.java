// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.treaty;

/**
 * Why a treaty was refused ({@code ZXV_TREATY_FAULT_*}). Every parse, emit and
 * validation checks the rules R1-R4 of the treaty format and the spelling
 * rules; the first rule broken is reported here (see {@link TreatyException}).
 */
public enum TreatyFault {
    /** Not reported: the refusal came from a call that gives no fault code. */
    UNKNOWN(-1),
    /** No fault: the treaty validates. */
    NONE(0),
    /** The record does not start with "ZXT1". */
    MAGIC(2),
    /** Sutra text outside the grammar (including non-ASCII text, or anything after "END\n"). */
    SYNTAX(3),
    /** The treaty name is not 1..30 of A-Z, 0-9 and '-', starting with a letter. */
    NAME(4),
    /** The asset is not three capital letters. */
    ASSET(5),
    /** The operation is not NET or SETL. */
    OP(6),
    /** The party count is not 1..16, or the obligation count not 0..64. */
    COUNT(7),
    /** A malformed number (leading zeros, "-0"), or one outside PIC S9(18). */
    NUMBER(8),
    /** A bad, duplicate or unknown party id. */
    PARTY(9),
    /** Rule R1: a weight is not num/den in lowest terms with 1 &lt;= den, num &lt;= den, or the weights do not add up to exactly 1. */
    WEIGHT(10),
    /** Rule R2: a party claims more than it deposits. */
    CLAIM(11),
    /** An obligation to oneself, or one that is not above zero. */
    OWE(12),
    /** An unused record slot or field is not blank (not what COBOL's INITIALIZE leaves). */
    PAD(13);

    private final int code;

    TreatyFault(int code) {
        this.code = code;
    }

    /** The {@code ZXV_TREATY_FAULT_*} value (-1 for {@link #UNKNOWN}). */
    public int code() {
        return code;
    }

    /** The fault for a C value; {@link #UNKNOWN} for anything not listed. */
    public static TreatyFault fromCode(int code) {
        for (TreatyFault f : values()) if (f.code == code) return f;
        return UNKNOWN;
    }
}
