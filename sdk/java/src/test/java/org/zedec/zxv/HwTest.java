// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.EnumSet;
import java.util.List;
import java.util.Set;
import java.util.concurrent.atomic.AtomicInteger;
import org.junit.jupiter.api.Test;
import org.zedec.zxv.crypto.HwPackageSigner;
import org.zedec.zxv.hw.HwAuditRecord;
import org.zedec.zxv.hw.HwCapKey;
import org.zedec.zxv.hw.HwClass;
import org.zedec.zxv.hw.HwEvent;
import org.zedec.zxv.hw.HwFunction;
import org.zedec.zxv.hw.HwKind;
import org.zedec.zxv.hw.HwMatrix;
import org.zedec.zxv.hw.HwProfile;
import org.zedec.zxv.hw.HwRegion;
import org.zedec.zxv.hw.HwVector;

/** ABI 1.4: ZHP-1 profiles, the emulation matrix and hardware updates, mirroring test_zxv_abi14.c. */
class HwTest {
    private static Capability root(ZxvContext ctx) {
        return ctx.rootCapability(Permission.all());
    }

    /** A capability without the 1.4 bits (everything ZXV_CAP_ALL holds). */
    private static Capability noHw(Capability cap) {
        Set<Permission> keep = EnumSet.allOf(Permission.class);
        keep.remove(Permission.HW_MATRIX);
        keep.remove(Permission.HW_RECONF);
        return cap.attenuate(keep);
    }

    private static int status(Throwable t) {
        return ((ZxvException) t).status();
    }

    @Test
    void profiles() {
        assertTrue(Zxv.hasFeature(Feature.HW));
        try (HwProfile host = HwProfile.probeHost(); HwProfile r1 = HwProfile.reference()) {
            assertEquals(16, r1.nodeCount(), "R1 has 16 nodes");
            assertEquals(16, r1.nodes().size());
            assertTrue(host.nodeCount() >= 2);
            assertEquals(64L, host.cap(2, HwCapKey.WORD_BITS).orElseThrow());
            assertTrue(host.cap(2, HwCapKey.COUNT).orElseThrow() >= 1);

            HwProfile.Match m = HwProfile.match(host, r1);
            assertTrue(m.nativeCount() >= 1 && m.nativeCount() + m.emulatedCount() == 14, m.toString());
            HwProfile.Pairing p = HwProfile.pair(host, r1, 1);
            assertEquals(2, p.targetId());
            assertEquals(HwProfile.PairKind.GROUP, p.kind());
            p = HwProfile.pair(host, r1, 2);
            assertEquals(new HwProfile.Pairing(3, HwProfile.PairKind.NATIVE, 2, p.unmetKey()), p);
            assertEquals(HwProfile.PairKind.EMULATE, HwProfile.pair(host, r1, 7).kind(), "no ternary unit here");
            assertEquals(-4, status(assertThrows(ZxvException.class, () -> HwProfile.pair(host, r1, 99))));
            assertEquals(HwFunction.TENSOR, (int) r1.cap(6, HwCapKey.CONTRACT_ID).orElseThrow());

            try (HwProfile pres = HwProfile.present(host, r1)) {
                assertEquals(HwKind.HARD.code(), pres.cap(3, HwCapKey.KIND).orElseThrow());
                assertEquals(HwKind.SOFT.code(), pres.cap(8, HwCapKey.KIND).orElseThrow());
                assertTrue(pres.cap(1, HwCapKey.KIND).isEmpty());
            }

            byte[] enc = r1.encode();
            try (HwProfile copy = HwProfile.decode(enc)) {
                assertArrayEquals(r1.digest(), copy.digest());
                assertEquals(32, copy.digest().length);
            }
            byte[] cut = java.util.Arrays.copyOf(enc, enc.length - 1);
            ZxvUsageException e = assertThrows(ZxvUsageException.class, () -> HwProfile.decode(cut));
            assertEquals("ZXV_E_FORMAT", e.statusName());

            try (HwProfile mine = HwProfile.create()) {
                mine.addNode(1, HwProfile.ROOT, HwClass.MACHINE);
                mine.addNode(2, 1, HwClass.TERNARY_UNIT);
                mine.setCap(2, HwCapKey.RADIX, 3);
                assertEquals(-1, status(assertThrows(ZxvException.class, () -> mine.addNode(2, 1, HwClass.CACHE))));
                assertEquals(-4, status(assertThrows(ZxvException.class, () -> mine.setCap(9, HwCapKey.RADIX, 3))));
                mine.addNode(3, 1, HwClass.PRIVATE | 1);
                HwProfile.Pairing t = HwProfile.pair(mine, r1, 7);
                assertEquals(HwProfile.PairKind.NATIVE, t.kind());
                assertEquals(2, t.hostId());
                assertEquals(new HwProfile.Node(3, 1, HwClass.PRIVATE | 1, 0), mine.node(2));

                byte[] d1 = mine.digest();
                assertArrayEquals(new byte[0], mine.extension(3));
                byte[] ext = {(byte) 0xA5, 0, 0x5A};
                mine.setExtension(3, ext);
                assertFalse(java.util.Arrays.equals(d1, mine.digest()), "the extension is part of the identity");
                assertArrayEquals(ext, mine.extension(3));
                assertEquals(-8, status(assertThrows(ZxvException.class,
                        () -> mine.setExtension(3, new byte[HwProfile.EXT_MAX + 1]))));
            }
        }
    }

    @Test
    void standardEnginesPolicyAndDiscovery() {
        try (ZxvContext ctx = ZxvContext.create(); Capability cap = root(ctx); Capability nohw = noHw(cap)) {
            assertThrows(CapabilityDeniedException.class, () -> HwMatrix.create(nohw));
            try (HwMatrix mx = HwMatrix.create(cap)) {
                byte[] sha = mx.call(HwFunction.SHA256, 0, "abc".getBytes(StandardCharsets.US_ASCII));
                assertEquals("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", Vectors.toHex(sha));

                byte[] macIn = new byte[34];
                macIn[0] = 4;
                System.arraycopy("Jefe".getBytes(StandardCharsets.US_ASCII), 0, macIn, 2, 4);
                System.arraycopy("what do ya want for nothing?".getBytes(StandardCharsets.US_ASCII), 0, macIn, 6, 28);
                assertEquals("5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843",
                        Vectors.toHex(mx.call(HwFunction.MAC, 0, macIn)), "HMAC RFC 4231 case 2");
                byte[] g = ByteBuffer.allocate(16).order(ByteOrder.LITTLE_ENDIAN).putLong(12).putLong(18).array();
                byte[] gcd = mx.call(HwFunction.GCD, 0, g);
                assertEquals(8, gcd.length);
                assertEquals(6L, ByteBuffer.wrap(gcd).order(ByteOrder.LITTLE_ENDIAN).getLong());

                List<HwMatrix.ProviderInfo> engines = mx.providers();
                assertTrue(engines.size() >= 5, "standard engines " + engines.size());
                int macPv = -1, gcdPv = -1;
                for (int i = 0; i < engines.size(); i++) {
                    HwMatrix.ProviderInfo pi = engines.get(i);
                    assertEquals(HwKind.SOFT, pi.kind());
                    assertEquals(Truth.TRUE, pi.proof());
                    assertEquals(1, pi.contract());
                    assertFalse(pi.flags().contains(HwMatrix.ProviderFlag.FAULT));
                    if (pi.functionId() == HwFunction.MAC) macPv = i;
                    if (pi.functionId() == HwFunction.GCD) gcdPv = i;
                }
                assertTrue(macPv >= 0 && gcdPv >= 0);
                final int count = engines.size();
                assertEquals(-4, status(assertThrows(ZxvException.class, () -> mx.providerInfo(count))));
                HwMatrix.FunctionInfo fi = mx.functionInfo(HwFunction.MAC);
                assertEquals(1, fi.contract());
                assertTrue(fi.policy().contains(HwMatrix.Policy.KEY_BEARING));
                assertTrue(fi.vectors() >= 1);

                // Key-bearing: a cheaper REMOTE implementation never serves it.
                AtomicInteger remoteHits = new AtomicInteger();
                mx.addResource(7);
                int prem = mx.addProvider(HwFunction.MAC, 7, HwKind.REMOTE, 1, (op, in) -> {
                    remoteHits.incrementAndGet();
                    throw new ZxvRefusedException(-9, "ZXV_E_STATE", "must never serve key-bearing work");
                });
                mx.attest(prem);
                assertEquals("5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843",
                        Vectors.toHex(mx.call(HwFunction.MAC, 0, macIn)));
                assertEquals(macPv, mx.bound(HwFunction.MAC), "MAC stays on the local engine");
                assertTrue(mx.providerInfo(prem).flags().contains(HwMatrix.ProviderFlag.POLICY));
                ZxvRefusedException pol = assertThrows(ZxvRefusedException.class,
                        () -> mx.setPolicy(HwFunction.MAC, Set.of(), 0, 0));
                assertEquals("ZXV_E_POLICY", pol.statusName());
                assertEquals(-31, status(assertThrows(ZxvException.class, () -> mx.memo(HwFunction.MAC, 16))));

                // A power budget: undeclared traits never qualify, declared ones do.
                mx.setPolicy(HwFunction.GCD, Set.of(), 0, 500);
                assertEquals(-25, status(assertThrows(ZxvRefusedException.class, () -> mx.call(HwFunction.GCD, 0, g))));
                final int gp = gcdPv;
                mx.setTraits(gp, 2000, 300, 4);
                assertEquals(-8, status(assertThrows(ZxvException.class, () -> mx.setTraits(gp, 2000, 300, 16))));
                assertEquals(6, mx.call(HwFunction.GCD, 0, g)[0]);
                HwMatrix.ProviderInfo gi = mx.providerInfo(gcdPv);
                assertEquals(4, gi.throughputClass());
                assertEquals(HwMatrix.Power.W_1, gi.power());
                mx.setPolicy(HwFunction.GCD, EnumSet.of(HwMatrix.Policy.LOCAL_ONLY), 1000, 0);
                assertEquals(-25, status(assertThrows(ZxvRefusedException.class, () -> mx.call(HwFunction.GCD, 0, g))));
                mx.setPolicy(HwFunction.GCD, Set.of(), 0, 0);
            }
        }
    }

    private static HwMatrix.Implementation addk(int k, int[] bad) {
        return (op, in) -> {
            if (bad[0] == 2) throw new ZxvSystemException(-23, "ZXV_E_IO", "simulated device error");
            if (bad[0] == 3) throw new IllegalStateException("a bug in the implementation");
            byte[] out = new byte[in.length];
            for (int i = 0; i < in.length; i++) out[i] = (byte) (in[i] + k + (bad[0] == 1 && in[i] == 9 ? 1 : 0));
            return out;
        };
    }

    @Test
    void managedImplementationsProofFailoverShadowMemo() {
        try (ZxvContext ctx = ZxvContext.create(); Capability cap = root(ctx); HwMatrix mx = HwMatrix.create(cap)) {
            mx.addFunction(0x100, "add8");
            mx.addFunction(0x101, "add16");
            assertEquals(-13, status(assertThrows(ZxvException.class, () -> mx.addFunction(0x100, "again"))));
            mx.addResource(1);
            mx.addResource(2);
            byte[] in = {0, 1, 9, (byte) 200}, ex8 = {8, 9, 17, (byte) 208}, ex16 = {16, 17, 25, (byte) 216};
            mx.addVector(0x100, new HwVector(0, in, ex8));
            mx.addVector(0x101, new HwVector(0, in, ex16));
            int[] fxBad = {0}, swBad = {0};
            int pfx = mx.addProvider(0x100, 2, HwKind.HARD, 1, addk(8, fxBad));
            int psw = mx.addProvider(0x100, 1, HwKind.SOFT, 10, addk(8, swBad));
            // add16 through two nested calls to add8 on the same matrix.
            int p16 = mx.addProvider(0x101, 1, HwKind.SOFT, 20, (op, x) -> mx.call(0x100, op, mx.call(0x100, op, x)));

            assertEquals(-25, status(assertThrows(ZxvException.class, () -> mx.call(0x100, 0, in))));
            assertEquals(Truth.TRUE, mx.attest(pfx));
            assertEquals(Truth.TRUE, mx.attest(psw));
            assertEquals(Truth.FALSE, mx.attest(p16), "an undeclared dependency must not prove");
            mx.addDependency(p16, 0x100);
            assertEquals(Truth.TRUE, mx.attest(p16));
            assertArrayEquals(ex16, mx.call(0x101, 0, in));

            assertEquals(pfx, mx.bound(0x100));
            mx.setResourceState(2, HwMatrix.ResourceState.FAULTED);
            assertArrayEquals(ex8, mx.call(0x100, 0, in));
            assertEquals(psw, mx.bound(0x100), "failover to the emulator");
            mx.setResourceState(2, HwMatrix.ResourceState.OK);

            // An implementation's status comes back as its own; a bug is rethrown as itself.
            fxBad[0] = 2;
            assertEquals(-23, status(assertThrows(ZxvSystemException.class, () -> mx.call(0x100, 0, in))));
            fxBad[0] = 3;
            IllegalStateException bug = assertThrows(IllegalStateException.class, () -> mx.call(0x100, 0, in));
            assertEquals("a bug in the implementation", bug.getMessage());
            fxBad[0] = 0;
            assertArrayEquals(ex8, mx.call(0x100, 0, in));

            // Shadow: agreement, then a silent fault: dispute, nobody serves.
            assertTrue(mx.shadow(0x100, 0, in).checked());
            swBad[0] = 1;
            assertEquals(-26, status(assertThrows(ZxvRefusedException.class, () -> mx.shadow(0x100, 0, in))));
            assertEquals(-25, status(assertThrows(ZxvException.class, () -> mx.call(0x100, 0, in))));
            assertEquals(Truth.TRUE, mx.attest(pfx));
            assertEquals(Truth.FALSE, mx.attest(psw));

            // Memo: a repeat skips the implementation.
            assertEquals(-1, status(assertThrows(ZxvException.class, () -> mx.memo(0x100, 6))));
            mx.memo(0x100, 16);
            long calls0 = mx.providerInfo(pfx).calls();
            mx.call(0x100, 0, in);
            assertArrayEquals(ex8, mx.call(0x100, 0, in));
            assertEquals(calls0 + 1, mx.providerInfo(pfx).calls());
            assertEquals(new HwMatrix.MemoStats(1, 1), mx.memo(0x100, 16));

            // Versioned contracts: a new version makes everyone prove again.
            HwMatrix.FunctionInfo fi = mx.functionInfo(0x100);
            assertEquals(new HwMatrix.FunctionInfo(0, Set.of(), 1), fi);
            mx.startContract(0x100, 2);
            assertEquals(-28, status(assertThrows(ZxvRefusedException.class, () -> mx.startContract(0x100, 2))));
            assertEquals(-25, status(assertThrows(ZxvException.class, () -> mx.call(0x100, 0, in))));
            mx.addVector(0x100, new HwVector(0, in, ex8));
            assertEquals(Truth.TRUE, mx.attest(pfx));
            assertEquals(2, mx.providerInfo(pfx).contract());
            assertTrue(mx.providerInfo(psw).flags().contains(HwMatrix.ProviderFlag.UNPROVEN));
            mx.setEnabled(pfx, false);
            assertTrue(mx.providerInfo(pfx).flags().contains(HwMatrix.ProviderFlag.DISABLED));
        }
    }

    /** A simulated two-slot device: the active slot is a 256-entry lookup table. */
    private static final class Sim implements HwRegion.Device {
        final byte[][] slot = new byte[2][256];
        final int[] len = new int[2];
        int active = -1;

        @Override
        public void load(int s, byte[] payload) {
            if (s > 1 || payload.length > 256) throw new ZxvUsageException(-8, "ZXV_E_RANGE", "slot");
            System.arraycopy(payload, 0, slot[s], 0, payload.length);
            len[s] = payload.length;
        }

        @Override
        public byte[] readback(int s) {
            try {
                MessageDigest md = MessageDigest.getInstance("SHA-256");
                md.update(slot[s], 0, len[s]);
                return md.digest();
            } catch (java.security.NoSuchAlgorithmException e) {
                throw new IllegalStateException(e);
            }
        }

        @Override
        public void activate(int s) {
            active = s;
        }

        byte[] run(byte[] in) {
            if (active < 0) throw new ZxvRefusedException(-9, "ZXV_E_STATE", "no image");
            byte[] out = new byte[in.length];
            for (int i = 0; i < in.length; i++) out[i] = slot[active][in[i] & 0xFF];
            return out;
        }
    }

    @Test
    void signedUpdatesRegionsAndContracts() {
        record Ev(int region, HwEvent event, int reason, int version) {}
        try (ZxvContext ctx = ZxvContext.create(); Capability cap = root(ctx); Capability nohw = noHw(cap);
             HwMatrix mx = HwMatrix.create(cap)) {
            HwPackageSigner signer = HwPackageSigner.with(cap);
            try (HwPackageSigner.KeyPair key = signer.generateKeyPair();
                 HwPackageSigner.KeyPair other = HwPackageSigner.with(nohw).generateKeyPair()) {
                assertEquals(HwPackageSigner.PUBLIC_KEY_BYTES, key.publicKey().length);
                assertEquals(4896, key.mlDsaSecretKey().length());
                assertEquals(128, key.slhDsaSecretKey().length());

                mx.addFunction(0x200, "inc");
                mx.addResource(1);
                mx.addResource(2);
                byte[] vin = new byte[256], inc = new byte[256];
                for (int i = 0; i < 256; i++) {
                    vin[i] = (byte) i;
                    inc[i] = (byte) (i + 1);
                }
                mx.addVector(0x200, new HwVector(0, vin, inc));
                Sim sim = new Sim();
                int psoft = mx.addProvider(0x200, 1, HwKind.SOFT, 50, addk(1, new int[1]));
                int preg = mx.addProvider(0x200, 2, HwKind.FABRIC, 1, (op, x) -> sim.run(x));
                assertEquals(Truth.TRUE, mx.attest(psoft));

                assertThrows(CapabilityDeniedException.class,
                        () -> HwRegion.create(nohw, mx, preg, 1, 0xD0, null, sim, 16));
                assertEquals(-1, status(assertThrows(ZxvException.class,
                        () -> HwRegion.create(cap, mx, psoft, 1, 0xD0, null, sim, 16))));
                try (HwRegion rg = HwRegion.create(cap, mx, preg, 1, 0xD0, null, sim, 16)) {
                    rg.trustKey(key.publicKey());
                    List<Ev> events = new ArrayList<>();
                    rg.onEvent((r, e, why, v) -> events.add(new Ev(r, e, why, v)));

                    // Signing 1: the wrong signer is refused and recorded.
                    byte[] forged = signer.buildImage(HwPackageSigner.ImageKind.PARTIAL, 0xD0, null, 0x200, 1, 1, inc, other);
                    assertThrows(VerificationFailedException.class, () -> rg.stage(forged));

                    // Signing 2: the update. Wrong hardware first (checked before the signature).
                    byte[] good = signer.buildImage(HwPackageSigner.ImageKind.PARTIAL, 0xD0, null, 0x200, 1, 1, inc, key);
                    assertEquals(HwPackageSigner.HEADER_BYTES + 256 + HwPackageSigner.SIGNATURE_BYTES, good.length);
                    byte[] wrongHw = good.clone();
                    ByteBuffer.wrap(wrongHw).order(ByteOrder.LITTLE_ENDIAN).putInt(12, 0xD1);
                    ZxvUsageException target = assertThrows(ZxvUsageException.class, () -> rg.stage(wrongHw));
                    assertEquals("ZXV_E_TARGET", target.statusName());

                    rg.stage(good);
                    assertEquals(-9, status(assertThrows(ZxvException.class, rg::commit)));
                    rg.trial(5);
                    assertEquals(preg, mx.bound(0x200), "the reconfigured region serves");
                    rg.commit();
                    HwRegion.Info info = rg.info();
                    assertEquals(HwRegion.State.ACTIVE, info.state());
                    assertEquals(1, info.version());
                    assertEquals(1, info.floor());
                    assertEquals(HwEvent.REJECTED, events.get(0).event());
                    List<HwEvent> tail = events.subList(events.size() - 3, events.size()).stream().map(Ev::event).toList();
                    assertEquals(List.of(HwEvent.STAGED, HwEvent.TESTED, HwEvent.ACTIVATED), tail);
                    assertEquals(new Ev(1, HwEvent.ACTIVATED, 0, 1), events.get(events.size() - 1));
                    assertArrayEquals(inc, mx.call(0x200, 0, vin));

                    // Replay: refused, recorded.
                    assertEquals(-28, status(assertThrows(ZxvRefusedException.class, () -> rg.stage(good))));

                    HwAuditRecord.Head head = rg.log();
                    assertEquals(events.size(), head.count(), "one event per record");
                    HwAuditRecord last = rg.logEntry(head.count() - 1);
                    assertEquals(HwEvent.REJECTED, last.event());
                    assertEquals(1, last.id());
                    assertTrue(last.tag().isForbidden());
                    assertArrayEquals(head.head(), last.hash());
                    int act = -1;
                    for (int i = 0; i < head.count(); i++) if (rg.logEntry(i).event() == HwEvent.ACTIVATED) act = i;
                    HwAuditRecord activated = rg.logEntry(act);
                    assertEquals(1, activated.version());
                    assertFalse(activated.tag().isForbidden());
                    assertEquals(-4, status(assertThrows(ZxvException.class, () -> rg.logEntry(head.count()))));

                    // Signing 3: a contract package for "inc", applied to the matrix.
                    byte[] pkg = signer.buildContract(0x200, 7, 3,
                            List.of(new HwVector(0, new byte[] {5, (byte) 250}, new byte[] {6, (byte) 251})), key);
                    assertThrows(CapabilityDeniedException.class, () -> {
                        try (HwMatrix weak = HwMatrix.create(nohw)) {
                            weak.applyContract(pkg);
                        }
                    });
                    assertThrows(VerificationFailedException.class, () -> mx.applyContract(pkg), "no key trusted yet");
                    mx.trustKey(key.publicKey());
                    assertEquals("ZXV_E_FORMAT", assertThrows(ZxvUsageException.class, () -> mx.applyContract(good)).statusName());
                    assertEquals(-22, status(assertThrows(ZxvException.class, () -> rg.stage(pkg))));
                    assertEquals(2, mx.applyContract(pkg), "the region and the emulator both prove it");
                    HwMatrix.FunctionInfo fi = mx.functionInfo(0x200);
                    assertEquals(7, fi.contract());
                    assertEquals(1, fi.vectors());
                    assertEquals(-28, status(assertThrows(ZxvRefusedException.class, () -> mx.applyContract(pkg))));
                    HwAuditRecord.Head mlog = mx.log();
                    assertEquals(3, mlog.count());
                    HwAuditRecord contract = mx.logEntry(1);
                    assertEquals(HwEvent.CONTRACT, contract.event());
                    assertEquals(0x200, contract.id());
                    assertEquals(7, contract.version());
                    assertNotEquals(0, mlog.head()[0] | mlog.head()[1] | mlog.head()[31]);
                }
            }
        }
    }

    @Test
    void permissionsAndFeatureBits() {
        assertEquals(1L << 10, Permission.HW_MATRIX.bit());
        assertEquals(1L << 11, Permission.HW_RECONF.bit());
        assertEquals((1L << 12) - 1, Permission.mask(Permission.all()));
        assertEquals(0x10000000, Feature.HW.bit());
        assertSame(StatusClass.REFUSED, Zxv.statusClass(-31));
        assertSame(StatusClass.USAGE, Zxv.statusClass(-30));
        assertSame(StatusClass.REFUSED, Zxv.statusClass(-26));
    }
}
