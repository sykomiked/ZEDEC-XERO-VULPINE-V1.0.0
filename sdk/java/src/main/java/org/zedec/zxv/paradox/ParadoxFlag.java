// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.paradox;

/** Flags of a policy, a registry entry or a draft ({@code ZXV_PDX_FLAG_*}). */
public final class ParadoxFlag {
    private ParadoxFlag() {}

    /** Touches money: the owner decides; never changed automatically. */
    public static final int MONEY = 1;
    /** Touches devices: the owner decides; never changed automatically. */
    public static final int DEVICE = 2;
}
