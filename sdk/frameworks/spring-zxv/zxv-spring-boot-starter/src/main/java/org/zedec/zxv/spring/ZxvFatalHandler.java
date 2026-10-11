// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.spring;

import java.util.Optional;
import java.util.concurrent.atomic.AtomicBoolean;
import org.apache.commons.logging.Log;
import org.apache.commons.logging.LogFactory;
import org.springframework.boot.availability.AvailabilityChangeEvent;
import org.springframework.boot.availability.LivenessState;
import org.springframework.boot.availability.ReadinessState;
import org.springframework.context.ApplicationEventPublisher;
import org.zedec.zxv.ZxvFatalError;

/**
 * What the application does once a ZXV context has halted. The first time
 * it is tripped it logs the reason at ERROR and publishes
 * {@link LivenessState#BROKEN} and {@link ReadinessState#REFUSING_TRAFFIC},
 * so the {@code /actuator/health/liveness} and {@code readiness} probes report
 * DOWN and an orchestrator restarts the process from trusted state.
 *
 * <p>It never swallows anything: callers rethrow the error after tripping it.
 * The context stays poisoned in C whatever the JVM does.
 */
public class ZxvFatalHandler {
    private static final Log LOG = LogFactory.getLog(ZxvFatalHandler.class);
    private static final int MAX_CAUSE_DEPTH = 64;

    private final ApplicationEventPublisher publisher;
    private final AtomicBoolean tripped = new AtomicBoolean();

    /**
     * Creates the handler.
     *
     * @param publisher where availability changes are published
     */
    public ZxvFatalHandler(ApplicationEventPublisher publisher) {
        this.publisher = publisher;
    }

    /**
     * The {@link ZxvFatalError} in {@code t} or its cause chain, if any.
     *
     * @param t a throwable, or null
     * @return the fatal error
     */
    public static Optional<ZxvFatalError> find(Throwable t) {
        for (int depth = 0; t != null && depth < MAX_CAUSE_DEPTH; depth++, t = t.getCause()) {
            if (t instanceof ZxvFatalError fatal) return Optional.of(fatal);
        }
        return Optional.empty();
    }

    /**
     * Trips the handler if {@code t} carries a {@link ZxvFatalError}.
     *
     * @param t a throwable
     * @return the fatal error found, which the caller must rethrow
     */
    public Optional<ZxvFatalError> inspect(Throwable t) {
        Optional<ZxvFatalError> fatal = find(t);
        fatal.ifPresent(f -> trip(f.statusName()));
        return fatal;
    }

    /**
     * Marks the application broken. Idempotent.
     *
     * @param why a short reason for the log (a status name, not secret data)
     */
    public void trip(String why) {
        if (tripped.compareAndSet(false, true)) {
            LOG.error("ZXV context halted (" + why + "): liveness BROKEN, readiness REFUSING_TRAFFIC."
                    + " Restart from trusted state; the context refuses every further call.");
            AvailabilityChangeEvent.publish(publisher, this, LivenessState.BROKEN);
            AvailabilityChangeEvent.publish(publisher, this, ReadinessState.REFUSING_TRAFFIC);
        }
    }

    /**
     * True once a fatal error has been seen.
     *
     * @return whether {@link #trip} ran
     */
    public boolean isTripped() {
        return tripped.get();
    }
}
