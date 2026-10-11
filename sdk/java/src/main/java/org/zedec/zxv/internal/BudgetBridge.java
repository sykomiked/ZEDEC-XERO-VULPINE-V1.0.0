// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.internal;

/**
 * Lets the engine bind a session to a budget without public accessors on
 * {@code Budget}. Installed by {@code org.zedec.zxv.budget.Budget}. Internal.
 */
public final class BudgetBridge {
    private BudgetBridge() {}

    /** What the budget package exposes. */
    public interface Bridge {
        /** The registry (zxv_rmag) resource the budget lives in. */
        NativeResource registry(Object budget);

        /** The budget's index in its registry. */
        int index(Object budget);
    }

    private static volatile Bridge bridge;

    /** Installs the bridge. */
    public static void install(Bridge b) {
        bridge = b;
    }

    /** The bridge (the budget class is initialized whenever a Budget exists). */
    public static Bridge get() {
        return bridge;
    }
}
