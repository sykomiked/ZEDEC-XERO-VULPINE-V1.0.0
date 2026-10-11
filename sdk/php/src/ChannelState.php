<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

/** An encoded EVM state-channel state (binary strings). */
final class ChannelState
{
    public function __construct(public readonly string $encoding, public readonly string $digest)
    {
    }

    /** 0x-prefixed hex digest (what the parties sign, EIP-191). */
    public function digestHex(): string
    {
        return "0x" . bin2hex($this->digest);
    }

    public function encodingHex(): string
    {
        return "0x" . bin2hex($this->encoding);
    }
}
