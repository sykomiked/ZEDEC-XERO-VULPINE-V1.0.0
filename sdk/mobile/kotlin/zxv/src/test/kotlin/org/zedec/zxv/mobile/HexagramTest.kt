// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.mobile

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertTrue

class HexagramTest {
    private val hex = Vectors["hex"]

    @Test
    fun all64TagsMatchTheVectors() {
        val tags = hex.obj("tags").arr()
        assertEquals(64, tags.size)
        for (e in tags) {
            val h = Hexagram.fromTag(e.obj("tag").i())
            assertEquals(e.obj("name").s(), h.name)
            assertEquals(e.obj("base").i(), h.base.code)
            assertEquals(e.obj("polarity").i(), h.polarity.code)
            assertEquals(e.obj("phase").i(), h.phase.code)
            assertEquals(e.obj("action").i(), h.action.code)
            assertEquals(h, Hexagram.of(h.base, h.polarity, h.phase))
            assertEquals(h.polarity == HexPolarity.FORBIDDEN, h.isForbidden)
        }
        for ((i, n) in hex.obj("action_names").arr().withIndex()) assertEquals(n.s(), HexAction.fromCode(i).actionName)
    }

    @Test
    fun notAndOrTables() {
        val not = hex.obj("not").arr()
        val and = hex.obj("and").arr()
        val or = hex.obj("or").arr()
        var checked = 0
        for (a in 0 until 64) {
            val ha = Hexagram.fromTag(a)
            assertEquals(not[a].i(), (!ha).tag)
            for (b in 0 until 64) {
                val hb = Hexagram.fromTag(b)
                assertEquals(and[a].arr()[b].i(), (ha and hb).tag, "and $a $b")
                assertEquals(or[a].arr()[b].i(), (ha or hb).tag, "or $a $b")
                checked += 2
            }
        }
        assertEquals(8192, checked)
    }

    @Test
    fun stepAndSetRefuseWhatTheVectorsRefuse() {
        val step = hex.obj("step").arr()
        val set = hex.obj("set").arr()
        for (t in 0 until 64) {
            val h = Hexagram.fromTag(t)
            for (p in 0 until 4) {
                val want = step[t].arr()[p].i()
                if (want < 0) {
                    assertEquals(-9, assertFailsWith<ZxvRefusedException> { h.step(HexPhase.fromCode(p)) }.status)
                } else {
                    assertEquals(want, h.step(HexPhase.fromCode(p)).tag)
                }
            }
            for (bp in 0 until 16) {
                val want = set[t].arr()[bp].i()
                val base = HexBase.fromCode(bp shr 2)
                val pol = HexPolarity.fromCode(bp and 3)
                if (want < 0) {
                    assertEquals(-9, assertFailsWith<ZxvRefusedException> { h.withState(base, pol) }.status)
                } else {
                    assertEquals(want, h.withState(base, pol).tag)
                }
            }
            // advance() never fails and forbid() is absorbing.
            assertEquals(h.phase.next(), h.advance().phase)
            assertTrue(h.forbid().isForbidden)
            assertTrue(h.forbid().advance().isForbidden)
        }
    }

    @Test
    fun phaseCycleAndPolarity() {
        for ((i, n) in hex.obj("phase_next").arr().withIndex()) assertEquals(n.i(), HexPhase.fromCode(i).next().code)
        for (e in hex.obj("polarity").arr()) {
            val p = Hexagram.polarityFromEvidence(
                e.obj("pos0").l(), e.obj("neg0").l(), e.obj("pos1").l(), e.obj("neg1").l(),
                e.obj("stale").s() == "true",
            )
            assertEquals(e.obj("polarity").i(), p.code)
        }
        assertFailsWith<ZxvUsageException> { Hexagram.polarityFromEvidence(-1, 0, 0, 0, false) }
        assertFailsWith<ZxvUsageException> { Hexagram.polarityFromEvidence(1L shl 32, 0, 0, 0, false) }
    }

    @Test
    fun truthBridgeIsExplicitAndNotNumeric() {
        val l2b = hex.obj("lp_to_base").arr()
        val b2l = hex.obj("base_to_lp").arr()
        for (t in Truth.entries) {
            val h = Hexagram.fromTruth(t, HexPolarity.NEUTRAL, HexPhase.POTENTIAL)
            assertEquals(l2b[t.code].i(), h.base.code)
            assertEquals(t, h.toTruth())
        }
        for (b in HexBase.entries) assertEquals(b2l[b.code].i(), Hexagram.of(b, HexPolarity.NEUTRAL, HexPhase.POTENTIAL).toTruth().code)
        // TRUE is base 3 but Truth 1: names match, numbers do not.
        assertEquals(HexBase.TRUE, Hexagram.fromTruth(Truth.TRUE, HexPolarity.NEUTRAL, HexPhase.POTENTIAL).base)
        assertEquals(3, HexBase.TRUE.code)
        assertEquals(1, Truth.TRUE.code)
    }

    @Test
    fun outOfRangeTagsAreUsageErrorsNeverWrapped() {
        for (bad in listOf(64, 199, 255, 256, -1, -57, Int.MAX_VALUE)) {
            assertEquals(-1, assertFailsWith<ZxvUsageException>("$bad") { Hexagram.fromTag(bad) }.status)
        }
        val h = Hexagram.of(HexBase.TRUE, HexPolarity.NEGENTROPIC, HexPhase.POTENTIAL)
        assertEquals(HexAction.PROCEED, h.action)
        assertEquals(HexPhase.EMERGING, h.advance().phase)
    }
}
