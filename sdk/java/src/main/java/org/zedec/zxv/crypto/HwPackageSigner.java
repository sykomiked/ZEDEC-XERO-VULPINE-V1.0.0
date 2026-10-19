// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.crypto;

import static java.lang.foreign.ValueLayout.JAVA_BYTE;
import static java.lang.foreign.ValueLayout.JAVA_LONG;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.util.List;
import java.util.Objects;
import org.zedec.zxv.Capability;
import org.zedec.zxv.hw.HwVector;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Native;

/**
 * Release keys and signed packages for hardware updates (ABI 1.4). Every
 * package is {@code header || payload || signatures}, signed with ML-DSA-87
 * AND SLH-DSA-SHAKE-256s; a region or matrix accepts it only if both verify
 * under a key it trusts. Needs {@code CRYPTO_SIGN}. SLH-DSA signing takes
 * seconds: building a package is a release step.
 *
 * <pre>{@code
 * HwPackageSigner signer = HwPackageSigner.with(cap);
 * try (HwPackageSigner.KeyPair key = signer.generateKeyPair()) {
 *     byte[] image = signer.buildImage(HwPackageSigner.ImageKind.PARTIAL, 0xD0, null, fnId, 1, 1, payload, key);
 *     region.trustKey(key.publicKey());
 *     region.stage(image);
 * }
 * }</pre>
 */
public final class HwPackageSigner {
    /** Release public key length: the ML-DSA-87 key followed by the SLH-DSA one. */
    public static final int PUBLIC_KEY_BYTES = 2656;
    /** ML-DSA-87 secret key length. */
    public static final int MLDSA_SECRET_KEY_BYTES = 4896;
    /** SLH-DSA-SHAKE-256s secret key length. */
    public static final int SLHDSA_SECRET_KEY_BYTES = 128;
    /** Both signatures. */
    public static final int SIGNATURE_BYTES = 34419;
    /** Package header length. */
    public static final int HEADER_BYTES = 96;

    /** Package kinds ({@code ZXV_HW_IMG_*}). */
    public enum ImageKind {
        /** A full bitstream. */
        BITSTREAM(1),
        /** A partial bitstream. */
        PARTIAL(2),
        /** Microcode. */
        MICROCODE(3),
        /** Firmware. */
        FIRMWARE(4),
        /** A new golden-vector contract for a function ({@link #buildContract}). */
        VECTORS(5);

        private final int code;

        ImageKind(int code) {
            this.code = code;
        }

        /** The numeric code. */
        public int code() {
            return code;
        }
    }

    /**
     * A release key pair. Closing it wipes both secret keys.
     *
     * @param publicKey       2656 bytes, to trust on regions and matrices
     * @param mlDsaSecretKey  the off-heap ML-DSA-87 secret key
     * @param slhDsaSecretKey the off-heap SLH-DSA-SHAKE-256s secret key
     */
    public record KeyPair(byte[] publicKey, SecretKey mlDsaSecretKey, SecretKey slhDsaSecretKey) implements AutoCloseable {
        @Override
        public void close() {
            try {
                mlDsaSecretKey.close();
            } finally {
                slhDsaSecretKey.close();
            }
        }
    }

    private final Gate gate;

    private HwPackageSigner(Capability cap) {
        this.gate = new Gate(cap);
    }

    /** Operations gated by {@code cap} ({@code CRYPTO_SIGN}). */
    public static HwPackageSigner with(Capability cap) {
        return new HwPackageSigner(cap);
    }

    /** A fresh release key pair. The secret keys never reach the Java heap. */
    public KeyPair generateKeyPair() {
        SecretKey sk = SecretKey.allocate(SecretKey.Kind.MLDSA87);
        SecretKey ssk = SecretKey.allocate(SecretKey.Kind.SLHDSA_SHAKE256S);
        try (Arena a = Arena.ofConfined()) {
            MemorySegment pk = a.allocate(PUBLIC_KEY_BYTES);
            MemorySegment s = sk.acquire(SecretKey.Kind.MLDSA87);
            try {
                MemorySegment ss = ssk.acquire(SecretKey.Kind.SLHDSA_SHAKE256S);
                try {
                    gate.call(Native.hw_keygen, pk, s, ss);
                } finally {
                    ssk.release();
                }
            } finally {
                sk.release();
            }
            return new KeyPair(pk.toArray(JAVA_BYTE), sk, ssk);
        } catch (RuntimeException | Error e) {
            sk.close();
            ssk.close();
            throw e;
        }
    }

    /**
     * Builds and signs an image for a region.
     *
     * @param kind         what the payload is (not {@link ImageKind#VECTORS}; use {@link #buildContract})
     * @param targetClass  the hardware class it is for
     * @param targetDigest the exact-part digest (32 bytes), or null
     * @param fnId         the function the region will serve
     * @param version      strictly increasing per region
     * @param svn          the anti-rollback floor it raises to on commit
     * @param payload      the image bytes
     * @param key          the release key
     */
    public byte[] buildImage(ImageKind kind, int targetClass, byte[] targetDigest, int fnId, int version, int svn,
            byte[] payload, KeyPair key) {
        return build(kind.code, targetClass, targetDigest, fnId, version, svn, payload, key);
    }

    /**
     * Builds and signs a contract package ({@code ZXV_HW_IMG_VECTORS}): a newer
     * golden-vector set for {@code fnId}, applied with
     * {@link org.zedec.zxv.hw.HwMatrix#applyContract}.
     *
     * @param version the contract version (strictly newer than the matrix's)
     * @param svn     the function's new anti-rollback floor
     */
    public byte[] buildContract(int fnId, int version, int svn, List<HwVector> vectors, KeyPair key) {
        return build(ImageKind.VECTORS.code, 0, null, fnId, version, svn, HwVector.encodeList(vectors), key);
    }

    private byte[] build(int kind, int targetClass, byte[] targetDigest, int fnId, int version, int svn, byte[] payload,
            KeyPair key) {
        Objects.requireNonNull(payload, "payload");
        Objects.requireNonNull(key, "key");
        if (targetDigest != null) Ffi.requireLength("target digest", targetDigest, 32);
        try (Arena a = Arena.ofConfined()) {
            MemorySegment td = Ffi.bytes(a, targetDigest), p = Ffi.bytes(a, payload), len = a.allocate(JAVA_LONG);
            MemorySegment s = key.mlDsaSecretKey().acquire(SecretKey.Kind.MLDSA87);
            try {
                MemorySegment ss = key.slhDsaSecretKey().acquire(SecretKey.Kind.SLHDSA_SHAKE256S);
                try {
                    // R4: the first call only sizes the package (no signing).
                    int st = gate.raw(Native.hw_image_build, kind, targetClass, td, fnId, version, svn, p,
                            (long) payload.length, s, ss, MemorySegment.NULL, 0L, len);
                    if (st != Ffi.E_BUFFER) Ffi.check(st, gate.ctx);
                    long n = len.get(JAVA_LONG, 0);
                    MemorySegment buf = a.allocate(n);
                    gate.call(Native.hw_image_build, kind, targetClass, td, fnId, version, svn, p, (long) payload.length,
                            s, ss, buf, n, len);
                    return buf.asSlice(0, len.get(JAVA_LONG, 0)).toArray(JAVA_BYTE);
                } finally {
                    key.slhDsaSecretKey().release();
                }
            } finally {
                key.mlDsaSecretKey().release();
            }
        }
    }
}
