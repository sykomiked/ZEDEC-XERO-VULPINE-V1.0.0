// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.spring.jackson;

import com.fasterxml.jackson.core.JsonGenerator;
import com.fasterxml.jackson.databind.SerializerProvider;
import com.fasterxml.jackson.databind.ser.std.StdSerializer;
import java.io.IOException;
import org.zedec.zxv.Rational;

/**
 * Writes a {@link Rational} as a JSON string in its exact form:
 * {@code "12.75"} for a terminating decimal, {@code "1/3"} otherwise. Never a
 * JSON number, so no reader can round it through a binary float.
 */
public final class RationalSerializer extends StdSerializer<Rational> {
    private static final long serialVersionUID = 1L;

    /** Creates the serializer. */
    public RationalSerializer() {
        super(Rational.class);
    }

    @Override
    public void serialize(Rational value, JsonGenerator gen, SerializerProvider provider) throws IOException {
        gen.writeString(value.toString());
    }
}
