// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.spring.jackson;

import com.fasterxml.jackson.core.JsonParser;
import com.fasterxml.jackson.core.JsonToken;
import com.fasterxml.jackson.databind.DeserializationContext;
import com.fasterxml.jackson.databind.deser.std.StdDeserializer;
import com.fasterxml.jackson.databind.exc.InvalidFormatException;
import java.io.IOException;
import org.zedec.zxv.Rational;
import org.zedec.zxv.ZxvException;

/**
 * Reads a {@link Rational} from exact JSON only:
 *
 * <ul>
 *   <li>a string in the SDK's exact syntax: {@code "12.75"}, {@code "-0.1"},
 *       {@code "3/4"}, {@code "42"} (exponents, spaces and {@code "NaN"} are refused);</li>
 *   <li>a JSON integer that fits in 64 bits ({@code 42}).</li>
 * </ul>
 *
 * <p>A JSON floating-point number ({@code 0.1}, {@code 1e3}) is always
 * refused, even when it looks harmless: the client may already have rounded
 * it through a binary double, and ZXV accepts floats in exactly one,
 * explicitly named place ({@link Rational#fromDoubleExact(double)}).
 * Failures are {@link InvalidFormatException}s, which Spring MVC answers
 * with 400.
 */
public final class RationalDeserializer extends StdDeserializer<Rational> {
    private static final long serialVersionUID = 1L;

    /** Creates the deserializer. */
    public RationalDeserializer() {
        super(Rational.class);
    }

    @Override
    public Rational deserialize(JsonParser p, DeserializationContext ctxt) throws IOException {
        JsonToken t = p.currentToken();
        if (t == JsonToken.VALUE_STRING) {
            String text = p.getText();
            try {
                return Rational.parse(text);
            } catch (ZxvException e) {
                throw InvalidFormatException.from(p,
                        "not an exact rational (" + e.statusName() + "); use \"12.75\", \"-0.1\", \"3/4\" or \"42\"",
                        text, Rational.class);
            }
        }
        if (t == JsonToken.VALUE_NUMBER_INT) {
            if (p.getNumberType() != JsonParser.NumberType.INT && p.getNumberType() != JsonParser.NumberType.LONG) {
                throw InvalidFormatException.from(p, "integer does not fit in 64 bits", p.getText(), Rational.class);
            }
            return Rational.of(p.getLongValue());
        }
        if (t == JsonToken.VALUE_NUMBER_FLOAT) {
            throw InvalidFormatException.from(p,
                    "JSON floating-point numbers are refused for exact values; send a string such as \"0.1\" or \"1/3\"",
                    p.getText(), Rational.class);
        }
        return (Rational) ctxt.handleUnexpectedToken(Rational.class, p);
    }
}
