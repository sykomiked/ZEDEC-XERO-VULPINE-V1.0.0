// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.spring;

import java.util.ArrayList;
import java.util.EnumSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import org.springframework.boot.context.properties.ConfigurationProperties;
import org.zedec.zxv.FatalPolicy;
import org.zedec.zxv.Permission;

/**
 * Configuration of the managed ZXV context, under the {@code zxv} prefix.
 *
 * <pre>{@code
 * zxv:
 *   currencies: [NGN, KES]
 *   private-currencies:
 *     VFV: 2
 *   capabilities:
 *     teller:
 *       permissions: [LEDGER_READ, LEDGER_POST]
 *     signer:
 *       permissions: [CRYPTO_SIGN]
 * }</pre>
 */
@ConfigurationProperties(prefix = ZxvProperties.PREFIX)
public class ZxvProperties {
    /** The property prefix. */
    public static final String PREFIX = "zxv";

    /** Whether to create the ZXV context and its beans. */
    private boolean enabled = true;

    /**
     * Path of the libzxv shared library. When unset the SDK searches the
     * zxv.library system property, ZXV_LIBRARY_PATH, the copy bundled in the
     * SDK jar, then the system loader path.
     */
    private String library;

    /**
     * What the SDK does when the context halts: THROW raises ZxvFatalError
     * (an Error), ABORT halts the JVM at once.
     */
    private FatalPolicy fatalPolicy = FatalPolicy.THROW;

    /**
     * Permissions of the root capability. Every configured capability is
     * attenuated from it, so it must hold all of theirs. The root capability
     * itself is not an autowire candidate.
     */
    private Set<Permission> rootPermissions = EnumSet.allOf(Permission.class);

    /** ISO 4217 alpha codes to enable at start-up (needs LEDGER_POST on the root capability). */
    private List<String> currencies = new ArrayList<>();

    /** Platform-private units to register at start-up: code to number of minor units. */
    private Map<String, Integer> privateCurrencies = new LinkedHashMap<>();

    /**
     * Named capabilities to register as beans, each attenuated from the root
     * capability. Inject one with {@code @Qualifier("<name>") Capability}.
     */
    private Map<String, CapabilitySpec> capabilities = new LinkedHashMap<>();

    /** JSON settings. */
    private final Jackson jackson = new Jackson();

    /** JSON settings. */
    public static class Jackson {
        /**
         * Refuse JSON floats for integer fields (turns off Jackson's
         * ACCEPT_FLOAT_AS_INT, which would truncate 150.7 to 150).
         */
        private boolean strictIntegers = true;

        /** Whether JSON floats are refused for integer fields. */
        public boolean isStrictIntegers() {
            return strictIntegers;
        }

        /** Sets whether JSON floats are refused for integer fields. */
        public void setStrictIntegers(boolean strictIntegers) {
            this.strictIntegers = strictIntegers;
        }
    }

    /** JSON settings. */
    public Jackson getJackson() {
        return jackson;
    }

    /** One named capability. */
    public static class CapabilitySpec {
        /** The permissions it holds (never more than the root capability's). */
        private Set<Permission> permissions = EnumSet.noneOf(Permission.class);

        /** The permissions it holds. */
        public Set<Permission> getPermissions() {
            return permissions;
        }

        /** Sets the permissions it holds. */
        public void setPermissions(Set<Permission> permissions) {
            this.permissions = permissions;
        }
    }

    /** Whether to create the ZXV context and its beans. */
    public boolean isEnabled() {
        return enabled;
    }

    /** Sets whether to create the ZXV context and its beans. */
    public void setEnabled(boolean enabled) {
        this.enabled = enabled;
    }

    /** Path of the libzxv shared library, or null to use the SDK's search order. */
    public String getLibrary() {
        return library;
    }

    /** Sets the path of the libzxv shared library. */
    public void setLibrary(String library) {
        this.library = library;
    }

    /** The fatal policy of the context. */
    public FatalPolicy getFatalPolicy() {
        return fatalPolicy;
    }

    /** Sets the fatal policy of the context. */
    public void setFatalPolicy(FatalPolicy fatalPolicy) {
        this.fatalPolicy = fatalPolicy;
    }

    /** Permissions of the root capability. */
    public Set<Permission> getRootPermissions() {
        return rootPermissions;
    }

    /** Sets the permissions of the root capability. */
    public void setRootPermissions(Set<Permission> rootPermissions) {
        this.rootPermissions = rootPermissions;
    }

    /** ISO 4217 codes to enable at start-up. */
    public List<String> getCurrencies() {
        return currencies;
    }

    /** Sets the ISO 4217 codes to enable at start-up. */
    public void setCurrencies(List<String> currencies) {
        this.currencies = currencies;
    }

    /** Private units to register at start-up. */
    public Map<String, Integer> getPrivateCurrencies() {
        return privateCurrencies;
    }

    /** Sets the private units to register at start-up. */
    public void setPrivateCurrencies(Map<String, Integer> privateCurrencies) {
        this.privateCurrencies = privateCurrencies;
    }

    /** Named capabilities. */
    public Map<String, CapabilitySpec> getCapabilities() {
        return capabilities;
    }

    /** Sets the named capabilities. */
    public void setCapabilities(Map<String, CapabilitySpec> capabilities) {
        this.capabilities = capabilities;
    }
}
