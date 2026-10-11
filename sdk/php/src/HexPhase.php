<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

/**
 * The lifecycle phase of a {@see Hexagram} (bits 1-0). The cycle is
 * Potential -> Emerging -> Manifest -> Dissolving -> Potential.
 */
enum HexPhase: int
{
    case Potential = 0;
    case Emerging = 1;
    case Dissolving = 2;
    case Manifest = 3;

    /** The next phase on the cycle (zxv_hex_phase_next). */
    public function next(): self
    {
        return self::from(Hexagram::call8("zxv_hex_phase_next", $this->value));
    }
}
