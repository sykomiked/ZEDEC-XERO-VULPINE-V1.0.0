// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.spring.jackson;

import com.fasterxml.jackson.databind.DeserializationFeature;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.springframework.boot.autoconfigure.AutoConfiguration;
import org.springframework.boot.autoconfigure.condition.ConditionalOnClass;
import org.springframework.boot.autoconfigure.condition.ConditionalOnMissingBean;
import org.springframework.boot.autoconfigure.condition.ConditionalOnProperty;
import org.springframework.boot.autoconfigure.jackson.Jackson2ObjectMapperBuilderCustomizer;
import org.springframework.boot.autoconfigure.jackson.JacksonAutoConfiguration;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.http.converter.json.Jackson2ObjectMapperBuilder;

/**
 * Registers {@link ZxvJacksonModule} with Spring Boot's {@code ObjectMapper},
 * and turns off Jackson's {@code ACCEPT_FLOAT_AS_INT} (on by default), which
 * would otherwise silently truncate {@code 150.7} to {@code 150} for an
 * integer field such as an amount in minor units. Set
 * {@code zxv.jackson.strict-integers=false} to keep Jackson's default.
 */
@AutoConfiguration(before = JacksonAutoConfiguration.class)
@ConditionalOnClass(ObjectMapper.class)
public class ZxvJacksonAutoConfiguration {
    /** Creates the auto-configuration. */
    public ZxvJacksonAutoConfiguration() {
    }

    /** The module; Boot's Jackson auto-configuration picks up every Module bean. */
    @Bean
    @ConditionalOnMissingBean
    public ZxvJacksonModule zxvJacksonModule() {
        return new ZxvJacksonModule();
    }

    /** Refuses JSON floats for integer fields. */
    @Configuration(proxyBeanMethods = false)
    @ConditionalOnClass(Jackson2ObjectMapperBuilder.class)
    @ConditionalOnProperty(prefix = "zxv.jackson", name = "strict-integers", matchIfMissing = true)
    static class StrictIntegers {
        @Bean
        Jackson2ObjectMapperBuilderCustomizer zxvStrictIntegers() {
            return b -> b.featuresToDisable(DeserializationFeature.ACCEPT_FLOAT_AS_INT);
        }
    }
}
