<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv\Exception;

/**
 * ABI 1.6: a LIVE payment request marked as having a paid chance outcome
 * (loot boxes, paid draws) is refused (ZXV_E_CHANCE, REFUSED class). The
 * allotment is a separate module, not a payment request.
 */
class ChanceRefusedException extends ZxvRefusedException
{
}
