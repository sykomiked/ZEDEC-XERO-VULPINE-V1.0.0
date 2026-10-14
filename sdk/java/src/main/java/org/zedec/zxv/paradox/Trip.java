// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.paradox;

/** Which checks fired: a RepairProof's trips and the quarantine reason ({@code ZXV_PDX_TRIP_*}). */
public final class Trip {
    private Trip() {}

    /** steps reached the step limit. */
    public static final int STEPS = 1;
    /** spent reached the budget. */
    public static final int BUDGET = 2;
    /** A state fingerprint repeated. */
    public static final int CYCLE = 4;
    /** The caller's invariant result was false. */
    public static final int INVARIANT = 8;
    /** The tag was already forbidden. */
    public static final int FORBIDDEN = 16;
}
