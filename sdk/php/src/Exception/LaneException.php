<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv\Exception;

/**
 * ABI 1.6: the wrong lane for the call, or a lane number that is neither
 * PLAY (1) nor LIVE (2) (ZXV_E_LANE, USAGE class: a caller bug).
 */
class LaneException extends ZxvUsageException
{
}
