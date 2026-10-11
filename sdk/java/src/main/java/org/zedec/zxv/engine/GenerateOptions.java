// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.engine;

import org.zedec.zxv.Rational;

/**
 * Sampling parameters. Exact: temperature, top-p and repeat penalty are
 * {@link Rational}s, converted to the engine's Q16.16 integers. The same
 * options, seed and prompt give the same tokens on every machine.
 *
 * <pre>{@code
 * GenerateOptions greedy = GenerateOptions.greedy(32);
 * GenerateOptions sampled = GenerateOptions.builder()
 *         .maxTokens(64).temperature(Rational.parse("0.7")).topK(40)
 *         .topP(Rational.parse("0.95")).repeatPenalty(Rational.parse("1.1"))
 *         .repeatLastN(64).seed(42).build();
 * }</pre>
 *
 * @param maxTokens     the most tokens to generate
 * @param temperature   0 means greedy
 * @param topK          0 means off
 * @param topP          0 means off
 * @param repeatPenalty 0 means off
 * @param repeatLastN   how many recent tokens the penalty looks at
 * @param seed          the sampling seed
 */
public record GenerateOptions(int maxTokens, Rational temperature, int topK, Rational topP, Rational repeatPenalty,
        int repeatLastN, long seed) {

    /** Validates the parameters. */
    public GenerateOptions {
        if (maxTokens < 0) throw new IllegalArgumentException("maxTokens must be >= 0");
        if (topK < 0 || repeatLastN < 0) throw new IllegalArgumentException("topK and repeatLastN must be >= 0");
        java.util.Objects.requireNonNull(temperature, "temperature");
        java.util.Objects.requireNonNull(topP, "topP");
        java.util.Objects.requireNonNull(repeatPenalty, "repeatPenalty");
    }

    /** Greedy decoding (always the most likely token). */
    public static GenerateOptions greedy(int maxTokens) {
        return builder().maxTokens(maxTokens).build();
    }

    /** A builder starting from greedy decoding with 256 tokens. */
    public static Builder builder() {
        return new Builder();
    }

    /** Builds {@link GenerateOptions}. */
    public static final class Builder {
        private int maxTokens = 256;
        private Rational temperature = Rational.ZERO;
        private int topK;
        private Rational topP = Rational.ZERO;
        private Rational repeatPenalty = Rational.ZERO;
        private int repeatLastN;
        private long seed;

        private Builder() {}

        /** The most tokens to generate. */
        public Builder maxTokens(int v) {
            maxTokens = v;
            return this;
        }

        /** Sampling temperature (0 = greedy). */
        public Builder temperature(Rational v) {
            temperature = v;
            return this;
        }

        /** Top-k (0 = off). */
        public Builder topK(int v) {
            topK = v;
            return this;
        }

        /** Top-p nucleus (0 = off). */
        public Builder topP(Rational v) {
            topP = v;
            return this;
        }

        /** Repeat penalty (0 = off). */
        public Builder repeatPenalty(Rational v) {
            repeatPenalty = v;
            return this;
        }

        /** Window of the repeat penalty. */
        public Builder repeatLastN(int v) {
            repeatLastN = v;
            return this;
        }

        /** The sampling seed. */
        public Builder seed(long v) {
            seed = v;
            return this;
        }

        /** The options. */
        public GenerateOptions build() {
            return new GenerateOptions(maxTokens, temperature, topK, topP, repeatPenalty, repeatLastN, seed);
        }
    }
}
