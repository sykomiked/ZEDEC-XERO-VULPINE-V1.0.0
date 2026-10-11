<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

/** A currency enabled in a context, or an ISO 4217 table entry. */
final class Currency
{
    public const FUND = 0x01;
    public const MINOR_NA = 0x02;
    public const NO_COUNTRY = 0x04;
    public const PAYABLE = 0x08;
    public const PRIVATE = 0x10;

    public function __construct(
        public readonly string $code,
        public readonly int $minorUnits,
        /** ZXV_CCYF_* bits */
        public readonly int $flags,
        public readonly ?int $numeric = null,
    ) {
    }

    /** A platform-private unit (never valid in ISO 20022). */
    public function isPrivate(): bool
    {
        return ($this->flags & self::PRIVATE) !== 0;
    }

    public function isPayable(): bool
    {
        return ($this->flags & self::PAYABLE) !== 0;
    }
}
