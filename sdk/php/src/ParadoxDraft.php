<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

/** ABI 1.5: a draft classification of a frontier record (Paradox::DRAFT_BYTES), an immutable value: send it for review. */
final class ParadoxDraft
{
    public readonly string $bytes;

    public function __construct(string $bytes)
    {
        $this->bytes = Native::exact($bytes, "draft", Paradox::DRAFT_BYTES);
    }

    /** The frontier record index. */
    public function record(): int
    {
        return unpack("V", substr($this->bytes, 0, 4))[1];
    }

    public function recordHash(): string
    {
        return substr($this->bytes, 4, 32);
    }

    public function signature(): string
    {
        return substr($this->bytes, 36, 32);
    }

    /** The proposed kind. */
    public function kind(): string|int
    {
        return Paradox::name(Paradox::KINDS, ord($this->bytes[68]));
    }

    public function reason(): int
    {
        return ord($this->bytes[69]);
    }

    /** @return list<string> Paradox::FLAGS names */
    public function flags(): array
    {
        return Web4::flagNames(Paradox::FLAGS, ord($this->bytes[70]));
    }

    public function equals(?self $other): bool
    {
        return $other !== null && $other->bytes === $this->bytes;
    }
}
