// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.List;
import java.util.Map;
import org.junit.jupiter.api.Test;
import org.zedec.zxv.aipi.AipiNode;
import org.zedec.zxv.aipi.CallRecord;
import org.zedec.zxv.aipi.ClaimRecord;
import org.zedec.zxv.aipi.Determinism;
import org.zedec.zxv.aipi.FunctionRecord;
import org.zedec.zxv.aipi.Intent;
import org.zedec.zxv.aipi.Privacy;
import org.zedec.zxv.aipi.Route;
import org.zedec.zxv.aipi.VerifyMode;
import org.zedec.zxv.cid.Cid;
import org.zedec.zxv.cid.CidBase;
import org.zedec.zxv.cid.CidException;

/** ABI 1.5 CID and AIPI vectors, plus the offline AIPI node. */
class CidAipiTest {
    private static final Map<String, Object> C = V15.sec("cid");
    private static final Map<String, Object> A = V15.sec("aipi");

    @Test
    void computeAndText() {
        for (Object o : V15.l(C.get("compute"))) {
            Map<String, Object> c = V15.m(o);
            Cid cid = Cid.compute(V15.u64(c.get("codec")), V15.utf8(c.get("utf8")));
            assertEquals(c.get("cid"), cid.toHex());
            assertEquals(c.get("text_default"), cid.toString());
            assertEquals(c.get("text_58btc"), cid.toText(CidBase.BASE58BTC));
            assertEquals(V15.i(c.get("version")), cid.version());
            assertEquals(V15.i(c.get("hash")), cid.multihash());
            assertEquals(V15.u64(c.get("codec")), cid.codec());
            assertEquals(c.get("digest"), V15.hex(cid.digest()));
            assertTrue(cid.matches(V15.utf8(c.get("utf8"))));
        }
    }

    @Test
    void unixfs() {
        for (Object o : V15.l(C.get("unixfs"))) {
            Map<String, Object> c = V15.m(o);
            byte[] data = c.containsKey("ramp") ? V15.ramp(V15.i(c.get("len"))) : V15.utf8(c.get("utf8"));
            Cid cid = Cid.unixfs(data);
            assertEquals(c.get("cid"), cid.toHex());
            assertEquals(c.get("text_default"), cid.toString());
            if (c.containsKey("codec")) assertEquals(V15.u64(c.get("codec")), cid.codec());
        }
    }

    @Test
    void parseText() {
        for (Object o : V15.l(C.get("text"))) {
            Map<String, Object> c = V15.m(o);
            Cid cid = Cid.parse((String) c.get("text"));
            assertEquals(c.get("cid"), cid.toHex());
            assertEquals(c.get("text_default"), cid.toString());
            if ("ZXV_E_CID".equals(c.get("text_58btc"))) {
                assertThrows(CidException.class, () -> cid.toText(CidBase.BASE58BTC));
            } else {
                assertEquals(c.get("text_58btc"), cid.toText(CidBase.BASE58BTC));
            }
            assertEquals(V15.i(c.get("version")), cid.version());
            assertEquals(V15.u64(c.get("codec")), cid.codec());
            assertEquals(c.get("digest"), V15.hex(cid.digest()));
            assertEquals(c.get("v1"), cid.toV1().toHex());
            assertEquals(c.get("v1_text"), cid.toV1().toString());
            assertEquals(c.get("provider_key"), V15.hex(cid.providerKey()));
            assertEquals(cid, Cid.of(cid.bytes()));
        }
        for (Object o : V15.l(C.get("text_reject"))) {
            Map<String, Object> c = V15.m(o);
            V15.expect(c.get("status"), () -> Cid.parse((String) c.get("text")));
        }
    }

    @Test
    void verify() {
        for (Object o : V15.l(C.get("verify"))) {
            Map<String, Object> c = V15.m(o);
            V15.expectRun(c.get("status"), () -> Cid.of(V15.hex(c.get("cid"))).verify(V15.utf8(c.get("utf8"))));
        }
        Cid abc = Cid.raw(V15.utf8("abc"));
        assertFalse(abc.matches(V15.utf8("abd")));
    }

    private static Cid moduleCid() {
        Cid m = Cid.raw(V15.utf8(A.get("module_utf8")));
        assertEquals(A.get("module_cid"), m.toHex());
        return m;
    }

    @Test
    void functionRecords() {
        Cid module = moduleCid();
        Cid contract = Cid.raw(V15.utf8(A.get("contract_utf8")));
        assertEquals(A.get("contract_cid"), contract.toHex());
        for (Object o : V15.l(A.get("fn"))) {
            Map<String, Object> c = V15.m(o);
            FunctionRecord f = V15.expect(c.get("status"), () -> FunctionRecord.encode(
                    Intent.fromCode(V15.i(c.get("intent"))), VerifyMode.fromCode(V15.i(c.get("verify"))),
                    Privacy.fromCode(V15.i(c.get("privacy"))), Determinism.fromCode(V15.i(c.get("determinism"))),
                    V15.i(c.get("quorum_n")), V15.i(c.get("quorum_k")), V15.u64(c.get("fuel_max")), module,
                    (Boolean) c.get("contract") ? contract : null, (String) c.get("entry"), (String) c.get("checker"),
                    (String) c.get("certifier")));
            if (f == null) continue;
            assertEquals(c.get("record"), V15.hex(f.bytes()));
            assertEquals(c.get("fn_cid"), f.cid().toHex());
            assertEquals(c.get("fn_cid_text"), f.cid().toText(CidBase.BASE58BTC));
            FunctionRecord d = FunctionRecord.decode(f.bytes());
            assertEquals(f, d);
            assertEquals(c.get("entry"), d.entry());
            assertEquals(V15.u64(c.get("fuel_max")), d.fuelMax());
            assertEquals((Boolean) c.get("contract"), d.contract().isPresent());
        }
        FunctionRecord b = FunctionRecord.builder().module(module).entry("main").fuelMax(1_000_000)
                .verifyByChecker("check").build();
        assertEquals(V15.m(V15.l(A.get("fn")).get(0)).get("record"), V15.hex(b.bytes()));
        assertEquals(ZxvStatus.E_FORMAT, V15.status(assertThrows(ZxvException.class,
                () -> FunctionRecord.decode(new byte[] {1, 2, 3}))));
    }

    @Test
    void callAndClaimRecords() {
        Cid module = moduleCid();
        Map<String, Object> f0 = V15.m(V15.l(A.get("fn")).get(0));
        Cid fn = Cid.of(V15.hex(f0.get("fn_cid")));
        List<Object> calls = V15.l(A.get("call"));
        for (Object o : calls) {
            Map<String, Object> c = V15.m(o);
            Cid target = "module_cid".equals(c.get("fn")) ? module : fn;
            byte[] input = V15.utf8(c.get("input_utf8"));
            CallRecord r = V15.expect(c.getOrDefault("status", "ZXV_OK"), () -> CallRecord.encode(target, input));
            if (r == null) {
                assertThrows(CidException.class, () -> CallRecord.cidOf(target, input));
                continue;
            }
            assertEquals(c.get("record"), V15.hex(r.bytes()));
            assertEquals(c.get("call_cid"), r.cid().toHex());
            assertEquals(c.get("call_cid_text"), r.cid().toText(CidBase.BASE58BTC));
            assertEquals(r.cid(), CallRecord.cidOf(fn, input));
            CallRecord d = CallRecord.decode(r.bytes());
            assertEquals(fn, d.function());
            assertArrayEquals(input, d.input());
        }
        for (Object o : V15.l(A.get("claim"))) {
            Map<String, Object> c = V15.m(o);
            Cid call = Cid.of(V15.hex(V15.m(calls.get(1)).get("call_cid")));
            ClaimRecord r = ClaimRecord.encode(call, V15.hex(c.get("executor")), V15.utf8(c.get("output_utf8")),
                    V15.utf8(c.get("cert_utf8")));
            assertEquals(c.get("record"), V15.hex(r.bytes()));
            ClaimRecord d = ClaimRecord.decode(r.bytes());
            assertEquals(call, d.call());
            assertArrayEquals(V15.hex(c.get("executor")), d.executor());
            assertArrayEquals(V15.utf8(c.get("output_utf8")), d.output());
            assertArrayEquals(V15.utf8(c.get("cert_utf8")), d.certificate());
        }
    }

    @Test
    void offlineNode() {
        try (ZxvContext ctx = ZxvContext.create(); Capability cap = V15.root(ctx);
                AipiNode node = AipiNode.create(cap, 1 << 20, 64, V15.fill(0x5A, 32));
                AipiNode keyless = AipiNode.create(cap, AipiNode.STORE_MIN, 4, null)) {
            byte[] mod = V15.utf8(A.get("module_utf8"));
            Cid m = node.put(mod);
            assertEquals(A.get("module_cid"), m.toHex());
            assertEquals(m, node.put(mod), "putting the same bytes again is a no-op");
            assertArrayEquals(mod, node.get(m));

            // Buffer growth: the first GET buffer is 4096 bytes, this block is larger.
            byte[] big = V15.ramp(10_000);
            Cid bc = node.put(big);
            assertArrayEquals(big, node.get(bc));

            byte[] secret = V15.utf8("private notes");
            Cid sc = node.putPrivate(secret);
            assertArrayEquals(secret, node.get(sc));
            assertEquals(ZxvStatus.E_STATE, V15.status(assertThrows(ZxvException.class, () -> node.put(secret))));
            assertEquals(ZxvStatus.E_STATE, V15.status(assertThrows(ZxvException.class, () -> keyless.putPrivate(secret))));
            assertEquals(ZxvStatus.E_NOT_FOUND, V15.status(assertThrows(ZxvException.class,
                    () -> node.get(Cid.raw(V15.utf8("never stored"))))));

            FunctionRecord f = FunctionRecord.builder().module(m).entry("main").fuelMax(10).verifyLocal().build();
            Cid fc = node.publish(f);
            assertEquals(f.cid(), fc);
            assertEquals(f, node.loadFunction(fc));
            assertEquals(Route.LOCAL, node.plan(fc, V15.utf8("x"), false));
            FunctionRecord orphan = FunctionRecord.builder().module(Cid.raw(V15.utf8("elsewhere"))).entry("main")
                    .fuelMax(10).verifyLocal().build();
            Cid oc = node.publish(orphan);
            assertEquals(Route.UNAVAILABLE, node.plan(oc, null, false));

            AipiNode.Info info = node.info();
            assertEquals(6, info.blocks(), "four public blocks; a private block takes two entries (plaintext and sealed CID)");
            assertTrue(info.storeUsed() > 10_000);
            assertTrue(info.getsVerified() >= 4);
            assertEquals(0, info.forbidden());
        }
    }

    @Test
    void nodeNeedsAipiCapability() {
        try (ZxvContext ctx = ZxvContext.create(); Capability root = V15.root(ctx);
                Capability noAipi = V15.without(root, Permission.AIPI)) {
            assertThrows(CapabilityDeniedException.class, () -> AipiNode.create(noAipi, 1 << 16, 8, null));
            try (AipiNode node = AipiNode.create(root, 1 << 16, 8, null)) {
                assertEquals(ZxvStatus.E_CAP, V15.status(assertThrows(ZxvException.class,
                        () -> AipiNode.create(noAipi, 1 << 16, 8, null))));
                assertTrue(node.info().blocks() == 0);
            }
        }
    }
}
