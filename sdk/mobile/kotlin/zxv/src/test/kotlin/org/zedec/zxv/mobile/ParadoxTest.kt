// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.mobile

import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse
import kotlin.test.assertNotEquals
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlinx.serialization.json.JsonElement
import kotlinx.serialization.json.JsonNull
import kotlinx.serialization.json.jsonObject
import org.zedec.zxv.mobile.V15.opt

/** ABI 1.5 paradox vectors: every step runs in order on ONE book (log capacity 64, registry 16). */
class ParadoxTest {
    private val v = V15.sec("paradox")

    private fun tag(e: JsonElement) = Hexagram.fromTag(e.i())
    private fun kind(e: JsonElement) = ParadoxKind.fromCode(e.i())
    private fun longs(e: JsonElement) = e.arr().map { V15.i64(it) }.toLongArray()
    private fun ints(e: JsonElement) = e.arr().map { it.i() }
    private fun present(e: JsonElement?) = e != null && e !is JsonNull

    private fun heads(b: ParadoxBook, h: JsonElement) {
        for (log in ParadoxLog.entries) {
            val w = h.obj(log.name.lowercase())
            assertEquals(w.obj("count").i(), b.logCount(log), log.name)
            assertEquals(w.obj("head").s(), b.logHead(log).toHex(), log.name)
        }
    }

    @Test
    fun pureFunctions() {
        for (c in v.obj("signature").arr()) assertEquals(c.obj("sig").s(), Paradox.signature(V15.utf8(c.obj("utf8"))).toHex())
        val kn = v.obj("kind_names").arr()
        for (k in ParadoxKind.entries) assertEquals(kn[k.code].s(), Paradox.name(k))
        val on = v.obj("outcome_names").arr()
        for (k in Outcome.entries) assertEquals(on[k.code].s(), Paradox.name(k))
        val sn = v.obj("node_state_names").arr()
        for (k in NodeState.entries) assertEquals(sn[k.code].s(), Paradox.name(k))
        val kot = ints(v.obj("kind_of_tag"))
        for (t in 0 until 64) assertEquals(kot[t], Paradox.kindOfTag(Hexagram.fromTag(t)).code, "tag $t")
        val act = v.obj("action").arr()
        for (k in ParadoxKind.entries) {
            val row = ints(act[k.code])
            for (t in 0 until 64) {
                val h = Hexagram.fromTag(t)
                if (row[t] < 0) {
                    assertEquals(ZxvStatus.E_ARG, assertFailsWith<ZxvUsageException> { Paradox.action(k, h) }.status)
                } else {
                    assertEquals(ParadoxAction.fromCode(row[t]), Paradox.action(k, h))
                }
            }
        }
        val oa = ints(v.obj("outcome_action"))
        for (o in Outcome.entries) assertEquals(oa[o.code], Paradox.outcomeAction(o).code)
    }

    @Test
    fun oneBook() {
        val sigL = Paradox.signature(V15.utf8("loop:A->B->A"))
        ZxvContext.create().use { ctx ->
            V15.root(ctx).use { cap ->
                ParadoxBook.create(cap, 64, 16).use { b ->
                    heads(b, v.obj("heads_fresh"))
                    for (c in v.obj("classify").arr()) {
                        val ra = c.opt("registry_add")
                        if (ra != null) {
                            val idx = b.registryAdd(sigL, kind(ra.obj("kind")), ra.obj("reason").i(), ra.obj("flags").i(), V15.bool(ra.obj("owner_ok")))
                            assertEquals(ra.obj("index").i(), idx)
                            continue
                        }
                        val ev = c.opt("evidence")?.takeIf { present(it) }?.let {
                            val e = it.arr().map { x -> x.l() }
                            Evidence(e[0], e[1], e[2], e[3], V15.bool(c.obj("stale")))
                        }
                        val k = b.classify(tag(c.obj("tag")), ev, if (present(c.opt("sig"))) sigL else null)
                        assertEquals(Classification(kind(c.obj("kind")), tag(c.obj("out_tag")), c.obj("reg_index").i()), k)
                    }
                    for (c in v.obj("registry").arr()) {
                        V15.expect(c.obj("status")) {
                            b.registryAdd(Paradox.signature(V15.utf8(c.obj("utf8"))), kind(c.obj("kind")), 0, c.obj("flags").i(), V15.bool(c.obj("owner_ok")))
                        }?.let { assertEquals(c.obj("index").i(), it) }
                    }
                    assertFailsWith<OwnerApprovalRequiredException> {
                        b.registryAdd(ByteArray(32), ParadoxKind.NEUTRAL, 0, ParadoxFlag.DEVICE, false)
                    }
                    heads(b, v.obj("heads_after_classify"))
                    assertEquals(0, b.registryFind(sigL))
                    assertEquals(Paradox.SRC_SEED, b.registryEntry(0).source)

                    constructive(cap, b)
                    destructive(b)
                    neutral(cap, b)
                    special(b)
                    for (g in v.obj("graphs").arr()) graph(cap, b, g)

                    heads(b, v.obj("heads_final"))
                    val logs = v.obj("logs")
                    for (log in ParadoxLog.entries) {
                        val recs = logs.obj(log.name.lowercase()).arr()
                        recs.forEachIndexed { i, want ->
                            val r = b.logRecord(log, i)
                            assertEquals(want.s(), r.toHex(), "$log $i")
                            if (log == ParadoxLog.QUARANTINE) assertEquals(i.toLong(), QuarantineRecord.parse(r).seq)
                            else assertEquals(i.toLong(), LogRecord.parse(r).seq)
                        }
                        assertEquals(ZxvStatus.E_NOT_FOUND, assertFailsWith<ZxvException> { b.logRecord(log, recs.size) }.status)
                    }
                    val lv = v.obj("log_verify").arr()
                    for (log in ParadoxLog.entries) V15.expect(lv[log.code]) { b.logVerify(log) }
                }
            }
        }
    }

    private fun constructive(cap: Capability, b: ParadoxBook) {
        val c = v.obj("constructive")
        val lo = longs(c.obj("lo"))
        val hi = longs(c.obj("hi"))
        val init = longs(c.obj("initial"))
        ParadoxPolicy.create(cap, lo, hi, init, 0).use { plain ->
            ParadoxPolicy.create(cap, lo, hi, init, ParadoxFlag.MONEY.toLong()).use { money ->
                assertContentEquals(hi, plain.bounds().hi)
                assertEquals(ParadoxFlag.MONEY.toLong(), money.bounds().flags)
                for (s in c.obj("steps").arr()) {
                    val p = if (s.obj("policy").s() == "money") money else plain
                    val synth = s.opt("synth")?.takeIf { present(it) }?.let { longs(it) }
                    V15.expect(s.obj("status")) { b.constructive(p, kind(s.obj("kind")), tag(s.obj("tag")), synth, s.obj("tension").l()) }
                        ?.let { assertEquals(Outcome.fromCode(s.obj("outcome").i()), it) }
                    val vals = p.values()
                    assertContentEquals(longs(s.obj("values")), vals.values)
                    assertContentEquals(longs(s.obj("prev")), vals.previous)
                    assertEquals(V15.bool(s.obj("armed")), vals.armed)
                    assertEquals(s.obj("tension_at").l(), vals.tensionAt)
                    s.opt("proposal")?.let { assertEquals(it.s(), p.proposal(synth!!).toHex()) }
                }
            }
        }
    }

    private fun destructive(b: ParadoxBook) {
        for (c in v.obj("destructive").arr()) {
            val trace = c.obj("trace").arr().map { V15.u64(it) }.toLongArray()
            val d = ParadoxBook.Detection(
                c.obj("steps").l(), c.obj("step_limit").l(), c.obj("spent").l(), c.obj("budget").l(), trace,
                V15.bool(c.obj("invariant_ok")),
            )
            val pattern = c.obj("pattern_utf8").s().takeIf { it.isNotEmpty() }?.let { V15.utf8(it) }
            val state = V15.utf8(c.obj("state_utf8"))
            val r = b.destructive(kind(c.obj("kind")), tag(c.obj("tag")), d, state, V15.utf8(c.obj("checkpoint_utf8")), pattern, c.obj("submitter").l())
            assertEquals(Outcome.fromCode(c.obj("outcome").i()), r.outcome)
            assertEquals(c.obj("state_after").s(), r.state.toHex())
            assertContentEquals(V15.utf8(c.obj("state_utf8")), state, "the caller's array is not modified")
            val rp = c.opt("repair_proof")
            if (rp != null) {
                val p = assertNotNull(r.proof)
                assertEquals(rp.s(), p.bytes.toHex())
                V15.expect(c.obj("repair_verify")) { p.verify() }
                val bad = p.bytes.also { it[0] = (it[0].toInt() xor 1).toByte() }
                V15.expect(c.obj("repair_verify_flipped_byte0")) { RepairProof.of(bad).verify() }
                assertFalse(RepairProof.of(bad).isValid())
                assertContentEquals(Paradox.signature(pattern), p.signature)
                assertEquals(0, p.quarantineIndex)
            } else {
                assertNull(r.proof)
            }
        }
    }

    private fun neutral(cap: Capability, b: ParadoxBook) {
        val c = v.obj("neutral")
        val br = c.obj("branches").arr()
        val b0 = V15.hexOf(br[0])
        val b1 = V15.hexOf(br[1])
        ParadoxHold.create(cap, c.obj("capacity").l(), c.obj("decisive").l()).use { h ->
            h.add(b0)
            h.add(b1)
            h.evidence(b0, true)
            h.evidence(b1, true)
            for (s in c.obj("steps").arr()) {
                if (s.opt("kind") == null) continue
                repeat(s.obj("add_for_branch0").i()) { h.evidence(b0, true) }
                V15.expect(s.obj("status")) { b.neutral(h, kind(s.obj("kind")), tag(s.obj("tag")), s.obj("owner_pick").i()) }
                    ?.let { assertEquals(ParadoxBook.Hold(Outcome.fromCode(s.obj("outcome").i()), s.obj("settled").l()), it) }
            }
            assertEquals(0, h.info().settled)
            assertContentEquals(b0, h.branch(0).id)
            assertEquals(3, h.branch(0).positive)
        }
    }

    private fun special(b: ParadoxBook) {
        val c = v.obj("special")
        val sg = Paradox.signature(V15.utf8(c.obj("sig_utf8")))
        assertEquals(c.obj("sig").s(), sg.toHex())
        val f = b.special(kind(c.obj("kind")), tag(c.obj("tag")), sg, V15.hexOf(c.obj("subject")))
        assertEquals(ParadoxBook.Frontier(Outcome.fromCode(c.obj("outcome").i()), c.obj("rec").l()), f)
        val dc = c.obj("draft")
        val dm = c.obj("draft_money")
        val draft = b.draft(f.record, kind(dc.obj("kind")), dc.obj("reason").i(), dc.obj("flags").i())
        assertEquals(dc.obj("bytes").s(), draft.bytes.toHex())
        val money = b.draft(f.record, kind(dm.obj("kind")), dm.obj("reason").i(), dm.obj("flags").i())
        assertEquals(dm.obj("bytes").s(), money.bytes.toHex())
        assertEquals(ParadoxFlag.MONEY, money.flags)
        for (r in c.obj("review").arr()) {
            val d = if (r.obj("draft").s() == "draft_money") money else draft
            V15.expect(r.obj("status")) { b.review(d, V15.bool(r.obj("owner_ok")), V15.bool(r.obj("consensus_ok"))) }
                ?.let { assertEquals(Outcome.fromCode(r.obj("outcome").i()), it) }
        }
        assertEquals(c.obj("registry_count_after").i(), b.registryCount())
    }

    private fun id(e: JsonElement): ByteArray = V15.fill(e.s()[0].code, 32)

    private fun graph(cap: Capability, b: ParadoxBook, g: JsonElement) {
        ParadoxGraph.create(cap, g.obj("step_limit").l()).use { graph ->
            for (n in g.obj("nodes").arr()) {
                V15.expect(n.obj("status")) { graph.addNode(id(n.obj("id")), kind(n.obj("kind")), tag(n.obj("tag")), n.obj("mag").l(), n.obj("dir").i()) }
            }
            for (d in g.obj("deps").arr()) V15.expect(d.obj("status")) { graph.addDependency(id(d.obj("id")), id(d.obj("dep"))) }
            for (p in g.obj("passes").arr()) {
                if (p.obj("status").s() == "ZXV_E_PARADOX_STEP") {
                    assertEquals(ZxvStatus.E_PARADOX_STEP, assertFailsWith<StepLimitException> { b.pass(graph) }.status)
                } else {
                    V15.expect(p.obj("status")) { b.pass(graph) }?.let { assertEquals(Outcome.fromCode(p.obj("outcome").i()), it) }
                }
                val info = graph.info()
                assertEquals(p.obj("pass").l(), info.passes)
                assertEquals(p.obj("total_mag").l(), info.totalMag)
                assertEquals(p.obj("total_dir").l(), info.totalDir)
                assertEquals(V15.bool(p.obj("escalated")), info.escalated)
                assertEquals(p.obj("chain").s(), info.chain.toHex())
                for ((key, value) in p.obj("states").jsonObject) {
                    assertEquals(NodeState.fromCode(value.i()), graph.state(V15.fill(key[0].code, 32)), key)
                }
            }
            assertNotEquals(NodeState.OPEN, graph.node(0).state)
        }
    }
}
