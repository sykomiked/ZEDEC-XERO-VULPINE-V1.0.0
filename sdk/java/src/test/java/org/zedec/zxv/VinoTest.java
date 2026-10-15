// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.util.ArrayList;
import java.util.List;
import org.junit.jupiter.api.Test;
import org.zedec.zxv.crypto.MlDsa87;
import org.zedec.zxv.crypto.SecretKey;
import org.zedec.zxv.internal.Access;
import org.zedec.zxv.internal.Errors;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Lease;
import org.zedec.zxv.internal.Native16;
import org.zedec.zxv.vhub.LimitException;
import org.zedec.zxv.vino.ChanceException;
import org.zedec.zxv.vino.CoverageException;
import org.zedec.zxv.vino.DailyCapException;
import org.zedec.zxv.vino.ExpiredException;
import org.zedec.zxv.vino.Lane;
import org.zedec.zxv.vino.LaneException;
import org.zedec.zxv.vino.PaymentRequest;
import org.zedec.zxv.vino.PlaySandbox;
import org.zedec.zxv.vino.RailException;
import org.zedec.zxv.vino.Rail;
import org.zedec.zxv.vino.Receipt;
import org.zedec.zxv.vino.SpendMeter;
import org.zedec.zxv.vino.Vino;

/** Vino Embed (ABI 1.6): error mapping, capabilities, handle lifetimes, the vouchers buffer, poisoning. */
class VinoTest {
    private static final byte[] MERCHANT = V15.ramp(32);
    private static final byte[] NONCE = V15.fill(0xa5, 16);

    @Test
    void eachNewStatusMapsToItsTypedExceptionAndClass() {
        Object[][] want = {
            {ZxvStatus.E_RAIL, -46, RailException.class, ZxvRefusedException.class},
            {ZxvStatus.E_COVERAGE, -47, CoverageException.class, ZxvRefusedException.class},
            {ZxvStatus.E_LANE, -48, LaneException.class, ZxvUsageException.class},
            {ZxvStatus.E_EXPIRED, -49, ExpiredException.class, ZxvRefusedException.class},
            {ZxvStatus.E_CHANCE, -50, ChanceException.class, ZxvRefusedException.class},
        };
        for (Object[] w : want) {
            int code = (int) w[0];
            assertEquals(w[1], code);
            RuntimeException e = Errors.raise(code, "probe", null);
            assertSame(w[2], e.getClass(), "status " + code);
            assertInstanceOf((Class<?>) w[3], e);
            ZxvException z = (ZxvException) e;
            assertEquals(code, z.status());
            assertEquals(Errors.statusClass(code), z.statusClass());
        }
        // A reserved code of the group (-51..-59) is not yet a typed exception.
        assertEquals("ZXV_E_UNKNOWN", Errors.name(-51));
    }

    @Test
    void featureAndCapabilityBits() {
        assertTrue(Zxv.version().minor() >= 6);
        assertEquals(0x100, Feature2.VINO.bit());
        assertTrue(Zxv.hasFeature(Feature2.VINO));
        assertEquals(1L << 15, Permission.VINO.bit());
        assertEquals((1L << 16) - 1, Permission.ALL_1_6);
        assertEquals(Permission.ALL_1_6, Permission.mask(Permission.all()));
        assertFalse(Permission.all1_5().contains(Permission.VINO));
        try (ZxvContext ctx = ZxvContext.create(); Capability cap = ctx.rootCapability(Permission.all())) {
            assertEquals(Permission.all(), cap.permissions(), "a 1.6 library accepts bit 1<<15");
        }
    }

    @Test
    void aCapabilityWithoutVinoIsRefused() {
        try (ZxvContext ctx = ZxvContext.create(); Capability root = V15.root(ctx);
                Capability noVino = V15.without(root, Permission.VINO);
                Capability noSign = V15.without(root, Permission.CRYPTO_SIGN)) {
            assertThrows(CapabilityDeniedException.class, () -> PlaySandbox.create(noVino));
            assertThrows(CapabilityDeniedException.class, () -> SpendMeter.create(noVino, 10));
            try (PlaySandbox sb = PlaySandbox.create(root); SpendMeter m = SpendMeter.create(root, 10)) {
                assertEquals(0, sb.openIssuer(1));
            }
            // Signing a receipt is the wallet's call: it needs CRYPTO_SIGN, not VINO.
            byte[] req = PaymentRequest.live(MERCHANT, NONCE, 5, 100, 200, "x").encode();
            try (MlDsa87.KeyPair kp = MlDsa87.with(root).generateKeyPair(V15.fill(7, 32))) {
                assertThrows(CapabilityDeniedException.class,
                        () -> Receipt.sign(noSign, req, kp.secretKey(), kp.publicKey(), 150));
                assertTrue(Receipt.sign(noVino, req, kp.secretKey(), kp.publicKey(), 150).verify(160));
            }
        }
    }

    @Test
    void aShopFromIssueToRedeemThroughTheTypedApi() {
        try (ZxvContext ctx = ZxvContext.create(); Capability cap = ctx.rootCapability(Permission.VINO);
                PlaySandbox shop = PlaySandbox.create(cap)) {
            int issuer = shop.openIssuer(1), alice = shop.openHolder(2), bob = shop.openHolder(3);
            shop.back(issuer, 9);
            assertThrows(CoverageException.class, () -> shop.issue(issuer, alice, 8, Rail.BEARER));
            long v = shop.issue(issuer, alice, 5, Rail.BEARER);
            assertThrows(RailException.class, () -> shop.transfer(v, alice, bob, Rail.GHOST));
            shop.transfer(v, alice, bob, Rail.BEARER);
            assertThrows(RailException.class, () -> shop.setRail(v, bob, Rail.BEARER));
            shop.setRail(v, bob, Rail.GHOST);
            assertEquals(5, shop.account(issuer).faces());
            shop.redeem(v, bob);
            assertEquals(0, shop.account(issuer).faces());
            assertEquals(9, shop.account(issuer).backing(), "redeeming pays nothing out of backing");
            assertFalse(shop.voucher(v).isActive());
            ZxvRefusedException again = assertThrows(ZxvRefusedException.class, () -> shop.redeem(v, bob));
            assertEquals(ZxvStatus.E_STATE, again.status());
            shop.check();
        }
    }

    @Test
    void voucherListsGrowPastAnyFirstBufferAndEmptyListsWork() {
        try (ZxvContext ctx = ZxvContext.create(); Capability cap = ctx.rootCapability(Permission.VINO);
                PlaySandbox sb = PlaySandbox.create(cap)) {
            int issuer = sb.openIssuer(1), holder = sb.openHolder(2), empty = sb.openHolder(3);
            sb.back(issuer, 1000);
            List<Long> issued = new ArrayList<>();
            for (int i = 0; i < 300; i++) issued.add(sb.issue(issuer, holder, 1, i % 2 == 0 ? Rail.BEARER : Rail.GHOST));
            assertEquals(issued, sb.vouchers(holder));
            assertEquals(issued, sb.vouchers(issuer));
            assertEquals(List.of(), sb.vouchers(empty));
            assertEquals(300, sb.account(holder).activeVouchers());
            sb.redeem(issued.get(7), holder);
            assertEquals(299, sb.vouchers(holder).size());
            assertFalse(sb.vouchers(holder).contains(issued.get(7)));
            assertThrows(UnsupportedOperationException.class, () -> sb.vouchers(holder).add(1L));
            ZxvUsageException nf = assertThrows(ZxvUsageException.class, () -> sb.vouchers(99));
            assertEquals(ZxvStatus.E_NOT_FOUND, nf.status());

            // The C side of the two-call pattern: a short buffer is ZXV_E_BUFFER with the count, ids untouched.
            try (Lease l = Lease.of(Access.core().resource(cap)); Arena a = Arena.ofConfined()) {
                MemorySegment p = rawHandle(sb);
                MemorySegment ids = a.allocate(ValueLayout.JAVA_LONG, 4), n = a.allocate(ValueLayout.JAVA_LONG);
                ids.fill((byte) 0x5a);
                assertEquals(Ffi.E_BUFFER, Ffi.call(Native16.vino_vouchers, l.h(0), p, holder, ids, 4L, n));
                assertEquals(299, n.get(ValueLayout.JAVA_LONG, 0));
                assertEquals(0x5a5a5a5a5a5a5a5aL, ids.getAtIndex(ValueLayout.JAVA_LONG, 0));
                assertEquals(0, Ffi.call(Native16.vino_vouchers, l.h(0), p, empty, MemorySegment.NULL, 0L, n));
                assertEquals(0, n.get(ValueLayout.JAVA_LONG, 0));
            }
        }
    }

    /** The sandbox's native handle, read reflectively (tests only). */
    private static MemorySegment rawHandle(PlaySandbox sb) {
        try {
            var f = PlaySandbox.class.getDeclaredField("res");
            f.setAccessible(true);
            return ((org.zedec.zxv.internal.NativeResource) f.get(sb)).rawHandle();
        } catch (ReflectiveOperationException e) {
            throw new AssertionError(e);
        }
    }

    @Test
    void useAfterCloseIsRefusedAndClosingTheContextClosesItsHandles() {
        PlaySandbox sb;
        SpendMeter m;
        try (ZxvContext ctx = ZxvContext.create(); Capability cap = ctx.rootCapability(Permission.VINO)) {
            sb = PlaySandbox.create(cap);
            int h = sb.openHolder(1);
            sb.close();
            sb.close(); // idempotent
            assertTrue(sb.isClosed());
            assertThrows(IllegalStateException.class, () -> sb.account(h));
            assertThrows(IllegalStateException.class, () -> sb.vouchers(h));

            m = SpendMeter.create(cap, 10);
            assertEquals(9, m.admit(1, 1));
            PlaySandbox live = PlaySandbox.create(cap);
            ctx.close();
            assertTrue(m.isClosed() && live.isClosed(), "the context closes what was made on it");
            assertThrows(IllegalStateException.class, () -> m.admit(1, 1));
            assertThrows(IllegalStateException.class, () -> live.openHolder(1));
        }
    }

    @Test
    void aHaltedContextPoisonsThePlaySandboxAndTheMeter() {
        try (ZxvContext ctx = ZxvContext.create(); Capability cap = ctx.rootCapability(Permission.VINO);
                PlaySandbox sb = PlaySandbox.create(cap); SpendMeter m = SpendMeter.create(cap, 10)) {
            int issuer = sb.openIssuer(1), holder = sb.openHolder(2);
            sb.back(issuer, 100);
            ctx.halt("test");
            ZxvFatalError e = assertThrows(ZxvFatalError.class, () -> sb.issue(issuer, holder, 1, Rail.BEARER));
            assertEquals(ZxvStatus.E_POISONED, e.status());
            assertTrue(ctx.isPoisoned());
            assertEquals(ZxvStatus.E_POISONED, assertThrows(ZxvFatalError.class, () -> sb.account(issuer)).status());
            assertEquals(ZxvStatus.E_POISONED, assertThrows(ZxvFatalError.class, () -> m.admit(1, 1)).status());
            assertEquals(ZxvStatus.E_POISONED, assertThrows(ZxvFatalError.class, () -> PlaySandbox.create(cap)).status());
            // libzxv itself refuses too, not only the SDK's check.
            MemorySegment capH = Access.core().resource(cap).rawHandle();
            try (Arena a = Arena.ofConfined()) {
                assertEquals(ZxvStatus.E_POISONED, Ffi.call(Native16.vino_issue, capH, rawHandle(sb), issuer, holder,
                        1L, 0, a.allocate(ValueLayout.JAVA_LONG)));
                assertEquals(ZxvStatus.E_POISONED,
                        Ffi.call(Native16.vino_play_create, capH, a.allocate(ValueLayout.ADDRESS)));
            }
            // The pure calls need no context and still work.
            assertEquals(Vino.REQUEST_BYTES, PaymentRequest.play(MERCHANT, NONCE, 1, 0, 1, "").encode().length);
        }
    }

    @Test
    void requestsAreValuesAndRefusalsAreTyped() {
        PaymentRequest r = PaymentRequest.live(MERCHANT, NONCE, 1_250, 1000, 1600, "Sword of Dawn");
        byte[] wire = r.encode();
        assertEquals(Vino.REQUEST_BYTES, wire.length);
        assertEquals(r, PaymentRequest.decode(wire));
        assertEquals(r.hashCode(), PaymentRequest.decode(wire).hashCode());
        assertArrayEquals(r.id(), PaymentRequest.idOf(wire));
        // Arrays are copied: changing the caller's or the returned array changes nothing.
        byte[] m = MERCHANT.clone();
        PaymentRequest copy = PaymentRequest.live(m, NONCE, 1_250, 1000, 1600, "Sword of Dawn");
        m[0] ^= 1;
        copy.merchant()[1] ^= 1;
        assertEquals(r, copy);
        assertNotEquals(r, PaymentRequest.live(m, NONCE, 1_250, 1000, 1600, "Sword of Dawn"));
        // Unsigned 64-bit amounts and times survive exactly.
        PaymentRequest big = PaymentRequest.play(MERCHANT, NONCE, Vino.AMOUNT_MAX, 0, -1L, null);
        assertEquals(-1L, PaymentRequest.decode(big.encode()).expiresAt());
        assertEquals("", big.memo());

        assertThrows(ChanceException.class,
                () -> new PaymentRequest(Lane.LIVE, PaymentRequest.FLAG_CHANCE, MERCHANT, NONCE, 1, 0, 1, "").encode());
        assertTrue(new PaymentRequest(Lane.PLAY, PaymentRequest.FLAG_CHANCE, MERCHANT, NONCE, 1, 0, 1, "").isPaidChance());
        new PaymentRequest(Lane.PLAY, PaymentRequest.FLAG_CHANCE, MERCHANT, NONCE, 1, 0, 1, "").encode();
        assertThrows(LaneException.class, () -> Lane.fromCode(0));
        ZxvUsageException nul = assertThrows(ZxvUsageException.class,
                () -> PaymentRequest.live(MERCHANT, NONCE, 1, 0, 1, "a\0b").encode());
        assertEquals(ZxvStatus.E_FORMAT, nul.status());
        assertEquals(ZxvStatus.E_FORMAT, assertThrows(ZxvUsageException.class,
                () -> PaymentRequest.live(MERCHANT, NONCE, 1, 0, 1, "café").encode()).status());
        assertEquals(ZxvStatus.E_ARG, assertThrows(ZxvUsageException.class,
                () -> PaymentRequest.live(new byte[31], NONCE, 1, 0, 1, "")).status());
        assertEquals(ZxvStatus.E_ARG, assertThrows(ZxvUsageException.class,
                () -> PaymentRequest.decode(new byte[159])).status());
    }

    @Test
    void receiptsBindRequestWalletAndTimeAndTheKeyIsNeverCopied() {
        byte[] req = PaymentRequest.live(MERCHANT, NONCE, 99, 1000, 1600, "gem").encode();
        try (ZxvContext ctx = ZxvContext.create(); Capability cap = ctx.rootCapability(Permission.CRYPTO_SIGN);
                MlDsa87.KeyPair wallet = MlDsa87.with(cap).generateKeyPair(V15.fill(1, 32));
                MlDsa87.KeyPair other = MlDsa87.with(cap).generateKeyPair(V15.fill(2, 32))) {
            Receipt r = Receipt.sign(cap, req, wallet.secretKey(), wallet.publicKey(), 1200);
            assertTrue(r.verify(1300));
            Receipt again = Receipt.sign(cap, req, wallet.secretKey(), wallet.publicKey(), 1200);
            assertNotEquals(r, again, "hedged signing: a fresh signature each time");
            assertTrue(again.verify(1300));
            assertFalse(new Receipt(req, other.publicKey(), 1200, r.signature()).verify(1300), "another wallet's key");
            assertFalse(new Receipt(req, wallet.publicKey(), 1201, r.signature()).verify(1300), "another time");
            assertThrows(ExpiredException.class, () -> r.verify(1601));
            assertThrows(ExpiredException.class,
                    () -> Receipt.sign(cap, req, wallet.secretKey(), wallet.publicKey(), 1601));
            assertThrows(ExpiredException.class,
                    () -> Receipt.sign(cap, req, wallet.secretKey(), wallet.publicKey(), 999));
            assertEquals(PaymentRequest.decode(req), r.paymentRequest());
            assertArrayEquals(Receipt.message(r.requestId(), wallet.publicKey(), 1200),
                    Receipt.message(PaymentRequest.idOf(req), wallet.publicKey(), 1200));
            // A key of the wrong kind is refused, and a closed key cannot be used.
            try (SecretKey ed = SecretKey.copyOf(SecretKey.Kind.ED25519, new byte[32])) {
                assertThrows(ZxvUsageException.class, () -> Receipt.sign(cap, req, ed, wallet.publicKey(), 1200));
            }
            wallet.secretKey().close();
            assertThrows(IllegalStateException.class,
                    () -> Receipt.sign(cap, req, wallet.secretKey(), wallet.publicKey(), 1200));
        }
    }

    @Test
    void theMeterReportsWhatIsLeftAndIsALimitException() {
        try (ZxvContext ctx = ZxvContext.create(); Capability cap = ctx.rootCapability(Permission.VINO);
                SpendMeter m = SpendMeter.create(cap, 100)) {
            assertEquals(70, m.admit(19_000, 30));
            DailyCapException e = assertThrows(DailyCapException.class, () -> m.admit(19_000, 71));
            assertInstanceOf(LimitException.class, e);
            assertEquals(StatusClass.REFUSED, e.statusClass());
            assertEquals(70, e.remainingToday());
            assertEquals(0, m.admit(19_000, 70), "the refused amount was not counted");
            assertEquals(ZxvStatus.E_RANGE, assertThrows(ZxvUsageException.class, () -> m.admit(18_999, 1)).status());
            assertEquals(99, m.admit(19_001, 1));
            assertEquals(ZxvStatus.E_RANGE,
                    assertThrows(ZxvUsageException.class, () -> SpendMeter.create(cap, 0)).status());
        }
    }
}
