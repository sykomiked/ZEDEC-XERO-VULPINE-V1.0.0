// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.crypto;

import java.lang.foreign.MemorySegment;
import java.lang.invoke.MethodHandle;
import org.zedec.zxv.Capability;
import org.zedec.zxv.ZxvContext;
import org.zedec.zxv.internal.Caps;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Lease;

/** Calls a capability-gated crypto function. */
final class Gate {
    final Capability cap;
    final ZxvContext ctx;

    Gate(Capability cap) {
        this.cap = cap;
        this.ctx = Caps.ctx(cap);
    }

    /** Calls {@code mh(cap, rest...)} and returns the raw status (last error read by {@link #check}). */
    int raw(MethodHandle mh, Object... rest) {
        try (Lease l = Lease.of(Caps.res(cap))) {
            Object[] args = new Object[rest.length + 1];
            args[0] = l.h(0);
            System.arraycopy(rest, 0, args, 1, rest.length);
            return Ffi.call(mh, args);
        }
    }

    /** Calls and maps any error to an exception. */
    void call(MethodHandle mh, Object... rest) {
        Ffi.check(raw(mh, rest), ctx);
    }

    /** Verify-style call: true on OK, false on ZXV_E_VERIFY, exception otherwise. */
    boolean verify(MethodHandle mh, Object... rest) {
        int st = raw(mh, rest);
        if (st == -21) return false;
        Ffi.check(st, ctx);
        return true;
    }

    static MemorySegment orNull(MemorySegment s) {
        return s == null ? MemorySegment.NULL : s;
    }
}
