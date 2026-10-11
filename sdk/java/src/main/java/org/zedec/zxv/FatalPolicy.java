// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv;

/**
 * What the SDK does when a context halts (a FATAL status: a broken
 * invariant, or {@link ZxvContext#halt(String)}).
 *
 * <p>In both cases the context is already poisoned in C: every later call on
 * it fails with {@code ZXV_E_POISONED}, so a retry can never succeed.
 */
public enum FatalPolicy {
    /**
     * Throw {@link ZxvFatalError} (an {@link Error}, outside the
     * {@link ZxvException} hierarchy, so ordinary {@code catch (Exception e)}
     * blocks do not swallow it). The default.
     */
    THROW,
    /**
     * Print the reason to standard error and terminate the JVM at once with
     * {@code Runtime.getRuntime().halt(70)}: no shutdown hooks, no finally
     * blocks. The only form no host code can catch.
     */
    ABORT
}
