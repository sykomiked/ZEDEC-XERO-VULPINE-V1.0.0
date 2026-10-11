// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.spring.actuate;

import java.util.LinkedHashMap;
import java.util.Map;
import java.util.TreeMap;
import org.springframework.boot.actuate.health.Health;
import org.springframework.boot.actuate.health.HealthIndicator;
import org.zedec.zxv.Capability;
import org.zedec.zxv.Zxv;
import org.zedec.zxv.ZxvContext;
import org.zedec.zxv.ZxvFatalError;
import org.zedec.zxv.spring.ZxvAutoConfiguration;
import org.zedec.zxv.spring.ZxvFatalHandler;

/**
 * Health of the managed ZXV context, fail-closed: DOWN when the context is
 * poisoned (halted), closed, or cannot be asked; UP only when libzxv answers
 * that it is live. A poisoned context also trips the
 * {@link ZxvFatalHandler}, so liveness reports BROKEN even if no request
 * touched it.
 *
 * <p>Details (shown only if {@code management.endpoint.health.show-details}
 * allows): the ABI version, live features, and for each named capability its
 * permissions, attestation and revocation. No key material, no ledger data,
 * no halt reason.
 */
public final class ZxvHealthIndicator implements HealthIndicator {
    private final ZxvContext context;
    private final Map<String, Capability> capabilities;
    private final ZxvFatalHandler fatalHandler;

    /**
     * Creates the indicator.
     *
     * @param context      the managed context
     * @param capabilities the named capability beans, by bean name
     * @param fatalHandler tripped when the context is found poisoned (may be null)
     */
    public ZxvHealthIndicator(ZxvContext context, Map<String, Capability> capabilities, ZxvFatalHandler fatalHandler) {
        this.context = context;
        this.capabilities = new TreeMap<>(capabilities);
        this.fatalHandler = fatalHandler;
    }

    @Override
    public Health health() {
        boolean poisoned;
        try {
            poisoned = context.isPoisoned();
        } catch (ZxvFatalError e) {
            poisoned = true;
        } catch (IllegalStateException e) {
            return Health.down().withDetail("state", "closed").build();
        } catch (RuntimeException e) {
            return Health.down().withDetail("state", "unknown").withDetail("error", e.getClass().getName()).build();
        }
        if (poisoned) {
            if (fatalHandler != null) fatalHandler.trip("ZXV_E_POISONED, found by the health check");
            return Health.down().withDetail("state", "poisoned").build();
        }
        Health.Builder up = Health.up()
                .withDetail("state", "live")
                .withDetail("abi", Zxv.version().toString())
                .withDetail("features", Zxv.features().toString());
        Map<String, Object> caps = new LinkedHashMap<>();
        for (Map.Entry<String, Capability> e : capabilities.entrySet()) {
            caps.put(shortName(e.getKey()), describe(e.getValue()));
        }
        if (!caps.isEmpty()) up.withDetail("capabilities", caps);
        return up.build();
    }

    private static Object describe(Capability cap) {
        try {
            Capability.Info info = cap.info();
            Map<String, Object> m = new LinkedHashMap<>();
            m.put("permissions", info.permissions().toString());
            m.put("attestation", info.attestation().name());
            m.put("revoked", info.revoked());
            return m;
        } catch (IllegalStateException e) {
            return Map.of("state", "closed");
        }
    }

    private static String shortName(String bean) {
        return bean.startsWith(ZxvAutoConfiguration.CAPABILITY_BEAN_PREFIX)
                ? bean.substring(ZxvAutoConfiguration.CAPABILITY_BEAN_PREFIX.length()) : bean;
    }
}
