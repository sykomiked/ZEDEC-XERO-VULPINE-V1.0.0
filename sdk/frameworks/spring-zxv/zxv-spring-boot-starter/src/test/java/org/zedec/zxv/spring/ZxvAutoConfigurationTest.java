// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.spring;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import java.util.List;
import java.util.concurrent.CopyOnWriteArrayList;
import org.junit.jupiter.api.Test;
import org.springframework.beans.factory.NoSuchBeanDefinitionException;
import org.springframework.beans.factory.annotation.Qualifier;
import org.springframework.beans.factory.config.DestructionAwareBeanPostProcessor;
import org.springframework.boot.autoconfigure.AutoConfigurations;
import org.springframework.boot.test.context.runner.ApplicationContextRunner;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.zedec.zxv.Capability;
import org.zedec.zxv.CapabilityDeniedException;
import org.zedec.zxv.FatalPolicy;
import org.zedec.zxv.Permission;
import org.zedec.zxv.Zxv;
import org.zedec.zxv.ZxvContext;
import org.zedec.zxv.ZxvFatalError;
import org.zedec.zxv.ledger.AccountKind;
import org.zedec.zxv.ledger.Ledger;

/** Start-up, capability beans and shutdown order against the real libzxv. */
class ZxvAutoConfigurationTest {
    private final ApplicationContextRunner runner = new ApplicationContextRunner()
            .withConfiguration(AutoConfigurations.of(ZxvAutoConfiguration.class));

    private static final String[] TELLER_AUDITOR = {
        "zxv.capabilities.teller.permissions=LEDGER_READ,LEDGER_POST",
        "zxv.capabilities.auditor.permissions=ledger-read",
    };

    @Test
    void startsALiveContextWithCurrencies() {
        runner.withPropertyValues("zxv.currencies=NGN,KES", "zxv.private-currencies.VFV=2").run(ctx -> {
            assertThat(ctx).hasNotFailed().hasSingleBean(ZxvContext.class);
            ZxvContext zxv = ctx.getBean(ZxvContext.class);
            assertThat(zxv.isPoisoned()).isFalse();
            assertThat(zxv.fatalPolicy()).isEqualTo(FatalPolicy.THROW);
            assertThat(zxv.enabledCurrencies()).containsExactly("NGN", "KES", "VFV");
            assertThat(zxv.currency("VFV")).hasValueSatisfying(c -> assertThat(c.isPrivate()).isTrue());
        });
    }

    @Configuration(proxyBeanMethods = false)
    static class NeedsAnyCapability {
        @Bean
        String holder(Capability cap) {
            return cap.toString();
        }
    }

    @Test
    void rootCapabilityIsNotAnAutowireCandidate() {
        runner.run(ctx -> {
            assertThat(ctx).hasNotFailed();
            assertThat(ctx.getBean(ZxvAutoConfiguration.ROOT_CAPABILITY_BEAN, Capability.class).permissions())
                    .isEqualTo(Permission.all());
        });
        // With no named capabilities, nothing can be injected by type: the root never leaks.
        runner.withUserConfiguration(NeedsAnyCapability.class).run(ctx -> {
            assertThat(ctx).hasFailed();
            assertThat(ctx.getStartupFailure()).rootCause().isInstanceOf(NoSuchBeanDefinitionException.class);
        });
        // With one named capability, injection by type gets that one.
        runner.withUserConfiguration(NeedsAnyCapability.class)
                .withPropertyValues("zxv.capabilities.reader.permissions=LEDGER_READ")
                .run(ctx -> assertThat(ctx).hasNotFailed());
    }

    @Test
    void namedCapabilitiesHoldExactlyTheirPermissions() {
        runner.withPropertyValues("zxv.currencies=NGN").withPropertyValues(TELLER_AUDITOR).run(ctx -> {
            assertThat(ctx).hasNotFailed();
            Capability teller = ctx.getBean("zxv.capability.teller", Capability.class);
            Capability auditor = ctx.getBean("zxv.capability.auditor", Capability.class);
            assertThat(teller.permissions()).containsExactlyInAnyOrder(Permission.LEDGER_READ, Permission.LEDGER_POST);
            assertThat(auditor.permissions()).containsExactly(Permission.LEDGER_READ);
            assertThat(teller.allows(Permission.CRYPTO_SIGN)).isFalse();

            Ledger post = Ledger.of(teller);
            var bank = post.openAccount("bank", "NGN", AccountKind.ISSUER);
            var ada = post.openAccount("ada", "NGN", AccountKind.HOLDER);
            post.post(bank, ada, 150_00, "NGN", "E2E-1");
            assertThat(Ledger.of(auditor).balance(ada).equity()).isEqualTo(150_00);
            assertThatThrownBy(() -> Ledger.of(auditor).post(ada, bank, 1, "NGN", "E2E-2"))
                    .isInstanceOf(CapabilityDeniedException.class);
        });
    }

    @Configuration(proxyBeanMethods = false)
    static class NeedsTeller {
        record Holder(Capability cap) {}

        @Bean
        Holder holder(@Qualifier("teller") Capability teller) {
            return new Holder(teller);
        }
    }

    @Test
    void injectsByQualifier() {
        runner.withUserConfiguration(NeedsTeller.class).withPropertyValues(TELLER_AUDITOR).run(ctx -> {
            assertThat(ctx).hasNotFailed();
            assertThat(ctx.getBean(NeedsTeller.Holder.class).cap())
                    .isSameAs(ctx.getBean("zxv.capability.teller"));
        });
    }

    @Test
    void refusesACapabilityWiderThanTheRoot() {
        runner.withPropertyValues("zxv.root-permissions=LEDGER_READ",
                "zxv.capabilities.signer.permissions=CRYPTO_SIGN").run(ctx -> {
            assertThat(ctx).hasFailed();
            assertThat(ctx.getStartupFailure()).rootCause()
                    .hasMessageContaining("zxv.capabilities.signer asks for [CRYPTO_SIGN]");
        });
    }

    @Test
    void refusesACapabilityWithNoPermissions() {
        runner.withPropertyValues("zxv.capabilities.empty.permissions=").run(ctx -> {
            assertThat(ctx).hasFailed();
            assertThat(ctx.getStartupFailure()).rootCause().hasMessageContaining("permissions is empty");
        });
    }

    @Test
    void refusesAnUnknownPermission() {
        runner.withPropertyValues("zxv.capabilities.x.permissions=LEDGER_READ,ROOT_SHELL")
                .run(ctx -> assertThat(ctx).hasFailed());
    }

    @Test
    void currenciesNeedLedgerPostOnTheRoot() {
        runner.withPropertyValues("zxv.root-permissions=LEDGER_READ", "zxv.currencies=NGN").run(ctx -> {
            assertThat(ctx).hasFailed();
            assertThat(ctx.getStartupFailure()).rootCause().hasMessageContaining("need LEDGER_POST");
        });
    }

    @Test
    void badCurrencyFailsStartup() {
        runner.withPropertyValues("zxv.currencies=XYZ").run(ctx -> assertThat(ctx).hasFailed());
    }

    @Test
    void canBeDisabled() {
        runner.withPropertyValues("zxv.enabled=false").run(ctx -> {
            assertThat(ctx).hasNotFailed().doesNotHaveBean(ZxvContext.class).doesNotHaveBean(ZxvFatalHandler.class);
        });
    }

    @Test
    void bindsTheFatalPolicy() {
        runner.withPropertyValues("zxv.fatal-policy=abort").run(ctx ->
                assertThat(ctx.getBean(ZxvContext.class).fatalPolicy()).isEqualTo(FatalPolicy.ABORT));
    }

    @Test
    void libraryPropertyAcceptsTheLoadedFileAndRejectsAMissingOne() {
        String loaded = System.getProperty("zxv.library");
        Zxv.version(); // make sure it is loaded
        runner.withPropertyValues("zxv.library=" + loaded).run(ctx -> assertThat(ctx).hasNotFailed());
        runner.withPropertyValues("zxv.library=/nonexistent/libzxv.so").run(ctx -> {
            assertThat(ctx).hasFailed();
            assertThat(ctx.getStartupFailure()).rootCause().hasMessageContaining("no file at");
        });
    }

    /** Records the order in which Spring destroys ZXV beans. */
    static final class DestroyRecorder implements DestructionAwareBeanPostProcessor {
        final List<String> order = new CopyOnWriteArrayList<>();

        @Override
        public void postProcessBeforeDestruction(Object bean, String beanName) {
            if (bean instanceof Capability || bean instanceof ZxvContext) order.add(beanName);
        }

        @Override
        public boolean requiresDestruction(Object bean) {
            return bean instanceof Capability || bean instanceof ZxvContext;
        }
    }

    @Test
    void shutsDownChildrenBeforeParents() {
        DestroyRecorder recorder = new DestroyRecorder();
        ZxvContext[] zxv = new ZxvContext[1];
        Capability[] teller = new Capability[1];
        runner.withBean(DestroyRecorder.class, () -> recorder).withPropertyValues(TELLER_AUDITOR).run(ctx -> {
            zxv[0] = ctx.getBean(ZxvContext.class);
            teller[0] = ctx.getBean("zxv.capability.teller", Capability.class);
        });
        // ABI.md R6: attenuated caps, then their parent (the root), then the context.
        assertThat(recorder.order).hasSize(4);
        assertThat(recorder.order.subList(0, 2))
                .containsExactlyInAnyOrder("zxv.capability.teller", "zxv.capability.auditor");
        assertThat(recorder.order.subList(2, 4)).containsExactly("zxvRootCapability", "zxvContext");
        assertThatThrownBy(() -> zxv[0].isPoisoned()).isInstanceOf(IllegalStateException.class);
        assertThatThrownBy(teller[0]::permissions).isInstanceOf(IllegalStateException.class);
    }

    @Test
    void aPoisonedContextStillShutsDownCleanly() {
        ZxvContext[] zxv = new ZxvContext[1];
        runner.withPropertyValues(TELLER_AUDITOR).run(ctx -> {
            zxv[0] = ctx.getBean(ZxvContext.class);
            zxv[0].halt("test");
            Capability teller = ctx.getBean("zxv.capability.teller", Capability.class);
            assertThatThrownBy(() -> Ledger.of(teller).accounts()).isInstanceOf(ZxvFatalError.class);
        });
        assertThatThrownBy(() -> zxv[0].enabledCurrencies()).isInstanceOf(IllegalStateException.class);
    }
}
