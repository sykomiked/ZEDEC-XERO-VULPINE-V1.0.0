// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.spring.web;

import org.springframework.core.convert.converter.Converter;
import org.zedec.zxv.Rational;

/**
 * Converts request parameters and path variables to {@link Rational} with
 * {@link Rational#parse(String)}: {@code ?amount=0.1} is exactly 1/10, and
 * {@code ?amount=1e-1} is refused (400), as in JSON.
 */
public final class StringToRationalConverter implements Converter<String, Rational> {
    /** Creates the converter. */
    public StringToRationalConverter() {
    }

    @Override
    public Rational convert(String source) {
        return Rational.parse(source);
    }
}
