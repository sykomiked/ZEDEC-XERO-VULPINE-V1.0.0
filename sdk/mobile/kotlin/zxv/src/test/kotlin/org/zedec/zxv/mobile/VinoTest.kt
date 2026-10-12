// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.mobile

import kotlin.reflect.KClass
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse
import kotlin.test.assertIs
import kotlin.test.assertSame
import kotlin.test.assertTrue
import org.zedec.zxv.mobile.internal.Status

/** ABI 1.6 vino: every `status_abi16` and `vino` vector, plus lifetimes, capabilities and the poisoned context. */
class VinoTest {
    private val v = V15.sec("vino")

    // ---- statuses, features, capability bits ----

    @Test
    fun statusVectors() {
        for (s in Vectors["status_abi16"].arr()) {
            val code = s.obj("code").i()
            assertEquals(s.obj("name").s(), Zxv.statusName(code))
            assertEquals(StatusClass.fromCode(s.obj("class").i()), Zxv.statusClass(code))
        }
    }

    @Test
    fun everyNewStatusMapsToItsTypedException() {
        val want: List<Triple<Int, KClass<*>, KClass<*>>> = listOf(
            Triple(ZxvStatus.E_RAIL, RailException::class, ZxvRefusedException::class),
            Triple(ZxvStatus.E_COVERAGE, CoverageException::class, ZxvRefusedException::class),
            Triple(ZxvStatus.E_LANE, LaneException::class, ZxvUsageException::class),
            Triple(ZxvStatus.E_EXPIRED, ExpiredException::class, ZxvRefusedException::class),
            Triple(ZxvStatus.E_CHANCE, ChanceException::class, ZxvRefusedException::class),
        )
        for ((code, exact, base) in want) {
            val e = Status.raise(code, "probe", null)
            assertSame(exact.java, e.javaClass, "status $code")
            assertTrue(base.isInstance(e))
            val z = assertIs<ZxvException>(e)
            assertEquals(code, z.status)
            assertEquals(Zxv.statusName(code), z.statusName)
        }
        assertEquals(listOf(-46, -47, -48, -49, -50), listOf(ZxvStatus.E_RAIL, ZxvStatus.E_COVERAGE, ZxvStatus.E_LANE, ZxvStatus.E_EXPIRED, ZxvStatus.E_CHANCE))
    }

    @Test
    fun featureAndCapabilityBit() {
        assertTrue(Zxv.version().minor >= 6)
        assertTrue(Zxv.hasFeature(Feature2.VINO))
        assertEquals(0x100L, Feature2.VINO.bit)
        assertEquals(1L shl 15, Permission.VINO.bit)
        assertEquals((1L shl 16) - 1, Permission.ALL_1_6)
        assertEquals(Permission.ALL_1_6, Permission.mask(Permission.all()))
        ZxvContext.create().use { ctx ->
            ctx.rootCapability(Permission.VINO).use { cap -> assertEquals(setOf(Permission.VINO), cap.info().permissions) }
        }
    }

    // ---- payment requests ----

    private fun request(c: kotlinx.serialization.json.JsonElement) = PaymentRequest(
        c.obj("lane").i(), V15.hexOf(c.obj("merchant")), V15.hexOf(c.obj("nonce")), V15.u64(c.obj("amount")),
        V15.u64(c.obj("created_at")), V15.u64(c.obj("expires_at")), c.obj("memo").s(), c.obj("flags").i(),
    )

    @Test
    fun requestVectors() {
        for (c in v.obj("requests").arr()) {
            val req = request(c)
            val bytes = V15.expect(c.obj("status")) { req.encode() } ?: continue
            assertEquals(c.obj("bytes").s(), bytes.toHex())
            assertEquals(c.obj("id").s(), req.id().toHex())
            assertEquals(c.obj("id").s(), PaymentRequest.idOf(bytes).toHex())
            // Round trip: decode gives back the same value, memo and 64-bit fields exact.
            val back = PaymentRequest.decode(bytes)
            assertEquals(req, back)
            assertEquals(c.obj("memo").s(), back.memo)
            assertEquals(c.obj("expires_at").s(), java.lang.Long.toUnsignedString(back.expiresAt))
            assertEquals(c.obj("lane").i(), back.lane.code)
        }
        // The first is a LIVE request; the second a PLAY request with a chance outcome (allowed on PLAY).
        val r1 = PaymentRequest.decode(V15.hexOf(v.obj("requests").arr()[1].obj("bytes")))
        assertEquals(VinoLane.PLAY, r1.lane)
        assertTrue(r1.hasChanceOutcome)
    }

    @Test
    fun typedRequestRefusals() {
        val m = ByteArray(32) { 1 }
        val n = ByteArray(16) { 2 }
        assertFailsWith<ChanceException> { PaymentRequest(VinoLane.LIVE, m, n, 5, 1, 2, flags = Vino.FLAG_CHANCE).encode() }
        assertFailsWith<LaneException> { PaymentRequest(0, m, n, 5, 1, 2, "", 0).encode() }
        assertEquals(ZxvStatus.E_FORMAT, assertFailsWith<ZxvUsageException> { PaymentRequest(VinoLane.LIVE, m, n, 5, 1, 2, "café").encode() }.status)
        assertEquals(ZxvStatus.E_ARG, assertFailsWith<ZxvUsageException> { PaymentRequest(VinoLane.LIVE, m, n, 5, 1, 2, "a\u0000b").encode() }.status)
        assertFailsWith<ZxvUsageException> { PaymentRequest(VinoLane.LIVE, ByteArray(31), n, 5, 1, 2) }
        assertFailsWith<ZxvUsageException> { PaymentRequest.decode(ByteArray(159)) }
        // The value is immutable: changing a returned array changes nothing.
        val r = PaymentRequest(VinoLane.PLAY, m, n, 5, 1, 2, "x")
        r.merchant[0] = 9
        m[0] = 9
        assertEquals(1, r.merchant[0].toInt())
    }

    @Test
    fun decodeVectors() {
        val good = V15.hexOf(v.obj("requests").arr()[0].obj("bytes"))
        assertEquals(PaymentRequest.decode(good).encode().toHex(), good.toHex())
        for (c in v.obj("decode").arr()) {
            val b = good.copyOf()
            b[c.obj("offset").i()] = c.obj("value").i().toByte()
            V15.expect(c.obj("status")) { PaymentRequest.decode(b) }
        }
    }

    // ---- receipts ----

    @Test
    fun receiptVectors() {
        val r = v.obj("receipt")
        val request = V15.hexOf(v.obj("requests").arr()[0].obj("bytes"))
        val requestId = PaymentRequest.idOf(request)
        ZxvContext.create().use { ctx ->
            V15.root(ctx).use { cap ->
                val dsa = MlDsa87(cap)
                SecretKey.fromBytes(V15.hexOf(r.obj("seed"))).use { seed -> dsa.generate(seed) }.use { wallet ->
                    assertEquals(r.obj("pk_sha3_256").s(), Hashes.sha3_256(wallet.publicKey).toHex())
                    val approvedAt = V15.u64(r.obj("approved_at"))
                    val msg = VinoReceipt.message(requestId, wallet.publicKey, approvedAt)
                    assertEquals(r.obj("msg").s(), msg.toHex())
                    val sig = dsa.signDeterministic(wallet.secretKey, msg, ByteArray(0))
                    assertEquals(r.obj("sig").s(), sig.toHex())
                    for (c in r.obj("checks").arr()) {
                        val s = sig.copyOf()
                        val flip = c.obj("flip_sig").i()
                        if (flip >= 0) s[flip] = (s[flip].toInt() xor 1).toByte()
                        V15.expect(c.obj("status")) {
                            VinoReceipt.verify(request, wallet.publicKey, V15.u64(c.obj("approved_at")), s, V15.u64(c.obj("now")))
                        }
                        assertEquals(c.obj("status").s() == "ZXV_OK", VinoReceipt.isValid(request, wallet.publicKey, V15.u64(c.obj("approved_at")), s, V15.u64(c.obj("now"))))
                    }
                    // The wallet's own call: a hedged signature that verifies offline.
                    val live = VinoReceipt.sign(cap, request, wallet.secretKey, wallet.publicKey, approvedAt)
                    VinoReceipt.verify(request, wallet.publicKey, approvedAt, live, approvedAt + 1)
                    assertFailsWith<ExpiredException> { VinoReceipt.sign(cap, request, wallet.secretKey, wallet.publicKey, 1_700_000_601) }
                    assertFailsWith<VerificationFailedException> { VinoReceipt.verify(request, wallet.publicKey, approvedAt + 1, live, approvedAt + 1) }
                    V15.without(cap, Permission.CRYPTO_SIGN).use { app ->
                        assertFailsWith<CapabilityDeniedException> { VinoReceipt.sign(app, request, wallet.secretKey, wallet.publicKey, approvedAt) }
                    }
                    assertFailsWith<ZxvUsageException> { VinoReceipt.sign(cap, request, SecretKey.fromBytes(ByteArray(32)), wallet.publicKey, approvedAt) }
                    assertFailsWith<ZxvUsageException> { VinoReceipt.verify(request, wallet.publicKey, approvedAt, live.copyOf(10), approvedAt) }
                }
            }
        }
    }

    // ---- the play lane ----

    @Test
    fun playScenario() {
        val p = v.obj("play")
        ZxvContext.create().use { ctx ->
            ctx.rootCapability(Permission.VINO).use { cap ->
                PlaySandbox.create(cap).use { sb ->
                    for (st in p.obj("steps").arr()) {
                        val status = st.obj("status")
                        fun acct(k: String) = st.obj(k).l()
                        fun rail() = VinoRail.fromCode(st.obj("rail").i())
                        when (val op = st.obj("op").s()) {
                            "open" -> when (val kind = st.obj("is_issuer").i()) {
                                0, 1 -> V15.expect(status) { sb.open(acct("owner"), kind == 1) }?.let { assertEquals(st.obj("account").l(), it) }
                                // A Boolean cannot carry is_issuer = 2, so the ABI's ZXV_E_ARG for it is unreachable here.
                                else -> assertEquals("ZXV_E_ARG", status.s())
                            }
                            "back" -> V15.expect(status) { sb.back(acct("issuer"), V15.u64(st.obj("amount"))) }
                            "unback" -> V15.expect(status) { sb.unback(acct("issuer"), V15.u64(st.obj("amount"))) }
                            "issue" -> V15.expect(status) { sb.issue(acct("issuer"), acct("holder"), V15.u64(st.obj("face")), rail()) }
                                ?.let { assertEquals(V15.u64(st.obj("id")), it) }
                            "transfer" -> V15.expect(status) { sb.transfer(V15.u64(st.obj("voucher")), acct("from"), acct("to"), rail()) }
                            "set_rail" -> V15.expect(status) { sb.setRail(V15.u64(st.obj("voucher")), acct("holder"), rail()) }
                            "redeem" -> V15.expect(status) { sb.redeem(V15.u64(st.obj("voucher")), acct("holder")) }
                            else -> error("unknown op $op")
                        }
                    }
                    for (a in p.obj("accounts").arr()) {
                        val id = a.obj("account").l()
                        assertEquals(
                            PlaySandbox.Account(id, a.obj("owner").l(), a.obj("is_issuer").i() == 1, V15.u64(a.obj("backing")), V15.u64(a.obj("faces")), a.obj("count").l()),
                            sb.account(id),
                        )
                        assertEquals(a.obj("vouchers").arr().map { V15.u64(it) }, sb.vouchers(id))
                    }
                    for (x in p.obj("vouchers").arr()) {
                        val id = V15.u64(x.obj("id"))
                        assertEquals(
                            PlaySandbox.Voucher(id, V15.u64(x.obj("face")), x.obj("issuer").l(), x.obj("holder").l(), VinoRail.fromCode(x.obj("rail").i()), VoucherState.fromCode(x.obj("state").i())),
                            sb.voucher(id),
                        )
                    }
                    sb.check()
                    // Typed refusals on the same sandbox.
                    assertFailsWith<RailException> { sb.transfer(2, 2, 1, VinoRail.BEARER) }
                    assertFailsWith<RailException> { sb.setRail(2, 2, VinoRail.GHOST) }
                    assertFailsWith<CoverageException> { sb.issue(0, 1, 1, VinoRail.BEARER) }
                    assertFailsWith<NotADenominationException> { sb.issue(0, 1, 4, VinoRail.BEARER) }
                    assertEquals(ZxvStatus.E_NOT_FOUND, assertFailsWith<ZxvException> { sb.voucher(99) }.status)
                    assertEquals(ZxvStatus.E_NOT_FOUND, assertFailsWith<ZxvException> { sb.vouchers(77) }.status)
                    assertFailsWith<ZxvUsageException> { sb.account(-1) }
                }
            }
        }
    }

    @Test
    fun voucherListsGrowPastAnyFirstGuess() {
        ZxvContext.create().use { ctx ->
            ctx.rootCapability(Permission.VINO).use { cap ->
                PlaySandbox.create(cap).use { sb ->
                    val iss = sb.openIssuer(1)
                    val holder = sb.openHolder(2)
                    val other = sb.openHolder(3)
                    assertEquals(emptyList(), sb.vouchers(holder))
                    sb.back(iss, 1_000_000)
                    val ids = (1..300).map { sb.issue(iss, if (it % 3 == 0) other else holder, 1, VinoRail.GHOST) }
                    assertEquals(ids, sb.vouchers(iss))
                    assertEquals(ids.filterIndexed { i, _ -> (i + 1) % 3 != 0 }, sb.vouchers(holder))
                    assertEquals(100, sb.vouchers(other).size)
                    sb.redeem(ids[0], holder)
                    assertFalse(ids[0] in sb.vouchers(iss), "redeemed vouchers are not listed")
                    assertEquals(VoucherState.REDEEMED, sb.voucher(ids[0]).state)
                    assertEquals(299L, sb.account(iss).faces)
                    assertEquals(1_000_000L, sb.account(iss).backing, "redeeming pays nothing out")
                    sb.check()
                }
            }
        }
    }

    // ---- the meter ----

    @Test
    fun meterScenario() {
        val m = v.obj("meter")
        ZxvContext.create().use { ctx ->
            ctx.rootCapability(Permission.VINO).use { cap ->
                SpendMeter.create(cap, V15.u64(m.obj("daily_cap"))).use { meter ->
                    assertEquals(100, meter.dailyCap)
                    for (st in m.obj("steps").arr()) {
                        val day = V15.u64(st.obj("day"))
                        val amount = V15.u64(st.obj("amount"))
                        when (st.obj("status").s()) {
                            "ZXV_OK" -> assertEquals(SpendMeter.Admission(true, V15.u64(st.obj("left"))), meter.tryAdmit(day, amount))
                            "ZXV_E_LIMIT" -> assertEquals(SpendMeter.Admission(false, V15.u64(st.obj("left"))), meter.tryAdmit(day, amount))
                            else -> V15.expect(st.obj("status")) { meter.tryAdmit(day, amount) }
                        }
                    }
                }
                // admit() throws for the cap; tryAdmit() does not.
                SpendMeter.create(cap, 10).use { meter ->
                    assertEquals(4, meter.admit(1, 6))
                    assertFailsWith<LimitException> { meter.admit(1, 5) }
                    assertEquals(0, meter.admit(1, 4))
                    assertEquals(ZxvStatus.E_RANGE, assertFailsWith<ZxvException> { meter.admit(0, 1) }.status)
                }
                assertEquals(ZxvStatus.E_RANGE, assertFailsWith<ZxvException> { SpendMeter.create(cap, 0) }.status)
                assertEquals(ZxvStatus.E_RANGE, assertFailsWith<ZxvException> { SpendMeter.create(cap, Vino.AMOUNT_MAX + 1) }.status)
            }
        }
    }

    // ---- lifetimes, capabilities, poisoned context ----

    @Test
    fun useAfterCloseIsRefused() {
        ZxvContext.create().use { ctx ->
            ctx.rootCapability(Permission.VINO).use { cap ->
                val sb = PlaySandbox.create(cap)
                val meter = SpendMeter.create(cap, 10)
                sb.openIssuer(1)
                for ((h, use) in listOf<Pair<AutoCloseable, () -> Any?>>(sb to { sb.account(0) }, meter to { meter.admit(1, 1) })) {
                    h.close()
                    h.close() // idempotent
                    val e = assertFailsWith<IllegalStateException> { use() }
                    assertTrue(e.message!!.contains("closed"), e.message)
                }
                assertTrue(sb.isClosed && meter.isClosed)
            }
        }
    }

    @Test
    fun closingTheContextClosesThem() {
        val ctx = ZxvContext.create()
        val cap = ctx.rootCapability(Permission.VINO)
        val sb = PlaySandbox.create(cap)
        val meter = SpendMeter.create(cap, 10)
        ctx.close()
        assertTrue(sb.isClosed)
        assertTrue(meter.isClosed)
        assertFailsWith<IllegalStateException> { sb.check() }
    }

    @Test
    fun capabilityWithoutVinoIsRefused() {
        ZxvContext.create().use { ctx ->
            V15.root(ctx).use { root ->
                V15.without(root, Permission.VINO).use { noVino ->
                    assertEquals(ZxvStatus.E_CAP, assertFailsWith<CapabilityDeniedException> { PlaySandbox.create(noVino) }.status)
                    assertEquals(ZxvStatus.E_CAP, assertFailsWith<CapabilityDeniedException> { SpendMeter.create(noVino, 10) }.status)
                    // Pure calls need no capability at all.
                    assertEquals(Vino.REQUEST_BYTES, PaymentRequest(VinoLane.LIVE, ByteArray(32), ByteArray(16), 1, 1, 2).encode().size)
                }
                // A sandbox whose capability is revoked later refuses every call.
                val narrow = root.attenuate(Permission.VINO)
                PlaySandbox.create(narrow).use { sb ->
                    sb.openIssuer(1)
                    narrow.revoke()
                    assertFailsWith<CapabilityDeniedException> { sb.account(0) }
                    assertFailsWith<CapabilityDeniedException> { sb.openHolder(2) }
                }
            }
        }
    }

    @Test
    fun poisonedContextFailsClosed() {
        ZxvContext.create().use { ctx ->
            ctx.rootCapability(Permission.VINO).use { cap ->
                PlaySandbox.create(cap).use { sb ->
                    SpendMeter.create(cap, 10).use { meter ->
                        val iss = sb.openIssuer(1)
                        ctx.halt("test: poisoned context")
                        val e = assertFailsWith<ZxvFatalError> { sb.back(iss, 5) }
                        assertEquals(ZxvStatus.E_POISONED, e.status)
                        assertTrue(ctx.isPoisoned)
                        assertFailsWith<ZxvFatalError> { sb.account(iss) }
                        assertFailsWith<ZxvFatalError> { meter.admit(1, 1) }
                        assertFailsWith<ZxvFatalError> { PlaySandbox.create(cap) }
                        assertFalse(sb.isClosed, "a poisoned handle can still be closed")
                    }
                }
            }
        }
    }

    /** The two README examples, as written there. */
    @Test
    fun readmeExamples() {
        ZxvContext.create().use { ctx ->
            ctx.rootCapability(Permission.VINO).use { cap ->
                PlaySandbox.create(cap).use { play ->
                    val bank = play.openIssuer(owner = 1)
                    val player = play.openHolder(owner = 100)
                    val shop = play.openHolder(owner = 200)
                    play.back(bank, 18)
                    val coin = play.issue(bank, player, 8, VinoRail.BEARER)
                    play.transfer(coin, player, shop, VinoRail.BEARER)
                    play.redeem(coin, shop)
                    check(play.voucher(coin).state == VoucherState.REDEEMED)
                    check(play.account(bank).faces == 0L)
                }
            }
        }
        val now = 1_700_000_000L
        val request = PaymentRequest(
            VinoLane.LIVE, merchant = ByteArray(32) { 7 }, nonce = ByteArray(16) { 9 },
            amount = 1_250, createdAt = now, expiresAt = now + 600, memo = "Sword of Dawn",
        )
        val bytes = request.encode()
        ZxvContext.create().use { ctx ->
            ctx.rootCapability(Permission.CRYPTO_SIGN).use { walletCap ->
                MlDsa87(walletCap).generate().use { wallet ->
                    val sig = VinoReceipt.sign(walletCap, bytes, wallet.secretKey, wallet.publicKey, now + 30)
                    VinoReceipt.verify(bytes, wallet.publicKey, now + 30, sig, now = now + 60)
                    check(PaymentRequest.decode(bytes) == request)
                }
            }
        }
    }

    @Test
    fun enumsRoundTrip() {
        for (r in VinoRail.entries) assertSame(r, VinoRail.fromCode(r.code))
        for (l in VinoLane.entries) assertSame(l, VinoLane.fromCode(l.code))
        for (s in VoucherState.entries) assertSame(s, VoucherState.fromCode(s.code))
        assertFailsWith<IllegalArgumentException> { VinoRail.fromCode(2) }
    }
}
