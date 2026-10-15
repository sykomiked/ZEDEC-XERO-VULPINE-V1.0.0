<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

/**
 * ABI 1.6: the lane of a payment request (ZXV_VINO_LANE_*). The two lanes
 * never meet: nothing converts play units into live value.
 */
enum VinoLane: int
{
    /** Play units inside an app's sandbox: not money, worth nothing. */
    case Play = 1;
    /** Real Vino (VFV minor units), approved in the user's own wallet. */
    case Live = 2;
}
