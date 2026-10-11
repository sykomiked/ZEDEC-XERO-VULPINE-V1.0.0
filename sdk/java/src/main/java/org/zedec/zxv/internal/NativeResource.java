// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.internal;

import java.lang.foreign.MemorySegment;
import java.util.ArrayList;
import java.util.List;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.locks.ReentrantReadWriteLock;
import org.zedec.zxv.ZxvContext;
import org.zedec.zxv.ZxvFatalError;

/**
 * One native handle with safe lifetime: calls take a read lock, close takes
 * the write lock, so a handle is never destroyed while a call on it is in
 * flight (R6). Children (handles created from this one) are closed first.
 * Internal.
 */
public final class NativeResource {
    /** The C destroy function of this handle type. */
    @FunctionalInterface
    public interface Destroyer {
        /** Frees the handle. */
        void destroy(MemorySegment handle);
    }

    private final String kind;
    private final MemorySegment handle;
    private final Destroyer destroyer;
    private final ZxvContext owner;
    private final NativeResource parent;
    private final ReentrantReadWriteLock lock = new ReentrantReadWriteLock();
    private final Set<NativeResource> children = ConcurrentHashMap.newKeySet();
    private final List<Runnable> onClose = new ArrayList<>();
    private volatile boolean closed;

    /**
     * Wraps a handle.
     *
     * @param kind      a name for messages ("capability")
     * @param handle    the native pointer
     * @param destroyer how to free it
     * @param owner     the context whose death makes this unusable (may be null)
     * @param parent    the resource that must outlive this one (may be null)
     */
    public NativeResource(String kind, MemorySegment handle, Destroyer destroyer, ZxvContext owner, NativeResource parent) {
        this.kind = kind;
        this.handle = handle;
        this.destroyer = destroyer;
        this.owner = owner;
        this.parent = parent;
        if (parent != null) parent.adopt(this);
    }

    private void adopt(NativeResource child) {
        lock.readLock().lock();
        try {
            if (closed) {
                child.destroyer.destroy(child.handle);
                throw new IllegalStateException(kind + " is closed");
            }
            children.add(child);
        } finally {
            lock.readLock().unlock();
        }
    }

    /** Runs {@code r} when this resource closes (before the handle is freed). */
    public synchronized void onClose(Runnable r) {
        onClose.add(r);
    }

    /** The context that owns this resource, or null. */
    public ZxvContext owner() {
        return owner;
    }

    /** True once closed. */
    public boolean isClosed() {
        return closed;
    }

    /**
     * Takes the read lock and returns the handle. Every call must be paired
     * with {@link #release()}. Fails if closed, or if the owning context died.
     */
    public MemorySegment acquire() {
        lock.readLock().lock();
        if (closed) {
            lock.readLock().unlock();
            throw new IllegalStateException("this " + kind + " is closed");
        }
        if (owner != null) {
            String why = Access.core().deadReason(owner);
            if (why != null) {
                lock.readLock().unlock();
                throw new ZxvFatalError(-18, "ZXV_E_POISONED", "the context halted earlier: " + why);
            }
        }
        return handle;
    }

    /** Releases the read lock taken by {@link #acquire()}. */
    public void release() {
        lock.readLock().unlock();
    }

    /** The handle without locking (only valid while the caller holds the lock). */
    public MemorySegment rawHandle() {
        return handle;
    }

    /** Destroys children, then this handle. Idempotent. */
    public void close() {
        if (lock.getReadHoldCount() > 0) {
            throw new IllegalStateException("cannot close a " + kind + " from inside a call or callback on it");
        }
        lock.writeLock().lock();
        try {
            if (closed) return;
            closed = true;
            for (NativeResource c : List.copyOf(children)) c.close();
            List<Runnable> hooks;
            synchronized (this) {
                hooks = List.copyOf(onClose);
            }
            for (Runnable r : hooks) r.run();
            destroyer.destroy(handle);
        } finally {
            lock.writeLock().unlock();
        }
        if (parent != null) parent.children.remove(this);
    }
}
