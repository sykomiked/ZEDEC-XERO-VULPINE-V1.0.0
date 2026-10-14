<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

/** ABI 1.6: a play-lane voucher's state (ZXV_VINO_V_*). */
enum VinoVoucherState: int
{
    /** Can still be spent or redeemed. */
    case Active = 1;
    /** Returned to its issuer and retired; nothing was paid out. */
    case Redeemed = 2;
}
