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
 * RFC 8032 Ed25519 signatures through one capability. Key generation,
 * public-key derivation and signing need {@code CRYPTO_SIGN}; verifying too
 * (every call is gated in ABI 1.1).
 *
 * <pre>{@code
 * Ed25519 ed = Ed25519.with(cap);
 * try (Ed25519.KeyPair kp = ed.generateKeyPair()) {
 *     byte[] sig = ed.sign(kp.secretKey(), msg);
 *     boolean ok = ed.verify(kp.publicKey(), msg, sig);
 * }
 * }</pre>
 */
public final class Ed25519 {
    /** Public key length. */
    public static final int PUBLIC_KEY_BYTES = 32;
    /** Secret key (seed) length. */
    public static final int SECRET_KEY_BYTES = 32;
    /** Signature length. */
    public static final int SIGNATURE_BYTES = 64;

    /**
     * A key pair. Closing it closes (wipes) the secret key.
     *
     * @param publicKey 32 bytes
     * @param secretKey the off-heap seed
     */
    public record KeyPair(byte[] publicKey, SecretKey secretKey) implements AutoCloseable {
        @Override
        public void close() {
            secretKey.close();
        }
    }

    private final Gate gate;

    private Ed25519(Capability cap) {
        this.gate = new Gate(cap);
    }

    /** Ed25519 operations gated by {@code cap}. */
    public static Ed25519 with(Capability cap) {
        return new Ed25519(cap);
    }

    /** A fresh key pair from the OS CSPRNG. */
    public KeyPair generateKeyPair() {
        SecretKey sk = SecretKey.allocate(SecretKey.Kind.ED25519);
        try (Arena a = Arena.ofConfined()) {
            MemorySegment pk = a.allocate(PUBLIC_KEY_BYTES);
            MemorySegment s = sk.acquire(SecretKey.Kind.ED25519);
            try {
                gate.call(Native.ed_keygen, pk, s);
            } finally {
                sk.release();
            }
            return new KeyPair(pk.toArray(JAVA_BYTE), sk);
        } catch (RuntimeException | Error e) {
            sk.close();
            throw e;
        }
    }

    /** The public key of a secret key. */
    public byte[] publicKey(SecretKey secretKey) {
        try (Arena a = Arena.ofConfined()) {
            MemorySegment pk = a.allocate(PUBLIC_KEY_BYTES);
            MemorySegment s = secretKey.acquire(SecretKey.Kind.ED25519);
            try {
                gate.call(Native.ed_public, s, pk);
            } finally {
                secretKey.release();
            }
            return pk.toArray(JAVA_BYTE);
        }
    }

    /** Signs a message (64-byte signature). */
    public byte[] sign(SecretKey secretKey, byte[] message) {
        try (Arena a = Arena.ofConfined()) {
            MemorySegment sig = a.allocate(SIGNATURE_BYTES);
            sign(secretKey, Ffi.bytes(a, message), sig);
            return sig.toArray(JAVA_BYTE);
        }
    }

    /**
     * Signs a message held in a segment, writing the signature into
     * {@code signatureOut} (64 bytes). Native segments are used in place;
     * heap segments are copied.
     */
    public void sign(SecretKey secretKey, MemorySegment message, MemorySegment signatureOut) {
        Ffi.requireLength("signature", signatureOut, SIGNATURE_BYTES);
        try (Arena a = Arena.ofConfined()) {
            MemorySegment m = Ffi.nativeOrCopy(a, message);
            MemorySegment out = signatureOut.isNative() ? signatureOut : a.allocate(SIGNATURE_BYTES);
            MemorySegment s = secretKey.acquire(SecretKey.Kind.ED25519);
            try {
                gate.call(Native.ed_sign, s, m, message.byteSize(), out);
            } finally {
                secretKey.release();
            }
            if (out != signatureOut) MemorySegment.copy(out, 0, signatureOut, 0, SIGNATURE_BYTES);
        }
    }

    /** True if {@code signature} is valid for {@code message} under {@code publicKey}. */
    public boolean verify(byte[] publicKey, byte[] message, byte[] signature) {
        Ffi.requireLength("public key", publicKey, PUBLIC_KEY_BYTES);
        Ffi.requireLength("signature", signature, SIGNATURE_BYTES);
        try (Arena a = Arena.ofConfined()) {
            return verify(Ffi.bytes(a, publicKey), Ffi.bytes(a, message), Ffi.bytes(a, signature));
        }
    }

    /** {@link #verify(byte[], byte[], byte[])} over segments (native ones are read in place). */
    public boolean verify(MemorySegment publicKey, MemorySegment message, MemorySegment signature) {
        Ffi.requireLength("public key", publicKey, PUBLIC_KEY_BYTES);
        Ffi.requireLength("signature", signature, SIGNATURE_BYTES);
        try (Arena a = Arena.ofConfined()) {
            return gate.verify(Native.ed_verify, Ffi.nativeOrCopy(a, publicKey), Ffi.nativeOrCopy(a, message),
                    message.byteSize(), Ffi.nativeOrCopy(a, signature));
        }
    }
}
