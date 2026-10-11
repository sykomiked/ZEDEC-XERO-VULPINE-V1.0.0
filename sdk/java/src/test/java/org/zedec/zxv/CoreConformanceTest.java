// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.zedec.zxv.Vectors.list;
import static org.zedec.zxv.Vectors.map;
import static org.zedec.zxv.Vectors.num;

import java.lang.foreign.MemorySegment;
import java.math.BigDecimal;
import java.util.List;
import java.util.Map;
import org.junit.jupiter.api.Test;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Native;

/** Library, status, rational and four-valued logic conformance against vectors.json. */
class CoreConformanceTest {
    @Test
    void versionAndFeatures() {
        Zxv.Version v = Zxv.version();
        List<Object> abi = list(Vectors.root().get("abi"));
        assertEquals(1, v.major());
        assertTrue(v.minor() >= num(abi.get(1)));
        assertTrue(Zxv.hasFeature(Feature.RATIONAL));
        assertFalse(Zxv.hasFeature(Feature.LORA), "LoRA is honestly reported as not built");
        assertTrue(Zxv.libraryLocation().contains("libzxv"));
    }

    @Test
    void statusNamesAndClasses() {
        for (Object o : list(Vectors.root().get("status"))) {
            Map<String, Object> s = map(o);
            int code = (int) num(s.get("code"));
            assertEquals(s.get("name"), Zxv.statusName(code));
            assertEquals(StatusClass.fromCode((int) num(s.get("class"))), Zxv.statusClass(code), "class of " + code);
        }
        assertEquals(StatusClass.SYSTEM, Zxv.statusClass(-12345), "unknown negative codes are SYSTEM");
        assertEquals("ZXV_E_UNKNOWN", Zxv.statusName(-12345));
    }

    private static Rational rat(Object pair) {
        List<Object> p = list(pair);
        return Rational.of(num(p.get(0)), num(p.get(1)));
    }

    @Test
    void rationalArithmetic() {
        for (Object o : list(Vectors.root().get("rational_ops"))) {
            Map<String, Object> v = map(o);
            Rational a = rat(v.get("a")), b = rat(v.get("b"));
            assertEquals(rat(v.get("add")), a.add(b));
            assertEquals(rat(v.get("sub")), a.subtract(b));
            assertEquals(rat(v.get("mul")), a.multiply(b));
            assertEquals(rat(v.get("div")), a.divide(b));
            assertEquals(num(v.get("cmp")), a.compareTo(b));
        }
        assertEquals(Rational.of(3, 10), Rational.parse("0.1").add(Rational.parse("0.2")), "0.1 + 0.2 is exactly 3/10");
        assertEquals(Rational.of(-3, 2), Rational.of(6, -4));
        ZxvUsageException div0 = assertThrows(ZxvUsageException.class, () -> Rational.ONE.divide(Rational.ZERO));
        assertEquals(-1, div0.status());
        ZxvUsageException ovf = assertThrows(ZxvUsageException.class,
                () -> Rational.of(Long.MAX_VALUE).multiply(Rational.of(2)));
        assertEquals("ZXV_E_RANGE", ovf.statusName());
        assertThrows(ZxvUsageException.class, () -> Rational.of(Long.MIN_VALUE, 1));
    }

    @Test
    void rationalParseAndFormat() {
        for (Object o : list(Vectors.root().get("rational_parse"))) {
            Map<String, Object> v = map(o);
            String text = (String) v.get("text");
            int status = (int) num(v.get("status"));
            if (status == 0) {
                Rational r = Rational.parse(text);
                assertEquals(rat(v.get("value")), r, text);
                assertEquals(v.get("format"), r.toString());
                assertEquals(r, Rational.parse(r.toString()), "format round-trips");
            } else {
                ZxvUsageException e = assertThrows(ZxvUsageException.class, () -> Rational.parse(text), text);
                assertEquals(status, e.status(), text);
            }
        }
        assertEquals("1/3", Rational.of(1, 3).toString());
        assertEquals("-51/4", Rational.parse("-12.75").toFractionString());
    }

    @Test
    void ieee754Boundary() {
        for (Object o : list(Vectors.root().get("ieee754"))) {
            Map<String, Object> v = map(o);
            long bits = Long.parseUnsignedLong((String) v.get("bits"), 16);
            int status = (int) num(v.get("status"));
            if (status == 0) {
                assertEquals(rat(v.get("value")), Rational.fromIeee754Bits(bits));
                assertEquals(rat(v.get("value")), Rational.fromDoubleExact(Double.longBitsToDouble(bits)));
            } else if (status == -24) {
                FloatRejectedException e = assertThrows(FloatRejectedException.class, () -> Rational.fromIeee754Bits(bits));
                assertEquals(StatusClass.USAGE, e.statusClass());
            } else {
                ZxvUsageException e = assertThrows(ZxvUsageException.class, () -> Rational.fromIeee754Bits(bits));
                assertEquals(status, e.status());
            }
        }
        assertEquals(Rational.of(3602879701896397L, 36028797018963968L), Rational.fromDoubleExact(0.1));
        assertThrows(FloatRejectedException.class, () -> Rational.fromDoubleExact(Double.NaN));
    }

    @Test
    void q16AndBigDecimal() {
        for (Object o : list(Vectors.root().get("q16"))) {
            Map<String, Object> v = map(o);
            Rational r = rat(v.get("value"));
            if (num(v.get("status")) == 0) {
                assertEquals(num(v.get("q16")), r.toQ16());
            } else {
                assertEquals(num(v.get("status")), assertThrows(ZxvUsageException.class, r::toQ16).status());
            }
        }
        assertEquals(Rational.of(51, 4), Rational.from(new BigDecimal("12.750")));
        assertEquals(Rational.of(1200), Rational.from(new BigDecimal("1.2E+3")));
        assertEquals(new BigDecimal("-12.75"), Rational.parse("-12.75").toBigDecimal());
        assertThrows(ArithmeticException.class, () -> Rational.of(1, 3).toBigDecimal());
        assertThrows(ZxvUsageException.class, () -> Rational.from(new BigDecimal("1E-30")));
    }

    @Test
    void fourValuedLogicMatchesTablesAndC() {
        Map<String, Object> lp = map(Vectors.root().get("lpres"));
        List<Object> names = list(lp.get("names"));
        int[] not = Vectors.ints(lp.get("not"));
        for (Truth a : Truth.values()) {
            assertEquals(names.get(a.code()), a.name());
            assertEquals(Truth.fromCode(not[a.code()]), a.not());
            int[] out = new int[1];
            Ffi.check(Ffi.call(Native.lp_not, a.code(), MemorySegment.ofArray(out)), null);
            assertEquals(out[0], a.not().code(), "Java not matches C");
            int[] andRow = Vectors.ints(list(lp.get("and")).get(a.code()));
            int[] orRow = Vectors.ints(list(lp.get("or")).get(a.code()));
            for (Truth b : Truth.values()) {
                assertEquals(Truth.fromCode(andRow[b.code()]), a.and(b), a + " and " + b);
                assertEquals(Truth.fromCode(orRow[b.code()]), a.or(b), a + " or " + b);
                Ffi.check(Ffi.call(Native.lp_and, a.code(), b.code(), MemorySegment.ofArray(out)), null);
                assertEquals(out[0], a.and(b).code());
                Ffi.check(Ffi.call(Native.lp_or, a.code(), b.code(), MemorySegment.ofArray(out)), null);
                assertEquals(out[0], a.or(b).code());
            }
        }
        for (int p = 0; p < 3; p++) {
            for (int n = 0; n < 3; n++) {
                int[] out = new int[1];
                Ffi.check(Ffi.call(Native.lp_from_evidence, p, n, MemorySegment.ofArray(out)), null);
                assertEquals(Truth.fromCode(out[0]), Truth.fromEvidence(p, n));
            }
        }
        assertEquals(0, Truth.NEITHER.code());
        assertEquals(3, Truth.BOTH.code());
        assertTrue(Truth.BOTH.toBoolean(true, false));
        assertFalse(Truth.BOTH.toBoolean(false, true));
        assertFalse(Truth.BOTH.isCertainlyTrue());
        assertThrows(IllegalStateException.class, Truth.NEITHER::requireTrue);
        assertThrows(ZxvUsageException.class, () -> Truth.fromCode(7));
    }
}
