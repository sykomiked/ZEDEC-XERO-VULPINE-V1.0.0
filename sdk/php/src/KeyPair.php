<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

/** A public key (binary string) and its SecretKey. */
final class KeyPair
{
    public function __construct(public readonly string $publicKey, public readonly SecretKey $secretKey)
    {
    }

    /** Wipe the secret key. */
    public function close(): void
    {
        $this->secretKey->close();
    }
}
