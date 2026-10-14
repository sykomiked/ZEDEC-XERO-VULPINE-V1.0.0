<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv\Exception;

/**
 * ABI 1.6: the payment request has expired, or the receipt says it was
 * approved outside the request's window (ZXV_E_EXPIRED, REFUSED class).
 */
class ExpiredException extends ZxvRefusedException
{
}
