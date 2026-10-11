// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.zedec.zxv.Vectors.hex;
import static org.zedec.zxv.Vectors.map;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.nio.charset.StandardCharsets;
import java.util.Map;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.zedec.zxv.crypto.Ed25519;
import org.zedec.zxv.crypto.Hashes;
import org.zedec.zxv.crypto.MlDsa87;
import org.zedec.zxv.crypto.MlKem1024;
import org.zedec.zxv.crypto.SecretKey;

/** Ed25519, ML-DSA-87 and ML-KEM-1024 against vectors.json, plus capability gating and secret handling. */
class CryptoTest {
    private ZxvContext ctx;
    private Capability cap;

    @BeforeEach
    void open() {
        ctx = ZxvContext.create();
        cap = ctx.rootCapability(Permission.all());
    }

    @AfterEach
    void close() {
        ctx.close();
    }

    private static byte[] utf8(Object o) {
        return ((String) o).getBytes(StandardCharsets.UTF_8);
    }

    @Test
    void ed25519Vector() {
        Map<String, Object> v = map(Vectors.root().get("ed25519"));
        Ed25519 ed = Ed25519.with(cap);
        try (SecretKey sk = SecretKey.copyOf(SecretKey.Kind.ED25519, hex(v.get("sk")))) {
            byte[] pk = ed.publicKey(sk);
            assertArrayEquals(hex(v.get("pk")), pk);
            byte[] msg = utf8(v.get("msg_utf8"));
            byte[] sig = ed.sign(sk, msg);
            assertArrayEquals(hex(v.get("sig")), sig);
            assertTrue(ed.verify(pk, msg, sig));
            sig[0] ^= 1;
            assertFalse(ed.verify(pk, msg, sig), "tampered signature rejected");
        }
    }

    @Test
    void ed25519Rfc8032EmptyMessageAndSegments() {
        Ed25519 ed = Ed25519.with(cap);
        try (SecretKey sk = SecretKey.copyOf(SecretKey.Kind.ED25519,
                hex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60"));
             Arena a = Arena.ofConfined()) {
            byte[] pk = ed.publicKey(sk);
            assertEquals("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", Vectors.toHex(pk));
            byte[] sig = ed.sign(sk, new byte[0]);
            assertEquals("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b",
                    Vectors.toHex(sig));
            MemorySegment out = a.allocate(64);
            ed.sign(sk, a.allocate(1).asSlice(0, 0), out);
            assertArrayEquals(sig, out.toArray(java.lang.foreign.ValueLayout.JAVA_BYTE), "zero-copy sign into native memory");
        }
        try (Ed25519.KeyPair kp = ed.generateKeyPair()) {
            byte[] m = "hello".getBytes(StandardCharsets.UTF_8);
            assertTrue(ed.verify(kp.publicKey(), m, ed.sign(kp.secretKey(), m)));
        }
    }

    @Test
    void mlDsa87Vector() {
        Map<String, Object> v = map(Vectors.root().get("mldsa87"));
        MlDsa87 dsa = MlDsa87.with(cap);
        byte[] msg = utf8(v.get("msg_utf8")), c = utf8(v.get("ctx_utf8"));
        try (MlDsa87.KeyPair kp = dsa.generateKeyPair(hex(v.get("seed")))) {
            assertArrayEquals(hex(v.get("pk_sha3_256")), Hashes.sha3_256(kp.publicKey()));
            assertArrayEquals(hex(v.get("sk_sha3_256")), Hashes.sha3_256(kp.secretKey().exportBytesUnsafe()));
            byte[] sig = dsa.signDeterministic(kp.secretKey(), msg, c);
            assertArrayEquals(hex(v.get("sig_deterministic_sha3_256")), Hashes.sha3_256(sig));
            assertTrue(dsa.verify(kp.publicKey(), msg, c, sig));
            byte[] hedged = dsa.sign(kp.secretKey(), msg, c);
            assertTrue(dsa.verify(kp.publicKey(), msg, c, hedged));
            assertFalse(dsa.verify(kp.publicKey(), msg, "zxw".getBytes(StandardCharsets.UTF_8), sig), "wrong context rejected");
        }
    }

    @Test
    void mlKem1024Vector() {
        Map<String, Object> v = map(Vectors.root().get("mlkem1024"));
        MlKem1024 kem = MlKem1024.with(cap);
        try (MlKem1024.KeyPair kp = kem.generateKeyPair(hex(v.get("d")), hex(v.get("z")));
             MlKem1024.Encapsulation enc = kem.encapsulate(kp.encapsulationKey(), hex(v.get("m")));
             SecretKey ss = kem.decapsulate(kp.decapsulationKey(), enc.ciphertext())) {
            assertArrayEquals(hex(v.get("ek_sha3_256")), Hashes.sha3_256(kp.encapsulationKey()));
            assertArrayEquals(hex(v.get("ct_sha3_256")), Hashes.sha3_256(enc.ciphertext()));
            assertArrayEquals(hex(v.get("ss")), enc.sharedSecret().exportBytesUnsafe());
            assertTrue(ss.contentEquals(enc.sharedSecret()));
        }
        try (MlKem1024.KeyPair kp = kem.generateKeyPair(); MlKem1024.Encapsulation enc = kem.encapsulate(kp.encapsulationKey());
             SecretKey ss = kem.decapsulate(kp.decapsulationKey(), enc.ciphertext())) {
            assertTrue(ss.contentEquals(enc.sharedSecret()), "random round trip agrees");
            byte[] bad = kp.encapsulationKey().clone();
            java.util.Arrays.fill(bad, 0, 32, (byte) 0xFF);
            ZxvUsageException e = assertThrows(ZxvUsageException.class, () -> kem.encapsulate(bad));
            assertEquals("ZXV_E_FORMAT", e.statusName());
        }
    }

    @Test
    void capabilityDenialAndSecretLifetime() {
        try (Capability kemOnly = cap.attenuate(Permission.CRYPTO_KEM)) {
            Ed25519 ed = Ed25519.with(kemOnly);
            SecretKey sk = SecretKey.copyOf(SecretKey.Kind.ED25519, new byte[32]);
            CapabilityDeniedException e = assertThrows(CapabilityDeniedException.class, () -> ed.sign(sk, new byte[1]));
            assertEquals(-17, e.status());
            assertEquals(StatusClass.REFUSED, e.statusClass());
            assertTrue(e instanceof ZxvRefusedException);
            sk.close();
            assertTrue(sk.isClosed());
            assertThrows(IllegalStateException.class, sk::exportBytesUnsafe, "a closed key is wiped and unusable");
            sk.close(); // idempotent
            assertThrows(ZxvUsageException.class, () -> SecretKey.copyOf(SecretKey.Kind.MLDSA87, new byte[3]));
            assertThrows(ZxvUsageException.class,
                    () -> MlDsa87.with(cap).sign(SecretKey.copyOf(SecretKey.Kind.ED25519, new byte[32]), new byte[1], null),
                    "a key of the wrong kind is refused");
        }
    }
}
