// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.spring;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import com.fasterxml.jackson.databind.DeserializationFeature;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.exc.InvalidFormatException;
import com.fasterxml.jackson.databind.exc.MismatchedInputException;
import java.util.List;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.springframework.boot.autoconfigure.AutoConfigurations;
import org.springframework.boot.autoconfigure.jackson.JacksonAutoConfiguration;
import org.springframework.boot.test.context.runner.ApplicationContextRunner;
import org.zedec.zxv.Rational;
import org.zedec.zxv.spring.jackson.ZxvJacksonAutoConfiguration;

/** Rational in JSON: exact strings out, exact strings or integers in, floats refused. */
class RationalJacksonTest {
    record Order(Rational price, Rational quantity) {}

    record Payment(long amountMinor) {}

    private ObjectMapper mapper;

    @BeforeEach
    void bootMapper() {
        new ApplicationContextRunner()
                .withConfiguration(AutoConfigurations.of(JacksonAutoConfiguration.class, ZxvJacksonAutoConfiguration.class))
                // Even with the switch that turns JSON floats into BigDecimal, floats stay refused.
                .withPropertyValues("spring.jackson.deserialization.use-big-decimal-for-floats=true")
                .run(ctx -> mapper = ctx.getBean(ObjectMapper.class));
    }

    @Test
    void writesTheExactString() throws Exception {
        assertThat(mapper.writeValueAsString(Rational.parse("0.1").add(Rational.parse("0.2")))).isEqualTo("\"0.3\"");
        assertThat(mapper.writeValueAsString(Rational.of(1, 3))).isEqualTo("\"1/3\"");
        assertThat(mapper.writeValueAsString(Rational.of(-51, 4))).isEqualTo("\"-12.75\"");
        assertThat(mapper.writeValueAsString(List.of(Rational.of(42)))).isEqualTo("[\"42\"]");
    }

    @Test
    void readsExactStringsAndIntegers() throws Exception {
        assertThat(mapper.readValue("\"0.1\"", Rational.class)).isEqualTo(Rational.of(1, 10));
        assertThat(mapper.readValue("\"3/4\"", Rational.class)).isEqualTo(Rational.of(3, 4));
        assertThat(mapper.readValue("\"-12.75\"", Rational.class)).isEqualTo(Rational.of(-51, 4));
        assertThat(mapper.readValue("42", Rational.class)).isEqualTo(Rational.of(42));
        assertThat(mapper.readValue("null", Rational.class)).isNull();
        Order o = mapper.readValue("{\"price\":\"12.75\",\"quantity\":\"1/3\"}", Order.class);
        assertThat(o.price().multiply(o.quantity())).isEqualTo(Rational.of(17, 4));
        assertThat(mapper.readValue(mapper.writeValueAsString(o), Order.class)).isEqualTo(o);
    }

    @Test
    void refusesJsonFloats() {
        for (String json : List.of("0.1", "1.0", "1e3", "-0.0", "12.75")) {
            assertThatThrownBy(() -> mapper.readValue(json, Rational.class))
                    .as(json).isInstanceOf(InvalidFormatException.class)
                    .hasMessageContaining("floating-point numbers are refused");
        }
        assertThatThrownBy(() -> mapper.readValue("{\"price\":12.75,\"quantity\":\"1\"}", Order.class))
                .isInstanceOf(InvalidFormatException.class);
    }

    @Test
    void refusesFloatsForIntegerFields() throws Exception {
        assertThat(mapper.readValue("{\"amountMinor\":15000}", Payment.class).amountMinor()).isEqualTo(15000);
        assertThatThrownBy(() -> mapper.readValue("{\"amountMinor\":150.7}", Payment.class))
                .isInstanceOf(MismatchedInputException.class);
        new ApplicationContextRunner()
                .withConfiguration(AutoConfigurations.of(JacksonAutoConfiguration.class, ZxvJacksonAutoConfiguration.class))
                .withPropertyValues("zxv.jackson.strict-integers=false")
                .run(ctx -> assertThat(ctx.getBean(ObjectMapper.class)
                        .readValue("{\"amountMinor\":150.7}", Payment.class).amountMinor()).isEqualTo(150));
    }

    @Test
    void refusesFloatsEvenWhenTheMapperWouldCoerce() {
        ObjectMapper lenient = mapper.copy().enable(DeserializationFeature.ACCEPT_FLOAT_AS_INT);
        assertThatThrownBy(() -> lenient.readValue("0.5", Rational.class)).isInstanceOf(InvalidFormatException.class);
    }

    @Test
    void refusesInexactOrMalformedText() {
        for (String json : List.of("\"1e3\"", "\"NaN\"", "\"Infinity\"", "\" 1\"", "\".5\"", "\"1/0\"", "\"abc\"", "\"\"")) {
            assertThatThrownBy(() -> mapper.readValue(json, Rational.class))
                    .as(json).isInstanceOf(InvalidFormatException.class);
        }
        assertThatThrownBy(() -> mapper.readValue("123456789012345678901234567890", Rational.class))
                .isInstanceOf(InvalidFormatException.class).hasMessageContaining("64 bits");
        assertThatThrownBy(() -> mapper.readValue("true", Rational.class)).isInstanceOf(MismatchedInputException.class);
        assertThatThrownBy(() -> mapper.readValue("[1]", Rational.class)).isInstanceOf(MismatchedInputException.class);
    }
}
