// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.internal;

import java.lang.foreign.MemorySegment;

/**
 * Holds the read locks of one or more resources for the length of a native
 * call: {@code try (Lease l = Lease.of(cap, reg)) { call(l.h(0), l.h(1)); }}.
 * Locks are taken in argument order and released in reverse. Internal.
 */
public final class Lease implements AutoCloseable {
    private final NativeResource[] res;
    private final MemorySegment[] handles;
    private int held;

    private Lease(NativeResource[] res) {
        this.res = res;
        this.handles = new MemorySegment[res.length];
        try {
            for (; held < res.length; held++) handles[held] = res[held].acquire();
        } catch (RuntimeException | Error e) {
            close();
            throw e;
        }
    }

    /** Acquires every resource. */
    public static Lease of(NativeResource... res) {
        return new Lease(res);
    }

    /** The handle of the i-th resource. */
    public MemorySegment h(int i) {
        return handles[i];
    }

    @Override
    public void close() {
        while (held > 0) res[--held].release();
    }
}
