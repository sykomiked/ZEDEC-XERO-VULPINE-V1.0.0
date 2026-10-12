// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.internal;

import java.util.function.Supplier;

/**
 * Takes the monitors of two wrapper objects in a fixed order, so two threads
 * that combine the same pair in opposite orders cannot deadlock. Used by the
 * calls that take two handles that are not thread-safe in C. Internal.
 */
public final class Locks {
    private Locks() {}

    /** Runs {@code body} holding the monitors of {@code a} and {@code b} (the same object once). */
    public static <T> T both(Object a, Object b, Supplier<T> body) {
        if (a == b) {
            synchronized (a) {
                return body.get();
            }
        }
        int ha = System.identityHashCode(a), hb = System.identityHashCode(b);
        Object first = ha <= hb ? a : b, second = first == a ? b : a;
        if (ha == hb) {
            synchronized (Locks.class) { // a tie: serialize through one global lock
                synchronized (first) {
                    synchronized (second) {
                        return body.get();
                    }
                }
            }
        }
        synchronized (first) {
            synchronized (second) {
                return body.get();
            }
        }
    }
}
