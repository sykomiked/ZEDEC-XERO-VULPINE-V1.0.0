// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.internal;

import java.lang.foreign.MemorySegment;
import org.zedec.zxv.Capability;
import org.zedec.zxv.ZxvContext;

/**
 * Lets the SDK's sub-packages reach the native handles of core types without
 * making those accessors public API. The core package installs the bridge
 * when {@link ZxvContext} is initialized. Internal.
 */
public final class Access {
    private Access() {}

    /** What the core package exposes to the rest of the SDK. */
    public interface Core {
        /** The context's resource (for locking and child registration). */
        NativeResource resource(ZxvContext ctx);

        /** The capability's resource. */
        NativeResource resource(Capability cap);

        /** The context a capability was created from. */
        ZxvContext context(Capability cap);

        /** Marks the context dead after a FATAL status. */
        void markDead(ZxvContext ctx, String why);

        /** Why the context died, or null while it is alive. */
        String deadReason(ZxvContext ctx);
    }

    private static volatile Core core;

    /** Installs the bridge (called once by the core package). */
    public static void install(Core c) {
        core = c;
    }

    /** The bridge. */
    public static Core core() {
        Core c = core;
        if (c == null) {
            try {
                Class.forName(ZxvContext.class.getName(), true, ZxvContext.class.getClassLoader());
            } catch (ClassNotFoundException e) {
                throw new IllegalStateException(e);
            }
            c = core;
        }
        return c;
    }

    /** Convenience: the raw handle of a context (caller must hold its lock). */
    public static MemorySegment handle(ZxvContext ctx) {
        return core().resource(ctx).rawHandle();
    }
}
