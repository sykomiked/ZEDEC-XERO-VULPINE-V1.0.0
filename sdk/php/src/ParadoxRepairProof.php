<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

/**
 * ABI 1.5: a repair proof (Paradox::REPAIR_BYTES), an immutable value: what
 * containment found and did, bound by a hash. Share signature() with peers
 * so they can recognise the trap; signing the proof is your job.
 */
final class ParadoxRepairProof
{
    public readonly string $bytes;

    public function __construct(string $bytes)
    {
        $this->bytes = Native::exact($bytes, "proof", Paradox::REPAIR_BYTES);
    }

    /** @return list<string> the checks that fired (Paradox::TRIPS names) */
    public function trips(): array
    {
        return Web4::flagNames(Paradox::TRIPS, $this->u32(0));
    }

    public function steps(): int
    {
        return $this->u32(4);
    }

    public function spent(): int
    {
        return $this->u32(8);
    }

    /** State fingerprints examined. */
    public function traceN(): int
    {
        return $this->u32(12);
    }

    public function submitter(): int
    {
        return $this->u32(16);
    }

    public function stateLen(): int
    {
        return $this->u32(20);
    }

    /** SHA-256 of the state as found. */
    public function stateHashBefore(): string
    {
        return substr($this->bytes, 24, 32);
    }

    /** SHA-256 of the state after rollback. */
    public function stateHashAfter(): string
    {
        return substr($this->bytes, 56, 32);
    }

    /** The signature to share with peers. */
    public function signature(): string
    {
        return substr($this->bytes, 88, 32);
    }

    public function proofHash(): string
    {
        return substr($this->bytes, 120, 32);
    }

    /** The quarantine record's index. */
    public function quarantineIndex(): int|string
    {
        return Native::u64Out(unpack("P", substr($this->bytes, 152, 8))[1]);
    }

    /** Recompute the binding hash (integrity only). */
    public function isValid(): bool
    {
        $rc = Native::ffi15()->zxv_paradox_repair_verify(Native::fixed($this->bytes, "proof", Paradox::REPAIR_BYTES));
        if ($rc === Status::E_VERIFY) {
            return false;
        }
        Native::check($rc);
        return true;
    }

    public function equals(?self $other): bool
    {
        return $other !== null && $other->bytes === $this->bytes;
    }

    private function u32(int $off): int
    {
        return unpack("V", substr($this->bytes, $off, 4))[1];
    }
}
