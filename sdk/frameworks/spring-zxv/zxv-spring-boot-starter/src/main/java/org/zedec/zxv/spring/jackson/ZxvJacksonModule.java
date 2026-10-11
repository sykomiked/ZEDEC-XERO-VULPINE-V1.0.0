// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.spring.jackson;

import com.fasterxml.jackson.core.Version;
import com.fasterxml.jackson.databind.module.SimpleModule;
import org.zedec.zxv.Rational;

/**
 * Jackson support for ZXV value types: {@link Rational} as an exact string
 * ({@link RationalSerializer}, {@link RationalDeserializer}). Spring Boot
 * registers it with the application's {@code ObjectMapper}; outside Spring,
 * {@code mapper.registerModule(new ZxvJacksonModule())}.
 */
public final class ZxvJacksonModule extends SimpleModule {
    private static final long serialVersionUID = 1L;

    /** Creates the module. */
    public ZxvJacksonModule() {
        super("zxv", new Version(1, 1, 0, null, "org.zedec", "zxv-spring-boot-starter"));
        addSerializer(Rational.class, new RationalSerializer());
        addDeserializer(Rational.class, new RationalDeserializer());
    }
}
