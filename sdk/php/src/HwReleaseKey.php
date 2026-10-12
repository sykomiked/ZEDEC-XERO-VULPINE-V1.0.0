<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

/**
 * A hardware release key ({@see Hw::keygen()}): the public key (ML-DSA-87
 * key followed by the SLH-DSA key, Hw::PK_BYTES) and the two secret keys,
 * held in native memory and wiped by close() or the destructor.
 */
final class HwReleaseKey
{
    public function __construct(
        public readonly string $publicKey,
        public readonly SecretKey $mldsaSecret,
        public readonly SecretKey $slhdsaSecret,
    ) {
    }

    /** Wipe both secret keys. Idempotent. */
    public function close(): void
    {
        $this->mldsaSecret->close();
        $this->slhdsaSecret->close();
    }

    public function __destruct()
    {
        $this->close();
    }

    /** @return array<string, mixed> never the secret bytes */
    public function __debugInfo(): array
    {
        return ["publicKey" => strlen($this->publicKey) . " bytes", "closed" => $this->mldsaSecret->isClosed()];
    }
}
