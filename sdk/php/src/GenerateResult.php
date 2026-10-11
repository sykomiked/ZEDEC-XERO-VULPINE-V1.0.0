<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

/** What a generation produced. */
final class GenerateResult
{
    /**
     * @param list<int> $tokens generated token ids
     * @param int $stopReason a Session::STOP_* constant
     */
    public function __construct(
        public readonly string $text,
        public readonly array $tokens,
        public readonly int $stopReason,
        public readonly int $promptTokens,
    ) {
    }
}
