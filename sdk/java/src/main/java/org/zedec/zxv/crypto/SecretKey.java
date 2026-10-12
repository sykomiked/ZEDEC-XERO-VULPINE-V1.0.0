// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.crypto;

import static java.lang.foreign.ValueLayout.JAVA_BYTE;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.ref.Cleaner;
import java.util.concurrent.locks.ReentrantReadWriteLock;
import org.zedec.zxv.internal.Errors;
import org.zedec.zxv.internal.Ffi;
import org.zedec.zxv.internal.Native;

/**
 * Secret key material (an Ed25519 seed, an ML-DSA-87 secret key, an
 * ML-KEM-1024 decapsulation key, a 32-byte shared secret, or an
 * SLH-DSA-SHAKE-256s secret key) held outside the
 * Java heap, in memory from a shared {@link Arena}. The garbage collector
 * never sees or copies it.
 *
 * <p>{@link #close()} wipes it with {@code zxv_secure_zero} (a wipe the C
 * compiler may not remove) and frees it. A {@link Cleaner} does the same if
 * an unreachable key was never closed: a backstop, not a substitute; close
 * keys with try-with-resources.
 *
 * <p>The only way to get a heap copy is {@link #exportBytesUnsafe()}, named
 * so because it defeats the wiping. Thread-safe: closing waits for in-flight
 * operations that use the key.
 */
public final class SecretKey implements AutoCloseable {
    private static final Cleaner CLEANER = Cleaner.create();

    /** What the key is, and its fixed length. */
    public enum Kind {
        /** RFC 8032 Ed25519 private key (seed), 32 bytes. */
        ED25519(32),
        /** FIPS 204 ML-DSA-87 secret key, 4896 bytes. */
        MLDSA87(4896),
        /** FIPS 203 ML-KEM-1024 decapsulation key, 3168 bytes. */
        MLKEM1024(3168),
        /** An ML-KEM shared secret, 32 bytes. */
        SHARED_SECRET(32),
        /** FIPS 205 SLH-DSA-SHAKE-256s secret key, 128 bytes (hardware release keys, ABI 1.4). */
        SLHDSA_SHAKE256S(128);

        private final int length;

        Kind(int length) {
            this.length = length;
        }

        /** The length in bytes. */
        public int length() {
            return length;
        }
    }

    /** The cleaning action: holds no reference to the SecretKey itself. */
    private static final class Wiper implements Runnable {
        private final Arena arena;
        private final MemorySegment seg;
        private boolean done;

        Wiper(Arena arena, MemorySegment seg) {
            this.arena = arena;
            this.seg = seg;
        }

        @Override
        public synchronized void run() {
            if (done) return;
            done = true;
            try {
                Ffi.call(Native.secure_zero, seg, seg.byteSize());
            } finally {
                arena.close();
            }
        }
    }

    private final Kind kind;
    private final MemorySegment seg;
    private final Cleaner.Cleanable cleanable;
    private final ReentrantReadWriteLock lock = new ReentrantReadWriteLock();
    private volatile boolean closed;

    private SecretKey(Kind kind) {
        this.kind = kind;
        Arena arena = Arena.ofShared();
        this.seg = arena.allocate(kind.length);
        this.cleanable = CLEANER.register(this, new Wiper(arena, seg));
    }

    /** A zero-filled key of a kind, to be written by libzxv. */
    static SecretKey allocate(Kind kind) {
        return new SecretKey(kind);
    }

    /**
     * Copies key bytes into off-heap storage. The caller's array is a heap
     * copy the SDK cannot wipe: overwrite it (for example with
     * {@code Arrays.fill(bytes, (byte) 0)}) once this returns.
     */
    public static SecretKey copyOf(Kind kind, byte[] bytes) {
        Ffi.requireLength(kind + " key", bytes, kind.length);
        SecretKey k = new SecretKey(kind);
        MemorySegment.copy(bytes, 0, k.seg, JAVA_BYTE, 0, bytes.length);
        return k;
    }

    /** Copies key bytes from a segment (for example one you also wipe) into a new key. */
    public static SecretKey copyOf(Kind kind, MemorySegment bytes) {
        Ffi.requireLength(kind + " key", bytes, kind.length);
        SecretKey k = new SecretKey(kind);
        MemorySegment.copy(bytes, 0, k.seg, 0, kind.length);
        return k;
    }

    /** What the key is. */
    public Kind kind() {
        return kind;
    }

    /** The length in bytes. */
    public int length() {
        return kind.length;
    }

    /** True once closed (wiped). */
    public boolean isClosed() {
        return closed;
    }

    /** Takes the read lock and returns the segment; pair with {@link #release()}. */
    MemorySegment acquire(Kind expected) {
        if (expected != null && expected != kind) {
            throw Errors.usage(-1, "expected a " + expected + " key, got " + kind);
        }
        lock.readLock().lock();
        if (closed) {
            lock.readLock().unlock();
            throw new IllegalStateException("this secret key is closed (wiped)");
        }
        return seg;
    }

    void release() {
        lock.readLock().unlock();
    }

    /**
     * A heap copy of the key bytes. UNSAFE: the copy lives on the garbage
     * collected heap, which may move or keep it, and it is not wiped when
     * this key closes. Use it only to hand a key to code that needs a
     * {@code byte[]}, and overwrite the copy as soon as you can.
     */
    public byte[] exportBytesUnsafe() {
        MemorySegment s = acquire(null);
        try {
            return s.toArray(JAVA_BYTE);
        } finally {
            release();
        }
    }

    /** Compares two keys in constant time (for their length). */
    public boolean contentEquals(SecretKey other) {
        if (other.kind.length != kind.length) return false;
        MemorySegment a = acquire(null);
        try {
            MemorySegment b = other.acquire(null);
            try {
                int diff = 0;
                for (long i = 0; i < a.byteSize(); i++) diff |= a.get(JAVA_BYTE, i) ^ b.get(JAVA_BYTE, i);
                return diff == 0;
            } finally {
                other.release();
            }
        } finally {
            release();
        }
    }

    /** Wipes and frees the key. Idempotent. */
    @Override
    public void close() {
        lock.writeLock().lock();
        try {
            if (closed) return;
            closed = true;
            cleanable.clean();
        } finally {
            lock.writeLock().unlock();
        }
    }

    @Override
    public String toString() {
        return "SecretKey[" + kind + (closed ? ", closed" : "") + "]";
    }
}
