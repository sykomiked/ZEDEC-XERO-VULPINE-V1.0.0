// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.mobile

import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse
import kotlin.test.assertTrue
import org.zedec.zxv.mobile.V15.opt

/** ABI 1.5 CID and AIPI vectors, plus the offline AIPI node. */
class CidAipiTest {
    private val c0 = V15.sec("cid")
    private val a0 = V15.sec("aipi")

    @Test
    fun computeAndText() {
        for (c in c0.obj("compute").arr()) {
            val cid = Cid.compute(V15.u64(c.obj("codec")), V15.utf8(c.obj("utf8")))
            assertEquals(c.obj("cid").s(), cid.toHex())
            assertEquals(c.obj("text_default").s(), cid.toString())
            assertEquals(c.obj("text_58btc").s(), cid.toText(CidBase.BASE58BTC))
            assertEquals(c.obj("version").i(), cid.version)
            assertEquals(c.obj("hash").i(), cid.multihash)
            assertEquals(V15.u64(c.obj("codec")), cid.codec)
            assertEquals(c.obj("digest").s(), cid.digest.toHex())
            assertTrue(cid.matches(V15.utf8(c.obj("utf8"))))
        }
    }

    @Test
    fun unixfs() {
        for (c in c0.obj("unixfs").arr()) {
            val data = if (c.has("ramp")) V15.ramp(c.obj("len").i()) else V15.utf8(c.obj("utf8"))
            val cid = Cid.unixfs(data)
            assertEquals(c.obj("cid").s(), cid.toHex())
            assertEquals(c.obj("text_default").s(), cid.toString())
            c.opt("codec")?.let { assertEquals(V15.u64(it), cid.codec) }
        }
    }

    @Test
    fun parseText() {
        for (c in c0.obj("text").arr()) {
            val cid = Cid.parse(c.obj("text").s())
            assertEquals(c.obj("cid").s(), cid.toHex())
            assertEquals(c.obj("text_default").s(), cid.toString())
            if (c.obj("text_58btc").s() == "ZXV_E_CID") {
                assertFailsWith<CidException> { cid.toText(CidBase.BASE58BTC) }
            } else {
                assertEquals(c.obj("text_58btc").s(), cid.toText(CidBase.BASE58BTC))
            }
            assertEquals(c.obj("version").i(), cid.version)
            assertEquals(V15.u64(c.obj("codec")), cid.codec)
            assertEquals(c.obj("digest").s(), cid.digest.toHex())
            assertEquals(c.obj("v1").s(), cid.toV1().toHex())
            assertEquals(c.obj("v1_text").s(), cid.toV1().toString())
            assertEquals(c.obj("provider_key").s(), cid.providerKey().toHex())
            assertEquals(cid, Cid.of(cid.bytes))
        }
        for (c in c0.obj("text_reject").arr()) V15.expect(c.obj("status")) { Cid.parse(c.obj("text").s()) }
    }

    @Test
    fun verify() {
        for (c in c0.obj("verify").arr()) {
            V15.expect(c.obj("status")) { Cid.of(V15.hexOf(c.obj("cid"))).verify(V15.utf8(c.obj("utf8"))) }
        }
        assertFalse(Cid.raw(V15.utf8("abc")).matches(V15.utf8("abd")))
    }

    private fun moduleCid(): Cid {
        val m = Cid.raw(V15.utf8(a0.obj("module_utf8")))
        assertEquals(a0.obj("module_cid").s(), m.toHex())
        return m
    }

    @Test
    fun functionRecords() {
        val module = moduleCid()
        val contract = Cid.raw(V15.utf8(a0.obj("contract_utf8")))
        assertEquals(a0.obj("contract_cid").s(), contract.toHex())
        for (c in a0.obj("fn").arr()) {
            val f = V15.expect(c.obj("status")) {
                FunctionRecord.encode(
                    Intent.fromCode(c.obj("intent").i()), VerifyMode.fromCode(c.obj("verify").i()),
                    Privacy.fromCode(c.obj("privacy").i()), Determinism.fromCode(c.obj("determinism").i()),
                    c.obj("quorum_n").i(), c.obj("quorum_k").i(), V15.u64(c.obj("fuel_max")), module,
                    if (V15.bool(c.obj("contract"))) contract else null, c.obj("entry").s(), V15.str(c.opt("checker")),
                    V15.str(c.opt("certifier")),
                )
            } ?: continue
            assertEquals(c.obj("record").s(), f.bytes.toHex())
            assertEquals(c.obj("fn_cid").s(), f.cid.toHex())
            assertEquals(c.obj("fn_cid_text").s(), f.cid.toText(CidBase.BASE58BTC))
            val d = FunctionRecord.decode(f.bytes)
            assertEquals(f, d)
            assertEquals(c.obj("entry").s(), d.entry)
            assertEquals(V15.u64(c.obj("fuel_max")), d.fuelMax)
            assertEquals(V15.bool(c.obj("contract")), d.contract != null)
        }
        val b = FunctionRecord.builder().module(module).entry("main").fuelMax(1_000_000).verifyByChecker("check").build()
        assertEquals(a0.obj("fn").arr()[0].obj("record").s(), b.bytes.toHex())
        assertEquals(ZxvStatus.E_FORMAT, assertFailsWith<ZxvException> { FunctionRecord.decode(byteArrayOf(1, 2, 3)) }.status)
    }

    @Test
    fun callAndClaimRecords() {
        val module = moduleCid()
        val fn = Cid.of(V15.hexOf(a0.obj("fn").arr()[0].obj("fn_cid")))
        val calls = a0.obj("call").arr()
        for (c in calls) {
            val target = if (c.obj("fn").s() == "module_cid") module else fn
            val input = V15.utf8(c.obj("input_utf8"))
            val r = V15.expect(c.opt("status")) { CallRecord.encode(target, input) }
            if (r == null) {
                assertFailsWith<CidException> { CallRecord.cidOf(target, input) }
                continue
            }
            assertEquals(c.obj("record").s(), r.bytes.toHex())
            assertEquals(c.obj("call_cid").s(), r.cid.toHex())
            assertEquals(c.obj("call_cid_text").s(), r.cid.toText(CidBase.BASE58BTC))
            assertEquals(r.cid, CallRecord.cidOf(fn, input))
            val d = CallRecord.decode(r.bytes)
            assertEquals(fn, d.function)
            assertContentEquals(input, d.input)
        }
        for (c in a0.obj("claim").arr()) {
            val call = Cid.of(V15.hexOf(calls[1].obj("call_cid")))
            val r = ClaimRecord.encode(call, V15.hexOf(c.obj("executor")), V15.utf8(c.obj("output_utf8")), V15.utf8(c.obj("cert_utf8")))
            assertEquals(c.obj("record").s(), r.bytes.toHex())
            val d = ClaimRecord.decode(r.bytes)
            assertEquals(call, d.call)
            assertContentEquals(V15.hexOf(c.obj("executor")), d.executor)
            assertContentEquals(V15.utf8(c.obj("output_utf8")), d.output)
            assertContentEquals(V15.utf8(c.obj("cert_utf8")), d.certificate)
        }
    }

    @Test
    fun offlineNode() {
        ZxvContext.create().use { ctx ->
            V15.root(ctx).use { cap ->
                SecretKey.fromBytes(V15.fill(0x5A, 32)).use { key ->
                    AipiNode.create(cap, 1L shl 20, 64, key).use { node ->
                        AipiNode.create(cap, AipiNode.STORE_MIN, 4).use { keyless ->
                            val mod = V15.utf8(a0.obj("module_utf8"))
                            val m = node.put(mod)
                            assertEquals(a0.obj("module_cid").s(), m.toHex())
                            assertEquals(m, node.put(mod), "putting the same bytes again is a no-op")
                            assertContentEquals(mod, node.get(m))
                            val big = V15.ramp(10_000)
                            assertContentEquals(big, node.get(node.put(big)))
                            val secret = V15.utf8("private notes")
                            val sc = node.putPrivate(secret)
                            assertContentEquals(secret, node.get(sc))
                            assertEquals(ZxvStatus.E_STATE, assertFailsWith<ZxvException> { node.put(secret) }.status)
                            assertEquals(ZxvStatus.E_STATE, assertFailsWith<ZxvException> { keyless.putPrivate(secret) }.status)
                            assertEquals(
                                ZxvStatus.E_NOT_FOUND,
                                assertFailsWith<ZxvException> { node.get(Cid.raw(V15.utf8("never stored"))) }.status,
                            )
                            val f = FunctionRecord.builder().module(m).entry("main").fuelMax(10).verifyLocal().build()
                            val fc = node.publish(f)
                            assertEquals(f.cid, fc)
                            assertEquals(f, node.loadFunction(fc))
                            assertEquals(Route.LOCAL, node.plan(fc, V15.utf8("x"), false))
                            val orphan = FunctionRecord.builder().module(Cid.raw(V15.utf8("elsewhere"))).entry("main")
                                .fuelMax(10).verifyLocal().build()
                            assertEquals(Route.UNAVAILABLE, node.plan(node.publish(orphan), null, false))
                            val info = node.info()
                            assertEquals(6, info.blocks, "four public blocks; a private block takes two entries")
                            assertTrue(info.storeUsed > 10_000)
                            assertTrue(info.getsVerified >= 4)
                            assertEquals(0, info.forbidden)
                        }
                    }
                }
            }
        }
    }

    @Test
    fun nodeNeedsAipiCapability() {
        ZxvContext.create().use { ctx ->
            V15.root(ctx).use { root ->
                V15.without(root, Permission.AIPI).use { noAipi ->
                    val e = assertFailsWith<CapabilityDeniedException> { AipiNode.create(noAipi, 1L shl 16, 8) }
                    assertEquals(ZxvStatus.E_CAP, e.status)
                    AipiNode.create(root, 1L shl 16, 8).use { assertEquals(0, it.info().blocks) }
                }
            }
        }
    }
}
