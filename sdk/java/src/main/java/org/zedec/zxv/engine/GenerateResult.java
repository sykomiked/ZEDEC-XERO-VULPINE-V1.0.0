// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.engine;

/**
 * The outcome of one generation.
 *
 * @param text            the generated text (the pieces joined, decoded as UTF-8)
 * @param tokens          the generated token ids
 * @param promptTokens    how many prompt tokens were evaluated
 * @param generatedTokens how many tokens were generated
 * @param stopReason      why it stopped
 */
public record GenerateResult(String text, int[] tokens, int promptTokens, int generatedTokens, StopReason stopReason) {}
