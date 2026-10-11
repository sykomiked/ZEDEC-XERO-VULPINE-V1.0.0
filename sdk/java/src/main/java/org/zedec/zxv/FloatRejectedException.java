// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv;

/**
 * A host floating-point value was refused (ZXV_E_FLOAT, USAGE class): NaN or
 * an infinity reached {@link Rational#fromDoubleExact(double)}. ZXV accepts
 * floats in exactly that one, explicitly named place.
 */
public class FloatRejectedException extends ZxvUsageException {
    private static final long serialVersionUID = 1L;

    /**
     * Creates the exception.
     *
     * @param detail the diagnostic detail
     */
    public FloatRejectedException(String detail) {
        super(-24, "ZXV_E_FLOAT", detail);
    }
}
