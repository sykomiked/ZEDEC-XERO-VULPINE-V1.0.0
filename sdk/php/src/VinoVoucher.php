<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

/** ABI 1.6: one voucher as VinoPlay::voucher() reports it (an immutable value). */
final class VinoVoucher
{
    /**
     * @param int|string $id the voucher id (> 0, never reused in its sandbox)
     * @param int|string $face its Fibonacci face, in play units
     * @param int $issuer the issuer's account id
     * @param int $holder the holder's account id
     * @param VinoRail|int $rail the one rail it can be spent on (an int only if unknown to this SDK)
     * @param VinoVoucherState|int $state active or redeemed (an int only if unknown)
     */
    public function __construct(
        public readonly int|string $id,
        public readonly int|string $face,
        public readonly int $issuer,
        public readonly int $holder,
        public readonly VinoRail|int $rail,
        public readonly VinoVoucherState|int $state,
    ) {
    }

    /** True while it can still be spent or redeemed. */
    public function isActive(): bool
    {
        return $this->state === VinoVoucherState::Active;
    }

    /** True once redeemed (retired). */
    public function isRedeemed(): bool
    {
        return $this->state === VinoVoucherState::Redeemed;
    }
}
