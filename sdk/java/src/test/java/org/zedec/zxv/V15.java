// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;

import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.function.Supplier;

/** Helpers for the ABI 1.5 vector tests: 64-bit numbers in the vectors are decimal strings. */
final class V15 {
    private V15() {}

    private static Map<String, Integer> codes;

    static Map<String, Object> sec(String name) {
        return Vectors.map(Vectors.root().get(name));
    }

    static Map<String, Object> m(Object o) {
        return Vectors.map(o);
    }

    static List<Object> l(Object o) {
        return Vectors.list(o);
    }

    /** A uint64 given as a decimal string (or a small JSON number), as Java long bits. */
    static long u64(Object o) {
        if (o instanceof String s) return Long.parseUnsignedLong(s);
        return Vectors.num(o);
    }

    static int i(Object o) {
        return (int) Vectors.num(o);
    }

    static byte[] hex(Object o) {
        return Vectors.hex(o);
    }

    static String hex(byte[] b) {
        return Vectors.toHex(b);
    }

    static byte[] utf8(Object o) {
        return ((String) o).getBytes(StandardCharsets.UTF_8);
    }

    static byte[] ramp(int n) {
        byte[] b = new byte[n];
        for (int i = 0; i < n; i++) b[i] = (byte) i;
        return b;
    }

    static byte[] fill(int value, int n) {
        byte[] b = new byte[n];
        java.util.Arrays.fill(b, (byte) value);
        return b;
    }

    /** The code of a status name, from the vectors' status tables. */
    static synchronized int code(String name) {
        if (codes == null) {
            codes = new HashMap<>();
            for (String t : new String[] {"status", "status_abi15", "status_abi16"}) {
                for (Object e : l(Vectors.root().get(t))) {
                    Map<String, Object> s = m(e);
                    codes.put((String) s.get("name"), i(s.get("code")));
                }
            }
        }
        Integer c = codes.get(name);
        if (c == null) throw new IllegalArgumentException(name);
        return c;
    }

    /**
     * Runs {@code f} expecting {@code status}: ZXV_OK returns its value; any
     * other status must throw a {@link ZxvException} with that code (and then null is returned).
     */
    static <T> T expect(Object status, Supplier<T> f) {
        String s = (String) status;
        if (s.equals("ZXV_OK")) return f.get();
        ZxvException e = assertThrows(ZxvException.class, f::get, "expected " + s);
        assertEquals(code(s), e.status(), "expected " + s + ", got " + e.statusName() + ": " + e.getMessage());
        return null;
    }

    /** {@link #expect} for a call with no result. */
    static void expectRun(Object status, Runnable r) {
        expect(status, () -> {
            r.run();
            return Boolean.TRUE;
        });
    }

    /** The status a ZxvException carries. */
    static int status(Throwable t) {
        return ((ZxvException) t).status();
    }

    static Capability root(ZxvContext ctx) {
        return ctx.rootCapability(Permission.all());
    }

    /** {@code cap} without the given permissions. */
    static Capability without(Capability cap, Permission... drop) {
        java.util.Set<Permission> keep = java.util.EnumSet.allOf(Permission.class);
        for (Permission p : drop) keep.remove(p);
        return cap.attenuate(keep);
    }
}
