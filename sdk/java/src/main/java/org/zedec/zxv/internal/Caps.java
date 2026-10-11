// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.internal;

import org.zedec.zxv.Capability;
import org.zedec.zxv.ZxvContext;

/** Capability helpers for sub-packages. Internal. */
public final class Caps {
    private Caps() {}

    /** The capability's resource. */
    public static NativeResource res(Capability cap) {
        return Access.core().resource(java.util.Objects.requireNonNull(cap, "capability"));
    }

    /** The capability's context. */
    public static ZxvContext ctx(Capability cap) {
        return Access.core().context(java.util.Objects.requireNonNull(cap, "capability"));
    }

    /** The context's resource. */
    public static NativeResource res(ZxvContext ctx) {
        return Access.core().resource(ctx);
    }
}
