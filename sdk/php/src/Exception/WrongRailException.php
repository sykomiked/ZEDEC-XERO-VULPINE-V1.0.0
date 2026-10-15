<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv\Exception;

/**
 * ABI 1.6: a voucher was spent or switched on a rail that is not its active
 * one (ZXV_E_RAIL, REFUSED class). A voucher lives on exactly one rail at a
 * time (bearer or ghost), so it can never be spent on both. Nothing changed.
 */
class WrongRailException extends ZxvRefusedException
{
}
