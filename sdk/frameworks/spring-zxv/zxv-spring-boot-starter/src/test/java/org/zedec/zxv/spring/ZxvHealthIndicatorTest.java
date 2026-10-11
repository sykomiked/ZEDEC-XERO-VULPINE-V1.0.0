// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.spring;

import static org.assertj.core.api.Assertions.assertThat;

import java.util.Map;
import org.junit.jupiter.api.Test;
import org.springframework.boot.actuate.health.Health;
import org.springframework.boot.actuate.health.Status;
import org.springframework.boot.autoconfigure.AutoConfigurations;
import org.springframework.boot.autoconfigure.availability.ApplicationAvailabilityAutoConfiguration;
import org.springframework.boot.availability.ApplicationAvailability;
import org.springframework.boot.availability.AvailabilityChangeEvent;
import org.springframework.boot.availability.LivenessState;
import org.springframework.boot.availability.ReadinessState;
import org.springframework.boot.test.context.runner.ApplicationContextRunner;
import org.zedec.zxv.Capability;
import org.zedec.zxv.Truth;
import org.zedec.zxv.ZxvContext;
import org.zedec.zxv.spring.actuate.ZxvHealthContributorAutoConfiguration;
import org.zedec.zxv.spring.actuate.ZxvHealthIndicator;

/** The health indicator is fail-closed: DOWN as soon as the context is poisoned. */
class ZxvHealthIndicatorTest {
    private final ApplicationContextRunner runner = new ApplicationContextRunner()
            .withConfiguration(AutoConfigurations.of(ZxvAutoConfiguration.class,
                    ZxvHealthContributorAutoConfiguration.class, ApplicationAvailabilityAutoConfiguration.class))
            .withPropertyValues("zxv.capabilities.teller.permissions=LEDGER_READ,LEDGER_POST");

    @Test
    void upWithCapabilityDetails() {
        runner.run(ctx -> {
            Health h = ctx.getBean(ZxvHealthIndicator.class).health();
            assertThat(h.getStatus()).isEqualTo(Status.UP);
            assertThat(h.getDetails()).containsEntry("state", "live").containsKey("abi");
            @SuppressWarnings("unchecked")
            Map<String, Map<String, Object>> caps = (Map<String, Map<String, Object>>) h.getDetails().get("capabilities");
            // Only the named capability, never the root.
            assertThat(caps).containsOnlyKeys("teller");
            assertThat(caps.get("teller")).containsEntry("attestation", "TRUE").containsEntry("revoked", false);
        });
    }

    @Test
    void reportsRevocationAndAttestationWithoutGoingDown() {
        runner.run(ctx -> {
            Capability teller = ctx.getBean("zxv.capability.teller", Capability.class);
            teller.setAttestation(Truth.BOTH);
            Health h = ctx.getBean(ZxvHealthIndicator.class).health();
            assertThat(h.getStatus()).isEqualTo(Status.UP);
            assertThat(h.getDetails().get("capabilities").toString()).contains("attestation=BOTH");
        });
    }

    @Test
    void downWhenPoisonedAndLivenessBroken() {
        runner.run(ctx -> {
            ApplicationAvailability availability = ctx.getBean(ApplicationAvailability.class);
            // What SpringApplication publishes once started.
            AvailabilityChangeEvent.publish(ctx, LivenessState.CORRECT);
            AvailabilityChangeEvent.publish(ctx, ReadinessState.ACCEPTING_TRAFFIC);
            assertThat(availability.getLivenessState()).isEqualTo(LivenessState.CORRECT);

            ctx.getBean(ZxvContext.class).halt("operator test");
            Health h = ctx.getBean(ZxvHealthIndicator.class).health();
            assertThat(h.getStatus()).isEqualTo(Status.DOWN);
            assertThat(h.getDetails()).containsEntry("state", "poisoned").doesNotContainKey("abi");
            // The halt reason is not exposed.
            assertThat(h.toString()).doesNotContain("operator test");

            assertThat(ctx.getBean(ZxvFatalHandler.class).isTripped()).isTrue();
            assertThat(availability.getLivenessState()).isEqualTo(LivenessState.BROKEN);
            assertThat(availability.getReadinessState()).isEqualTo(ReadinessState.REFUSING_TRAFFIC);
        });
    }

    @Test
    void downWhenClosed() {
        runner.run(ctx -> {
            ctx.getBean(ZxvContext.class).close();
            Health h = ctx.getBean(ZxvHealthIndicator.class).health();
            assertThat(h.getStatus()).isEqualTo(Status.DOWN);
            assertThat(h.getDetails()).containsEntry("state", "closed");
        });
    }

    @Test
    void canBeDisabled() {
        runner.withPropertyValues("management.health.zxv.enabled=false")
                .run(ctx -> assertThat(ctx).doesNotHaveBean(ZxvHealthIndicator.class));
    }
}
