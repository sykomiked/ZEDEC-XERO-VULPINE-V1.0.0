<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

/** One record of a {@see QuarantineLog}; the byte strings are 32 bytes each. */
final class QuarantineRecord
{
    public function __construct(
        public readonly int $seq,
        public readonly Hexagram $tag,
        /** 0..255 */
        public readonly int $reason,
        /** 0..4294967295 */
        public readonly int $submitter,
        public readonly string $subject,
        /** The previous record's hash (zeros for the first). */
        public readonly string $prev,
        public readonly string $hash,
    ) {
    }
}
