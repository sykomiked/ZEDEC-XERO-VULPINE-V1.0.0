<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

/** One record of a region's or a matrix's hash-chained audit log. */
final class HwLogEntry
{
    public function __construct(
        /** Hw::EV_* */
        public readonly int $event,
        /** The failure code (0 for none). */
        public readonly int $reason,
        /** The region id (the function id for Hw::EV_CONTRACT). */
        public readonly int $id,
        public readonly int $version,
        /** The hexagram tag 0..63 (forbidden for rejections and quarantines). */
        public readonly int $tag,
        /** SHA-256 of the package header (32 bytes). */
        public readonly string $subject,
        /** The record's chain hash (32 bytes). */
        public readonly string $hash,
    ) {
    }

    public function hexagram(): Hexagram
    {
        return Hexagram::fromTag($this->tag);
    }
}
