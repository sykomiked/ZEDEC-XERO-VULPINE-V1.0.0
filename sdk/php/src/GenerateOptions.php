<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

/**
 * Sampling options. Exact values only: temperature, topP and repeatPenalty
 * are Rationals (or ints / exact numeric strings), converted to Q16.16.
 * temperature 0 = greedy; topP 0 = off; repeatPenalty 0 = off.
 */
final class GenerateOptions
{
    public readonly Rational $temperature;
    public readonly Rational $topP;
    public readonly Rational $repeatPenalty;

    public function __construct(
        public readonly int $maxTokens = 64,
        Rational|int|float|string $temperature = 0,
        public readonly int $topK = 0,
        Rational|int|float|string $topP = 0,
        Rational|int|float|string $repeatPenalty = 0,
        public readonly int $repeatLastN = 64,
        public readonly int $seed = 0,
    ) {
        Native::uint32($maxTokens, "maxTokens");
        Native::uint32($topK, "topK");
        Native::uint32($repeatLastN, "repeatLastN");
        $this->temperature = Rational::from($temperature);
        $this->topP = Rational::from($topP);
        $this->repeatPenalty = Rational::from($repeatPenalty);
    }

    /** From raw Q16.16 integers (as vectors.json stores them). */
    public static function fromQ16(int $maxTokens = 64, int $temperatureQ16 = 0, int $topK = 0, int $topPQ16 = 0, int $repeatPenaltyQ16 = 0, int $repeatLastN = 64, int $seed = 0): self
    {
        return new self($maxTokens, Rational::of($temperatureQ16, 65536), $topK, Rational::of($topPQ16, 65536), Rational::of($repeatPenaltyQ16, 65536), $repeatLastN, $seed);
    }
}
