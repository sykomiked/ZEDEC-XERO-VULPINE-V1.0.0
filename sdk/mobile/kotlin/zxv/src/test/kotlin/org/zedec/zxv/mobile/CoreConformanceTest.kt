// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.mobile

import java.math.BigDecimal
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse
import kotlin.test.assertTrue
import org.zedec.zxv.mobile.internal.Jni

class CoreConformanceTest {
    @Test
    fun versionAndFeatures() {
        val v = Zxv.version()
        assertEquals(1, v.major)
        assertTrue(v.minor >= 3, "ABI $v")
        val abi = Vectors["abi"].arr()
        assertEquals(abi[0].i(), v.major)
        assertTrue(Zxv.hasFeature(Feature.HEX))
        assertTrue(Feature.RATIONAL in Zxv.features())
        assertFalse(Zxv.hasFeature(Feature.LORA))
    }

    @Test
    fun statusNamesAndClasses() {
        for (e in Vectors["status"].arr()) {
            val code = e.obj("code").i()
            assertEquals(e.obj("name").s(), Zxv.statusName(code))
            assertEquals(StatusClass.fromCode(e.obj("class").i()), Zxv.statusClass(code))
        }
        assertEquals(StatusClass.SYSTEM, Zxv.statusClass(-12345))
    }

    @Test
    fun rationalOps() {
        for (e in Vectors["rational_ops"].arr()) {
            val (an, ad) = e.obj("a").pair()
            val (bn, bd) = e.obj("b").pair()
            val a = Rational.of(an, ad)
            val b = Rational.of(bn, bd)
            fun r(k: String) = e.obj(k).pair().let { Rational.of(it.first, it.second) }
            assertEquals(r("add"), a + b)
            assertEquals(r("sub"), a - b)
            assertEquals(r("mul"), a * b)
            assertEquals(r("div"), a / b)
            assertEquals(e.obj("cmp").i(), a.compareTo(b))
        }
    }

    @Test
    fun rationalParseAndFormat() {
        for (e in Vectors["rational_parse"].arr()) {
            val text = e.obj("text").s()
            val status = e.obj("status").i()
            if (status == 0) {
                val r = Rational.parse(text)
                val (n, d) = e.obj("value").pair()
                assertEquals(n, r.numerator)
                assertEquals(d, r.denominator)
                assertEquals(e.obj("format").s(), r.toString())
            } else {
                val ex = assertFailsWith<ZxvUsageException>(text) { Rational.parse(text) }
                assertEquals(status, ex.status, text)
            }
        }
    }

    @Test
    fun ieee754IsTheOnlyFloatDoor() {
        for (e in Vectors["ieee754"].arr()) {
            val bits = java.lang.Long.parseUnsignedLong(e.obj("bits").s(), 16)
            val status = e.obj("status").i()
            if (status == 0) {
                val (n, d) = e.obj("value").pair()
                assertEquals(Rational.of(n, d), Rational.fromIeee754Bits(bits))
                assertEquals(Rational.of(n, d), Rational.fromDoubleExact(java.lang.Double.longBitsToDouble(bits)))
            } else if (status == -24) {
                assertFailsWith<FloatRejectedException> { Rational.fromIeee754Bits(bits) }
            } else {
                assertEquals(status, assertFailsWith<ZxvUsageException> { Rational.fromIeee754Bits(bits) }.status)
            }
        }
        assertEquals("3602879701896397/36028797018963968", Rational.fromDoubleExact(0.1).toFractionString())
        assertFailsWith<FloatRejectedException> { Rational.fromDoubleExact(Double.NaN) }
        // No other public member takes a Float or Double.
        val floaty = Rational::class.java.methods.filter { m ->
            m.parameterTypes.any { it == java.lang.Double.TYPE || it == java.lang.Float.TYPE } ||
                m.returnType == java.lang.Double.TYPE || m.returnType == java.lang.Float.TYPE
        }.map { it.name }.toSet()
        assertEquals(setOf("fromDoubleExact"), floaty)
    }

    @Test
    fun q16() {
        for (e in Vectors["q16"].arr()) {
            val (n, d) = e.obj("value").pair()
            val status = e.obj("status").i()
            if (status == 0) assertEquals(e.obj("q16").i(), Rational.of(n, d).toQ16())
            else assertEquals(status, assertFailsWith<ZxvUsageException> { Rational.of(n, d).toQ16() }.status)
        }
    }

    @Test
    fun rationalExtras() {
        assertEquals(Rational.of(3, 10), Rational.parse("0.1") + Rational.parse("0.2"))
        assertEquals(Rational.of(51, 4), Rational.from(BigDecimal("12.75")))
        assertEquals(BigDecimal("12.75"), Rational.of(51, 4).toBigDecimal())
        assertEquals(Rational.of(1, 2), Rational.of(2, 4))
        assertEquals(-1, Rational.of(-1, 3).signum)
        assertEquals(Rational.of(1, 3), Rational.of(-1, 3).abs())
        assertEquals(-1, assertFailsWith<ZxvUsageException> { Rational.of(1, 0) }.status)
        assertEquals(-8, assertFailsWith<ZxvUsageException> { Rational.of(Long.MAX_VALUE) + Rational.ONE }.status)
        assertFailsWith<ZxvUsageException> { Rational.parse("1\u00002") }
    }

    @Test
    fun truthTablesMatchLibzxv() {
        val lp = Vectors["lpres"]
        val names = lp.obj("names").arr().map { it.s() }
        assertEquals(Truth.entries.map { it.name }, names)
        val out = IntArray(1)
        for (a in Truth.entries) {
            assertEquals(lp.obj("not").arr()[a.code].i(), (!a).code)
            assertEquals(0, Jni.lpNot(a.code, out)); assertEquals(out[0], (!a).code)
            for (b in Truth.entries) {
                assertEquals(lp.obj("and").arr()[a.code].arr()[b.code].i(), (a and b).code)
                assertEquals(lp.obj("or").arr()[a.code].arr()[b.code].i(), (a or b).code)
                assertEquals(0, Jni.lpAnd(a.code, b.code, out)); assertEquals(out[0], (a and b).code)
                assertEquals(0, Jni.lpOr(a.code, b.code, out)); assertEquals(out[0], (a or b).code)
            }
        }
    }

    @Test
    fun truthHasNoImplicitBoolean() {
        assertTrue(Truth.TRUE.isCertainlyTrue)
        assertFalse(Truth.BOTH.isCertainlyTrue)
        assertTrue(Truth.BOTH.toBoolean(onBoth = true, onNeither = false))
        assertFalse(Truth.NEITHER.toBoolean(onBoth = true, onNeither = false))
        assertFailsWith<IllegalStateException> { Truth.BOTH.requireTrue() }
        assertEquals(Truth.BOTH, Truth.fromEvidence(1, 1))
        assertFailsWith<ZxvUsageException> { Truth.fromCode(4) }
        // No member converts to Boolean except the policy-taking toBoolean.
        val boolOut = Truth::class.java.declaredMethods.filter {
            it.returnType == java.lang.Boolean.TYPE && java.lang.reflect.Modifier.isPublic(it.modifiers) &&
                !it.name.startsWith("is")
        }.map { it.name }.toSet()
        assertEquals(setOf("toBoolean"), boolOut)
    }

    @Test
    fun hashes() {
        for (e in Vectors["hash"].arr()) {
            val data = e.obj("utf8").s().toByteArray()
            assertEquals(e.obj("sha3_256").s(), Hashes.sha3_256(data).toHex())
            assertEquals(e.obj("keccak256").s(), Hashes.keccak256(data).toHex())
            assertEquals(e.obj("shake256_64").s(), Hashes.shake256(data, 64).toHex())
        }
    }
}
