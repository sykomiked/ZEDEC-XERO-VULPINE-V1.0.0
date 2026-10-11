// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.spring.web;

import org.springframework.boot.autoconfigure.AutoConfiguration;
import org.springframework.boot.autoconfigure.condition.ConditionalOnBean;
import org.springframework.boot.autoconfigure.condition.ConditionalOnClass;
import org.springframework.boot.autoconfigure.condition.ConditionalOnMissingBean;
import org.springframework.boot.autoconfigure.condition.ConditionalOnWebApplication;
import org.springframework.context.annotation.Bean;
import org.springframework.web.servlet.DispatcherServlet;
import org.zedec.zxv.spring.ZxvAutoConfiguration;
import org.zedec.zxv.spring.ZxvFatalHandler;

/** Servlet (Spring MVC) integration: fatal errors are rethrown, {@code Rational} request parameters parse exactly. */
@AutoConfiguration(after = ZxvAutoConfiguration.class)
@ConditionalOnWebApplication(type = ConditionalOnWebApplication.Type.SERVLET)
@ConditionalOnClass(DispatcherServlet.class)
public class ZxvWebMvcAutoConfiguration {
    /** Creates the auto-configuration. */
    public ZxvWebMvcAutoConfiguration() {
    }

    /** Rethrows {@code ZxvFatalError} out of Spring MVC after tripping the fatal handler. */
    @Bean
    @ConditionalOnBean(ZxvFatalHandler.class)
    @ConditionalOnMissingBean
    public ZxvFatalExceptionResolver zxvFatalExceptionResolver(ZxvFatalHandler handler) {
        return new ZxvFatalExceptionResolver(handler);
    }

    /** {@code String} to {@code Rational} for {@code @RequestParam} and {@code @PathVariable}. */
    @Bean
    @ConditionalOnMissingBean
    public StringToRationalConverter zxvStringToRationalConverter() {
        return new StringToRationalConverter();
    }
}
