// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import org.junit.jupiter.api.Test;
import org.zedec.zxv.crypto.Hashes;
import org.zedec.zxv.crypto.MlDsa87;
import org.zedec.zxv.internal.Access;
import org.zedec.zxv.internal.Errors;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Lease;
import org.zedec.zxv.internal.Native16;
import org.zedec.zxv.vino.DailyCapException;
import org.zedec.zxv.vino.Lane;
import org.zedec.zxv.vino.PaymentRequest;
import org.zedec.zxv.vino.PlayAccount;
import org.zedec.zxv.vino.PlaySandbox;
import org.zedec.zxv.vino.Rail;
import org.zedec.zxv.vino.Receipt;
import org.zedec.zxv.vino.SpendMeter;
import org.zedec.zxv.vino.Voucher;
import org.zedec.zxv.vino.VoucherState;

/** Replays every {@code status_abi16} and {@code vino} vector of sdk/conformance/vectors.json. */
class VinoVectorsTest {
    private static Map<String, Object> vino() {
        return V15.sec("vino");
    }

    private static PaymentRequest request(Map<String, Object> r) {
        return new PaymentRequest(Lane.fromCode(V15.i(r.get("lane"))), V15.i(r.get("flags")), V15.hex(r.get("merchant")),
                V15.hex(r.get("nonce")), V15.u64(r.get("amount")), V15.u64(r.get("created_at")),
                V15.u64(r.get("expires_at")), (String) r.get("memo"));
    }

    private static byte[] firstRequestBytes() {
        return V15.hex(V15.m(V15.l(vino().get("requests")).get(0)).get("bytes"));
    }

    @Test
    void statusAbi16NamesAndClasses() {
        List<Object> all = V15.l(Vectors.root().get("status_abi16"));
        assertEquals(5, all.size());
        for (Object o : all) {
            Map<String, Object> s = V15.m(o);
            int code = V15.i(s.get("code"));
            assertEquals(s.get("name"), Errors.name(code));
            assertEquals(StatusClass.fromCode(V15.i(s.get("class"))), Errors.statusClass(code));
            assertEquals(s.get("name"), ((ZxvException) Errors.raise(code, "probe", null)).statusName());
        }
    }

    @Test
    void requestsEncodeToTheExactBytesAndIdOrTheExactRefusal() {
        int ok = 0;
        for (Object o : V15.l(vino().get("requests"))) {
            Map<String, Object> r = V15.m(o);
            int lane = V15.i(r.get("lane"));
            if (lane != 1 && lane != 2) {
                // Not a Lane: the Java type refuses it before encoding, with the same ZXV_E_LANE...
                V15.expect(r.get("status"), () -> request(r));
                // ...and so does libzxv when handed the raw lane.
                assertEquals(V15.code((String) r.get("status")), rawEncode(r));
                continue;
            }
            PaymentRequest req = request(r);
            byte[] bytes = V15.expect(r.get("status"), req::encode);
            if (bytes == null) continue;
            ok++;
            assertEquals(r.get("bytes"), V15.hex(bytes));
            assertEquals(r.get("id"), V15.hex(req.id()));
            assertEquals(r.get("id"), V15.hex(PaymentRequest.idOf(bytes)));
            PaymentRequest back = PaymentRequest.decode(bytes);
            assertEquals(req, back, "decode(encode(r)) == r");
            assertEquals(Long.toUnsignedString(back.amount()), r.get("amount"));
            assertEquals(Long.toUnsignedString(back.expiresAt()), r.get("expires_at"));
        }
        assertEquals(6, ok);
    }

    /** {@code zxv_vino_request_encode} with the vector's raw lane (one the {@link Lane} enum cannot hold). */
    private static int rawEncode(Map<String, Object> r) {
        try (Arena a = Arena.ofConfined()) {
            return Ffi.call(Native16.vino_request_encode, V15.i(r.get("lane")), V15.i(r.get("flags")),
                    a.allocateFrom(ValueLayout.JAVA_BYTE, V15.hex(r.get("merchant"))),
                    a.allocateFrom(ValueLayout.JAVA_BYTE, V15.hex(r.get("nonce"))), V15.u64(r.get("amount")),
                    V15.u64(r.get("created_at")), V15.u64(r.get("expires_at")), a.allocateFrom((String) r.get("memo")),
                    a.allocate(160), MemorySegment.NULL);
        }
    }

    @Test
    void decodeRefusesEachChangedByteWithItsStatus() {
        byte[] good = firstRequestBytes();
        assertNotNull(PaymentRequest.decode(good));
        for (Object o : V15.l(vino().get("decode"))) {
            Map<String, Object> d = V15.m(o);
            byte[] b = good.clone();
            b[V15.i(d.get("offset"))] = (byte) V15.i(d.get("value"));
            String want = (String) d.get("status");
            assertFalse(want.equals("ZXV_OK"));
            V15.expect(want, () -> PaymentRequest.decode(b));
            V15.expect(want, () -> PaymentRequest.idOf(b));
        }
    }

    @Test
    void receiptMessageSignatureAndEveryCheck() {
        Map<String, Object> rc = V15.m(vino().get("receipt"));
        byte[] req = firstRequestBytes();
        long approvedAt = V15.u64(rc.get("approved_at"));
        try (ZxvContext ctx = ZxvContext.create(); Capability cap = ctx.rootCapability(Permission.CRYPTO_SIGN);
                MlDsa87.KeyPair kp = MlDsa87.with(cap).generateKeyPair(V15.hex(rc.get("seed")))) {
            assertEquals(rc.get("pk_sha3_256"), V15.hex(Hashes.sha3_256(kp.publicKey())));
            byte[] msg = Receipt.message(PaymentRequest.idOf(req), kp.publicKey(), approvedAt);
            assertEquals(rc.get("msg"), V15.hex(msg));

            // The vector's signature is ML-DSA-87 deterministic signing over msg; it is exactly that.
            assertArrayEquals(V15.hex(rc.get("sig")), MlDsa87.with(cap).signDeterministic(kp.secretKey(), msg, null));
            // Receipt.sign is hedged: a different signature, valid all the same.
            Receipt signed = Receipt.sign(cap, req, kp.secretKey(), kp.publicKey(), approvedAt);
            assertTrue(signed.verify(approvedAt));
            assertTrue(MlDsa87.with(cap).verify(kp.publicKey(), msg, null, signed.signature()));
            assertEquals(V15.m(V15.l(vino().get("requests")).get(0)).get("id"), V15.hex(signed.requestId()));

            byte[] sig = V15.hex(rc.get("sig"));
            for (Object o : V15.l(rc.get("checks"))) {
                Map<String, Object> c = V15.m(o);
                byte[] s = sig.clone();
                int flip = V15.i(c.get("flip_sig"));
                if (flip >= 0) s[flip] ^= 0x01;
                Receipt r = new Receipt(req, kp.publicKey(), V15.u64(c.get("approved_at")), s);
                long now = V15.u64(c.get("now"));
                String want = (String) c.get("status");
                if (want.equals("ZXV_E_VERIFY")) {
                    assertFalse(r.verify(now), "a bad signature is false, not an exception");
                    assertThrows(VerificationFailedException.class, () -> r.requireValid(now));
                } else {
                    V15.expect(want, () -> r.verify(now));
                    V15.expectRun(want, () -> r.requireValid(now));
                }
            }
        }
    }

    @Test
    void playScenarioStepsAccountsAndVouchers() {
        Map<String, Object> play = V15.m(vino().get("play"));
        try (ZxvContext ctx = ZxvContext.create(); Capability cap = ctx.rootCapability(Permission.VINO);
                PlaySandbox sb = PlaySandbox.create(cap)) {
            for (Object o : V15.l(play.get("steps"))) {
                Map<String, Object> s = V15.m(o);
                Object st = s.get("status");
                switch ((String) s.get("op")) {
                    case "open" -> {
                        int kind = V15.i(s.get("is_issuer"));
                        if (kind != 0 && kind != 1) {
                            // Not expressible through the boolean Java API; check libzxv itself.
                            assertEquals(V15.code((String) st), rawOpen(cap, V15.i(s.get("owner")), kind));
                            continue;
                        }
                        Integer acct = V15.expect(st, () -> sb.open(V15.i(s.get("owner")), kind == 1));
                        if (acct != null) assertEquals(V15.i(s.get("account")), acct);
                    }
                    case "back" -> V15.expectRun(st, () -> sb.back(V15.i(s.get("issuer")), V15.u64(s.get("amount"))));
                    case "unback" -> V15.expectRun(st, () -> sb.unback(V15.i(s.get("issuer")), V15.u64(s.get("amount"))));
                    case "issue" -> {
                        Long id = V15.expect(st, () -> sb.issue(V15.i(s.get("issuer")), V15.i(s.get("holder")),
                                V15.u64(s.get("face")), Rail.fromCode(V15.i(s.get("rail")))));
                        if (id != null) assertEquals(V15.u64(s.get("id")), id);
                    }
                    case "transfer" -> V15.expectRun(st, () -> sb.transfer(V15.u64(s.get("voucher")),
                            V15.i(s.get("from")), V15.i(s.get("to")), Rail.fromCode(V15.i(s.get("rail")))));
                    case "set_rail" -> V15.expectRun(st, () -> sb.setRail(V15.u64(s.get("voucher")),
                            V15.i(s.get("holder")), Rail.fromCode(V15.i(s.get("rail")))));
                    case "redeem" -> V15.expectRun(st, () -> sb.redeem(V15.u64(s.get("voucher")), V15.i(s.get("holder"))));
                    default -> throw new AssertionError("unknown op " + s.get("op"));
                }
            }
            sb.check();
            for (Object o : V15.l(play.get("accounts"))) {
                Map<String, Object> a = V15.m(o);
                int n = V15.i(a.get("account"));
                PlayAccount got = sb.account(n);
                assertEquals(new PlayAccount(n, V15.i(a.get("owner")), V15.i(a.get("is_issuer")) == 1,
                        V15.u64(a.get("backing")), V15.u64(a.get("faces")), V15.i(a.get("count"))), got);
                assertTrue(got.isCovered());
                List<Long> want = new ArrayList<>();
                for (Object id : V15.l(a.get("vouchers"))) want.add(V15.u64(id));
                assertEquals(want, sb.vouchers(n));
            }
            for (Object o : V15.l(play.get("vouchers"))) {
                Map<String, Object> v = V15.m(o);
                long id = V15.u64(v.get("id"));
                assertEquals(new Voucher(id, V15.u64(v.get("face")), V15.i(v.get("issuer")), V15.i(v.get("holder")),
                        Rail.fromCode(V15.i(v.get("rail"))), VoucherState.fromCode(V15.i(v.get("state")))),
                        sb.voucher(id));
            }
        }
    }

    /** {@code zxv_vino_play_open} with an {@code is_issuer} the Java API cannot express, on a scratch sandbox. */
    private static int rawOpen(Capability cap, int owner, int kind) {
        try (Lease l = Lease.of(Access.core().resource(cap)); Arena a = Arena.ofConfined()) {
            MemorySegment out = a.allocate(ValueLayout.ADDRESS);
            Ffi.check(Ffi.call(Native16.vino_play_create, l.h(0), out), null);
            MemorySegment p = out.get(ValueLayout.ADDRESS, 0);
            try {
                return Ffi.call(Native16.vino_play_open, l.h(0), p, owner, kind, a.allocate(ValueLayout.JAVA_INT));
            } finally {
                Ffi.callVoid(Native16.vino_play_destroy, p);
            }
        }
    }

    @Test
    void meterScenario() {
        Map<String, Object> m = V15.m(vino().get("meter"));
        long cap0 = V15.u64(m.get("daily_cap"));
        try (ZxvContext ctx = ZxvContext.create(); Capability cap = ctx.rootCapability(Permission.VINO);
                SpendMeter meter = SpendMeter.create(cap, cap0)) {
            assertEquals(cap0, meter.dailyCap());
            for (Object o : V15.l(m.get("steps"))) {
                Map<String, Object> s = V15.m(o);
                long day = V15.u64(s.get("day")), amount = V15.u64(s.get("amount"));
                String want = (String) s.get("status");
                if (want.equals("ZXV_OK")) {
                    assertEquals(V15.u64(s.get("left")), meter.admit(day, amount));
                } else if (want.equals("ZXV_E_LIMIT")) {
                    DailyCapException e = assertThrows(DailyCapException.class, () -> meter.admit(day, amount));
                    assertEquals(ZxvStatus.E_LIMIT, e.status());
                    assertEquals(V15.u64(s.get("left")), e.remainingToday());
                } else {
                    V15.expect(want, () -> meter.admit(day, amount));
                }
            }
        }
    }
}
