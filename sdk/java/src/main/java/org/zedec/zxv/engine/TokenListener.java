// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.engine;

/**
 * Receives each token as it is generated, on the generating thread.
 * Return {@code false} to stop (the result's stop reason is then
 * {@link StopReason#CALLBACK}). An exception thrown here stops generation and
 * is rethrown by {@code generate}.
 */
@FunctionalInterface
public interface TokenListener {
    /** Called once per token; return true to continue. */
    boolean onToken(Token token);
}
