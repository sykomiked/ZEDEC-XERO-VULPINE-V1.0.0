// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.spring;

import java.util.Map;
import java.util.Set;
import org.springframework.boot.autoconfigure.AutoConfiguration;
import org.springframework.boot.autoconfigure.condition.ConditionalOnMissingBean;
import org.springframework.boot.autoconfigure.condition.ConditionalOnProperty;
import org.springframework.boot.context.properties.EnableConfigurationProperties;
import org.springframework.context.ApplicationEventPublisher;
import org.springframework.context.annotation.Bean;
import org.zedec.zxv.Capability;
import org.zedec.zxv.Permission;
import org.zedec.zxv.ZxvContext;

/**
 * Creates one {@link ZxvContext} for the application, its root
 * {@link Capability}, and one capability bean per entry under
 * {@code zxv.capabilities}.
 *
 * <p>Shutdown follows the C ABI's destroy order (ABI.md R6): every named
 * capability is a dependent of the root capability, which is a dependent of
 * the context, so Spring closes the named capabilities first, then the root,
 * then the context.
 *
 * <p>The root capability holds {@code zxv.root-permissions} (default: all)
 * and is registered with {@code autowireCandidate = false}, so injecting a
 * {@code Capability} by type only ever finds the narrow, named ones.
 */
@AutoConfiguration
@ConditionalOnProperty(prefix = ZxvProperties.PREFIX, name = "enabled", matchIfMissing = true)
@EnableConfigurationProperties(ZxvProperties.class)
public class ZxvAutoConfiguration {
    /** The bean name of the managed context. */
    public static final String CONTEXT_BEAN = "zxvContext";
    /** The bean name of the root capability (not an autowire candidate). */
    public static final String ROOT_CAPABILITY_BEAN = "zxvRootCapability";
    /** Prefix of the bean names of named capabilities ({@code zxv.capability.teller}). */
    public static final String CAPABILITY_BEAN_PREFIX = "zxv.capability.";

    /** Creates the auto-configuration. */
    public ZxvAutoConfiguration() {
    }

    /**
     * The managed context: created with {@code zxv.fatal-policy}, closed
     * last when the application context shuts down.
     */
    @Bean(name = CONTEXT_BEAN, destroyMethod = "close")
    @ConditionalOnMissingBean(ZxvContext.class)
    public ZxvContext zxvContext(ZxvProperties properties) {
        NativeLibrary.ensureLoaded(properties.getLibrary());
        return ZxvContext.create(properties.getFatalPolicy());
    }

    /**
     * The root capability. It enables {@code zxv.currencies} and registers
     * {@code zxv.private-currencies} (both need {@code LEDGER_POST}).
     */
    @Bean(name = ROOT_CAPABILITY_BEAN, destroyMethod = "close", autowireCandidate = false)
    public Capability zxvRootCapability(ZxvContext context, ZxvProperties properties) {
        Set<Permission> perms = properties.getRootPermissions();
        Capability root = context.rootCapability(perms);
        try {
            if (!properties.getCurrencies().isEmpty() || !properties.getPrivateCurrencies().isEmpty()) {
                if (!perms.contains(Permission.LEDGER_POST)) {
                    throw new IllegalStateException("zxv.currencies and zxv.private-currencies need LEDGER_POST"
                            + " in zxv.root-permissions");
                }
            }
            for (String code : properties.getCurrencies()) {
                context.enableCurrency(root, code);
            }
            for (Map.Entry<String, Integer> e : properties.getPrivateCurrencies().entrySet()) {
                context.registerPrivateCurrency(root, e.getKey(), e.getValue());
            }
            return root;
        } catch (RuntimeException | Error e) {
            root.close();
            throw e;
        }
    }

    /** Registers one capability bean per {@code zxv.capabilities.<name>}. */
    @Bean
    public static ZxvCapabilityRegistrar zxvCapabilityRegistrar() {
        return new ZxvCapabilityRegistrar();
    }

    /**
     * Turns a {@link org.zedec.zxv.ZxvFatalError} into a broken liveness
     * state and a refusing readiness state.
     */
    @Bean
    @ConditionalOnMissingBean
    public ZxvFatalHandler zxvFatalHandler(ApplicationEventPublisher publisher) {
        return new ZxvFatalHandler(publisher);
    }
}
