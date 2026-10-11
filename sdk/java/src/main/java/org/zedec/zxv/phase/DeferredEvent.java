// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.phase;

import java.util.concurrent.CompletableFuture;

/**
 * An S0 event parked on a {@link PhaseClock} until its due tick. It clears
 * when the clock is advanced to or past that tick.
 */
public final class DeferredEvent {
    /** The lifecycle of a deferred event. */
    public enum State {
        /** S0: waiting for its tick. */
        PENDING,
        /** Its tick was reached. */
        CLEARED,
        /** Cancelled before its tick. */
        CANCELLED
    }

    private final PhaseClock clock;
    private final long id;
    private final long dueTick;
    final CompletableFuture<DeferredEvent> cleared = new CompletableFuture<>();

    DeferredEvent(PhaseClock clock, long id, long dueTick) {
        this.clock = clock;
        this.id = id;
        this.dueTick = dueTick;
    }

    /** The event id in its clock. */
    public long id() {
        return id;
    }

    /** The absolute tick at which it clears. */
    public long dueTick() {
        return dueTick;
    }

    /** The current state, read from libzxv. */
    public State state() {
        return clock.eventState(id);
    }

    /** Cancels the event; {@link #awaitCleared()} then fails with a {@link java.util.concurrent.CancellationException}. */
    public void cancel() {
        clock.cancel(this);
    }

    /**
     * A future that completes (with this event) when the clock passes the
     * due tick. With a manual clock that happens inside the {@link
     * PhaseClock#advance} call that reaches it; with a real-time driver, on the
     * driver's thread. Dependent stages run there too unless you use the
     * {@code ...Async} variants.
     */
    public CompletableFuture<DeferredEvent> awaitCleared() {
        CompletableFuture<DeferredEvent> f = new CompletableFuture<>();
        cleared.whenComplete((v, e) -> {
            if (e == null) {
                f.complete(v);
            } else {
                f.completeExceptionally(e instanceof java.util.concurrent.CompletionException && e.getCause() != null
                        ? e.getCause() : e);
            }
        });
        return f;
    }

    @Override
    public String toString() {
        return "DeferredEvent[" + id + " due " + dueTick + "]";
    }
}
