// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.web4;

/** Consent scopes ({@code ZXV_W4_CONSENT_*}), combined with bitwise OR. */
public final class ConsentScope {
    private ConsentScope() {}

    /** Moving money, up to the token's maximum. */
    public static final int MONEY = 0x01;
    /** Sharing personal data. */
    public static final int PII = 0x02;
}
