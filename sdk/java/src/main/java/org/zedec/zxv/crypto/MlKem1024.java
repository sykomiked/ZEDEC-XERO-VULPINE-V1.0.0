// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.crypto;

import static java.lang.foreign.ValueLayout.JAVA_BYTE;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import org.zedec.zxv.Capability;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Native;

/**
 * FIPS 203 ML-KEM-1024 key encapsulation through one capability holding
 * {@code CRYPTO_KEM}. The decapsulation key and every shared secret are
 * {@link SecretKey}s (off-heap, wiped on close). Not FIPS 140-3 validated.
 *
 * <pre>{@code
 * MlKem1024 kem = MlKem1024.with(cap);
 * try (MlKem1024.KeyPair kp = kem.generateKeyPair();
 *      MlKem1024.Encapsulation enc = kem.encapsulate(kp.encapsulationKey());
 *      SecretKey ss = kem.decapsulate(kp.decapsulationKey(), enc.ciphertext())) {
 *     assert ss.contentEquals(enc.sharedSecret());
 * }
 * }</pre>
 */
public final class MlKem1024 {
    /** Encapsulation (public) key length. */
    public static final int ENCAPSULATION_KEY_BYTES = 1568;
    /** Decapsulation (secret) key length. */
    public static final int DECAPSULATION_KEY_BYTES = 3168;
    /** Ciphertext length. */
    public static final int CIPHERTEXT_BYTES = 1568;
    /** Shared secret length. */
    public static final int SHARED_SECRET_BYTES = 32;

    /**
     * A key pair. Closing it wipes the decapsulation key.
     *
     * @param encapsulationKey 1568 bytes, public
     * @param decapsulationKey the off-heap secret key
     */
    public record KeyPair(byte[] encapsulationKey, SecretKey decapsulationKey) implements AutoCloseable {
        @Override
        public void close() {
            decapsulationKey.close();
        }
    }

    /**
     * The result of encapsulating. Closing it wipes the shared secret.
     *
     * @param ciphertext   1568 bytes to send to the key's owner
     * @param sharedSecret the 32-byte shared secret, off-heap
     */
    public record Encapsulation(byte[] ciphertext, SecretKey sharedSecret) implements AutoCloseable {
        @Override
        public void close() {
            sharedSecret.close();
        }
    }

    private final Gate gate;

    private MlKem1024(Capability cap) {
        this.gate = new Gate(cap);
    }

    /** ML-KEM-1024 operations gated by {@code cap}. */
    public static MlKem1024 with(Capability cap) {
        return new MlKem1024(cap);
    }

    /** A fresh key pair. */
    public KeyPair generateKeyPair() {
        return keygen(null, null);
    }

    /** The deterministic key pair of seeds {@code d} and {@code z} (32 bytes each; FIPS 203 KeyGen_internal). */
    public KeyPair generateKeyPair(byte[] d, byte[] z) {
        Ffi.requireLength("d", d, 32);
        Ffi.requireLength("z", z, 32);
        return keygen(d, z);
    }

    private KeyPair keygen(byte[] d, byte[] z) {
        SecretKey dk = SecretKey.allocate(SecretKey.Kind.MLKEM1024);
        try (Arena a = Arena.ofConfined()) {
            MemorySegment ek = a.allocate(ENCAPSULATION_KEY_BYTES);
            MemorySegment s = dk.acquire(SecretKey.Kind.MLKEM1024);
            try {
                if (d == null) {
                    gate.call(Native.mlkem_keygen, ek, s);
                } else {
                    MemorySegment ds = Ffi.bytes(a, d), zs = Ffi.bytes(a, z);
                    try {
                        gate.call(Native.mlkem_keygen_derand, ds, zs, ek, s);
                    } finally {
                        Ffi.call(Native.secure_zero, ds, 32L);
                        Ffi.call(Native.secure_zero, zs, 32L);
                    }
                }
            } finally {
                dk.release();
            }
            return new KeyPair(ek.toArray(JAVA_BYTE), dk);
        } catch (RuntimeException | Error e) {
            dk.close();
            throw e;
        }
    }

    /**
     * Encapsulates a fresh shared secret to an encapsulation key.
     *
     * @throws org.zedec.zxv.ZxvUsageException {@code ZXV_E_FORMAT} if the key fails the FIPS 203 modulus check
     */
    public Encapsulation encapsulate(byte[] encapsulationKey) {
        return encaps(encapsulationKey, null);
    }

    /** Deterministic encapsulation from a 32-byte message {@code m} (for known-answer tests). */
    public Encapsulation encapsulate(byte[] encapsulationKey, byte[] m) {
        Ffi.requireLength("m", m, 32);
        return encaps(encapsulationKey, m);
    }

    private Encapsulation encaps(byte[] ek, byte[] m) {
        Ffi.requireLength("encapsulation key", ek, ENCAPSULATION_KEY_BYTES);
        SecretKey ss = SecretKey.allocate(SecretKey.Kind.SHARED_SECRET);
        try (Arena a = Arena.ofConfined()) {
            MemorySegment ct = a.allocate(CIPHERTEXT_BYTES);
            MemorySegment s = ss.acquire(SecretKey.Kind.SHARED_SECRET);
            try {
                if (m == null) {
                    gate.call(Native.mlkem_encaps, Ffi.bytes(a, ek), ct, s);
                } else {
                    MemorySegment ms = Ffi.bytes(a, m);
                    try {
                        gate.call(Native.mlkem_encaps_derand, Ffi.bytes(a, ek), ms, ct, s);
                    } finally {
                        Ffi.call(Native.secure_zero, ms, 32L);
                    }
                }
            } finally {
                ss.release();
            }
            return new Encapsulation(ct.toArray(JAVA_BYTE), ss);
        } catch (RuntimeException | Error e) {
            ss.close();
            throw e;
        }
    }

    /** Recovers the shared secret from a ciphertext with the decapsulation key. */
    public SecretKey decapsulate(SecretKey decapsulationKey, byte[] ciphertext) {
        Ffi.requireLength("ciphertext", ciphertext, CIPHERTEXT_BYTES);
        SecretKey ss = SecretKey.allocate(SecretKey.Kind.SHARED_SECRET);
        try (Arena a = Arena.ofConfined()) {
            MemorySegment s = ss.acquire(SecretKey.Kind.SHARED_SECRET);
            try {
                MemorySegment dk = decapsulationKey.acquire(SecretKey.Kind.MLKEM1024);
                try {
                    gate.call(Native.mlkem_decaps, dk, Ffi.bytes(a, ciphertext), s);
                } finally {
                    decapsulationKey.release();
                }
            } finally {
                ss.release();
            }
            return ss;
        } catch (RuntimeException | Error e) {
            ss.close();
            throw e;
        }
    }
}
