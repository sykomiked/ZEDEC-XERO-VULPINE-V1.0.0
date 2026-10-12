<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

/** What discovery reports for one implementation in a {@see HwMatrix}. */
final class HwProviderInfo
{
    public function __construct(
        /** Its index in the matrix. */
        public readonly int $index,
        public readonly int $fnId,
        /** Hw::KIND_* */
        public readonly int $kind,
        /** Its proof: only TRUE serves. */
        public readonly Truth $proof,
        /** The contract version it last proved. */
        public readonly int $contract,
        /** Hw::PF_* */
        public readonly int $flags,
        /** 0 (slowest) .. 15 */
        public readonly int $throughputClass,
        /** Hw::POWER_* */
        public readonly int $powerClass,
        public readonly int $calls,
        public readonly int $costSpent,
    ) {
    }

    /** @param int $flag one Hw::PF_* bit */
    public function hasFlag(int $flag): bool
    {
        return ($this->flags & $flag) !== 0;
    }

    public function isServing(): bool
    {
        return $this->hasFlag(Hw::PF_SERVING);
    }
}
