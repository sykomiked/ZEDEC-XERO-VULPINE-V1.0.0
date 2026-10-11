// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.spring.actuate;

import java.util.Map;
import java.util.stream.Collectors;
import org.springframework.beans.factory.ListableBeanFactory;
import org.springframework.beans.factory.ObjectProvider;
import org.springframework.boot.actuate.autoconfigure.health.ConditionalOnEnabledHealthIndicator;
import org.springframework.boot.actuate.health.HealthIndicator;
import org.springframework.boot.autoconfigure.AutoConfiguration;
import org.springframework.boot.autoconfigure.condition.ConditionalOnBean;
import org.springframework.boot.autoconfigure.condition.ConditionalOnClass;
import org.springframework.boot.autoconfigure.condition.ConditionalOnMissingBean;
import org.springframework.context.annotation.Bean;
import org.zedec.zxv.Capability;
import org.zedec.zxv.ZxvContext;
import org.zedec.zxv.spring.ZxvAutoConfiguration;
import org.zedec.zxv.spring.ZxvFatalHandler;

/**
 * Adds the {@code zxv} health contributor when Spring Boot Actuator is on
 * the class path. Turn it off with {@code management.health.zxv.enabled=false}.
 */
@AutoConfiguration(after = ZxvAutoConfiguration.class)
@ConditionalOnClass(HealthIndicator.class)
@ConditionalOnBean(ZxvContext.class)
@ConditionalOnEnabledHealthIndicator("zxv")
public class ZxvHealthContributorAutoConfiguration {
    /** Creates the auto-configuration. */
    public ZxvHealthContributorAutoConfiguration() {
    }

    /** The indicator; its bean name makes the contributor {@code zxv}. */
    @Bean
    @ConditionalOnMissingBean(name = "zxvHealthIndicator")
    public ZxvHealthIndicator zxvHealthIndicator(ZxvContext context, ListableBeanFactory beans,
            ObjectProvider<ZxvFatalHandler> fatalHandler) {
        // Only the named capabilities (zxv.capability.<name>), never the root.
        Map<String, Capability> named = beans.getBeansOfType(Capability.class).entrySet().stream()
                .filter(e -> e.getKey().startsWith(ZxvAutoConfiguration.CAPABILITY_BEAN_PREFIX))
                .collect(Collectors.toMap(Map.Entry::getKey, Map.Entry::getValue));
        return new ZxvHealthIndicator(context, named, fatalHandler.getIfAvailable());
    }
}
