// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.ledger;

/** Ledger account kinds ({@code zxv_account_kind}). */
public enum AccountKind {
    /** May never go below zero: no debt. */
    HOLDER,
    /** May carry a CREDIT (777) balance: issuance, nostro. */
    ISSUER;

    static AccountKind fromCode(int c) {
        return c == 1 ? ISSUER : HOLDER;
    }
}
