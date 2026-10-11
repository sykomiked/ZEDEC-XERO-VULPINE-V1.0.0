// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.spring;

import java.util.EnumSet;
import java.util.Map;
import java.util.Set;
import org.springframework.beans.BeansException;
import org.springframework.beans.factory.annotation.Qualifier;
import org.springframework.beans.factory.config.ConfigurableListableBeanFactory;
import org.springframework.beans.factory.config.RuntimeBeanReference;
import org.springframework.beans.factory.support.AutowireCandidateQualifier;
import org.springframework.beans.factory.support.BeanDefinitionRegistry;
import org.springframework.beans.factory.support.BeanDefinitionRegistryPostProcessor;
import org.springframework.beans.factory.support.RootBeanDefinition;
import org.springframework.boot.context.properties.bind.Bindable;
import org.springframework.boot.context.properties.bind.Binder;
import org.springframework.context.EnvironmentAware;
import org.springframework.core.env.Environment;
import org.zedec.zxv.Capability;
import org.zedec.zxv.Permission;

/**
 * Registers one {@link Capability} bean per {@code zxv.capabilities.<name>},
 * named {@code zxv.capability.<name>} and qualified {@code <name>}:
 *
 * <pre>{@code
 * public TellerService(@Qualifier("teller") Capability teller) { ... }
 * }</pre>
 *
 * <p>Each one is attenuated from the root capability, which is wired in as
 * a bean reference and a {@code depends-on}, so Spring destroys it before the
 * root (ABI.md R6: an attenuated cap before its parent).
 */
public final class ZxvCapabilityRegistrar implements BeanDefinitionRegistryPostProcessor, EnvironmentAware {
    private Environment environment;

    /** Creates the registrar. */
    public ZxvCapabilityRegistrar() {
    }

    @Override
    public void setEnvironment(Environment environment) {
        this.environment = environment;
    }

    @Override
    public void postProcessBeanDefinitionRegistry(BeanDefinitionRegistry registry) throws BeansException {
        Map<String, ZxvProperties.CapabilitySpec> specs = Binder.get(environment)
                .bind(ZxvProperties.PREFIX + ".capabilities",
                        Bindable.mapOf(String.class, ZxvProperties.CapabilitySpec.class))
                .orElse(Map.of());
        for (Map.Entry<String, ZxvProperties.CapabilitySpec> e : specs.entrySet()) {
            String name = e.getKey();
            Set<Permission> perms = e.getValue().getPermissions();
            RootBeanDefinition bd = new RootBeanDefinition(ZxvCapabilityRegistrar.class);
            bd.setTargetType(Capability.class);
            bd.setFactoryMethodName("attenuateFromRoot");
            bd.getConstructorArgumentValues().addIndexedArgumentValue(0,
                    new RuntimeBeanReference(ZxvAutoConfiguration.ROOT_CAPABILITY_BEAN));
            bd.getConstructorArgumentValues().addIndexedArgumentValue(1, name);
            bd.getConstructorArgumentValues().addIndexedArgumentValue(2,
                    perms == null || perms.isEmpty() ? EnumSet.noneOf(Permission.class) : EnumSet.copyOf(perms));
            bd.setDependsOn(ZxvAutoConfiguration.ROOT_CAPABILITY_BEAN);
            bd.setDestroyMethodName("close");
            bd.addQualifier(new AutowireCandidateQualifier(Qualifier.class, name));
            registry.registerBeanDefinition(ZxvAutoConfiguration.CAPABILITY_BEAN_PREFIX + name, bd);
        }
    }

    @Override
    public void postProcessBeanFactory(ConfigurableListableBeanFactory beanFactory) throws BeansException {
        // Nothing: the definitions are registered above.
    }

    /**
     * The factory method behind each named capability bean. Fails at
     * start-up, rather than narrowing silently, when the root capability
     * lacks a requested permission or none is requested.
     *
     * @param root  the root capability
     * @param name  the configured name, for messages
     * @param perms the permissions to keep
     * @return a capability attenuated from {@code root}
     */
    public static Capability attenuateFromRoot(Capability root, String name, Set<Permission> perms) {
        if (perms.isEmpty()) {
            throw new IllegalStateException("zxv.capabilities." + name + ".permissions is empty");
        }
        Set<Permission> held = root.permissions();
        if (!held.containsAll(perms)) {
            EnumSet<Permission> missing = EnumSet.copyOf(perms);
            missing.removeAll(held);
            throw new IllegalStateException("zxv.capabilities." + name + " asks for " + missing
                    + ", which the root capability (zxv.root-permissions) does not hold");
        }
        return root.attenuate(perms);
    }
}
