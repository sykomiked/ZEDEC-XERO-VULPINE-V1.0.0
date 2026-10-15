<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

/** ABI 1.6: one play account as VinoPlay::account() reports it (an immutable value). */
final class VinoAccount
{
    /**
     * @param int $id the account id (ids count up from 0)
     * @param int $owner the owner id given when it was opened
     * @param bool $issuer true for an issuer, false for a holder
     * @param int|string $backing an issuer's play backing (0 for a holder)
     * @param int|string $faces an issuer's outstanding faces, or the faces a holder holds
     * @param int $voucherCount active vouchers it issued (issuer) or holds (holder)
     */
    public function __construct(
        public readonly int $id,
        public readonly int $owner,
        public readonly bool $issuer,
        public readonly int|string $backing,
        public readonly int|string $faces,
        public readonly int $voucherCount,
    ) {
    }
}
