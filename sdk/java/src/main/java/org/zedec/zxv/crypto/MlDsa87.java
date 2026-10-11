// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.crypto;

import static java.lang.foreign.ValueLayout.JAVA_BYTE;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import org.zedec.zxv.Capability;
import org.zedec.zxv.internal.Errors;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Native;

/**
 * FIPS 204 ML-DSA-87 post-quantum signatures through one capability holding
 * {@code CRYPTO_SIGN}. Signatures take an optional context string (at most
 * 255 bytes) that must match at verification. Not FIPS 140-3 validated.
 *
 * <pre>{@code
 * MlDsa87 dsa = MlDsa87.with(cap);
 * try (MlDsa87.KeyPair kp = dsa.generateKeyPair()) {
 *     byte[] sig = dsa.sign(kp.secretKey(), msg, ctx);
 *     boolean ok = dsa.verify(kp.publicKey(), msg, ctx, sig);
 * }
 * }</pre>
 */
public final class MlDsa87 {
    /** Public key length. */
    public static final int PUBLIC_KEY_BYTES = 2592;
    /** Secret key length. */
    public static final int SECRET_KEY_BYTES = 4896;
    /** Signature length. */
    public static final int SIGNATURE_BYTES = 4627;
    /** Longest context string. */
    public static final int CONTEXT_MAX = 255;

    /**
     * A key pair. Closing it wipes the secret key.
     *
     * @param publicKey 2592 bytes
     * @param secretKey the off-heap secret key
     */
    public record KeyPair(byte[] publicKey, SecretKey secretKey) implements AutoCloseable {
        @Override
        public void close() {
            secretKey.close();
        }
    }

    private final Gate gate;

    private MlDsa87(Capability cap) {
        this.gate = new Gate(cap);
    }

    /** ML-DSA-87 operations gated by {@code cap}. */
    public static MlDsa87 with(Capability cap) {
        return new MlDsa87(cap);
    }

    /** A fresh key pair. */
    public KeyPair generateKeyPair() {
        return keygen(null);
    }

    /** The deterministic key pair of a 32-byte seed (for known-answer tests and key escrow). */
    public KeyPair generateKeyPair(byte[] seed) {
        Ffi.requireLength("seed", seed, 32);
        return keygen(seed);
    }

    private KeyPair keygen(byte[] seed) {
        SecretKey sk = SecretKey.allocate(SecretKey.Kind.MLDSA87);
        try (Arena a = Arena.ofConfined()) {
            MemorySegment pk = a.allocate(PUBLIC_KEY_BYTES);
            MemorySegment s = sk.acquire(SecretKey.Kind.MLDSA87);
            try {
                if (seed == null) {
                    gate.call(Native.mldsa_keygen, pk, s);
                } else {
                    MemorySegment sd = Ffi.bytes(a, seed);
                    try {
                        gate.call(Native.mldsa_keygen_seed, sd, pk, s);
                    } finally {
                        Ffi.call(Native.secure_zero, sd, 32L);
                    }
                }
            } finally {
                sk.release();
            }
            return new KeyPair(pk.toArray(JAVA_BYTE), sk);
        } catch (RuntimeException | Error e) {
            sk.close();
            throw e;
        }
    }

    /** Hedged signing (fresh randomness each time). {@code context} may be null. */
    public byte[] sign(SecretKey secretKey, byte[] message, byte[] context) {
        return signWith(false, secretKey, message, context);
    }

    /** Deterministic signing: the same inputs give the same signature (for known-answer tests). */
    public byte[] signDeterministic(SecretKey secretKey, byte[] message, byte[] context) {
        return signWith(true, secretKey, message, context);
    }

    private byte[] signWith(boolean det, SecretKey secretKey, byte[] message, byte[] context) {
        checkContext(context);
        try (Arena a = Arena.ofConfined()) {
            MemorySegment sig = a.allocate(SIGNATURE_BYTES);
            MemorySegment m = Ffi.bytes(a, message), c = Ffi.bytes(a, context);
            MemorySegment s = secretKey.acquire(SecretKey.Kind.MLDSA87);
            try {
                gate.call(det ? Native.mldsa_sign_det : Native.mldsa_sign, s, m, (long) message.length, c,
                        (long) (context == null ? 0 : context.length), sig);
            } finally {
                secretKey.release();
            }
            return sig.toArray(JAVA_BYTE);
        }
    }

    /** True if the signature is valid for the message and context under the public key. */
    public boolean verify(byte[] publicKey, byte[] message, byte[] context, byte[] signature) {
        Ffi.requireLength("public key", publicKey, PUBLIC_KEY_BYTES);
        Ffi.requireLength("signature", signature, SIGNATURE_BYTES);
        checkContext(context);
        try (Arena a = Arena.ofConfined()) {
            return gate.verify(Native.mldsa_verify, Ffi.bytes(a, publicKey), Ffi.bytes(a, message),
                    (long) message.length, Ffi.bytes(a, context), (long) (context == null ? 0 : context.length),
                    Ffi.bytes(a, signature));
        }
    }

    private static void checkContext(byte[] context) {
        if (context != null && context.length > CONTEXT_MAX) {
            throw Errors.usage(-1, "an ML-DSA context string is at most 255 bytes");
        }
    }
}
