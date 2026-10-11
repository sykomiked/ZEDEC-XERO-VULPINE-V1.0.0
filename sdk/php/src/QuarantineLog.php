<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

use Zedec\Zxv\Exception\ZxvUsageException;

/**
 * The quarantine log of ABI 1.3 (zxv_hexlog): an append-only, SHA-256
 * hash-chained record of forbidden work, read for audit and never
 * replayed. Creating and appending need the ATTEST capability.
 *
 * Record k's hash is SHA-256("zxv/lpx-qlog/v1" || seq (u64 LE) || tag ||
 * reason || submitter (u32 LE) || subject || prev), so the chain can be
 * recomputed from the records. A full log refuses the append with
 * ZXV_E_CAPACITY (ZxvRefusedException); the caller must then halt the
 * context (Context::halt), since forbidden work it cannot record must not
 * continue. Signing head() is the caller's job.
 *
 * @implements \IteratorAggregate<int, QuarantineRecord>
 */
final class QuarantineLog extends NativeHandle implements \Countable, \IteratorAggregate
{
    /** Largest capacity (ZXV_HEXLOG_MAX_RECORDS). */
    public const MAX_RECORDS = 65536;

    /**
     * @param Capability $capability needs ATTEST
     * @param int $capacity 1..MAX_RECORDS
     */
    public function __construct(public readonly Capability $capability, int $capacity)
    {
        $out = Native::new("zxv_hexlog*");
        Native::check(Native::ffi()->zxv_hexlog_create($capability->ptr(), Native::uint32($capacity, "capacity"), \FFI::addr($out)), $capability->context);
        parent::__construct($capability->context, $out, "zxv_hexlog_destroy");
    }

    /**
     * Quarantine forbidden work; returns the record's sequence number.
     *
     * @param Hexagram $tag a forbidden tag (any other is ZXV_E_ARG)
     * @param int $reason 0..255
     * @param int $submitter 0..4294967295
     * @param string $subject a 32-byte digest of the work
     */
    public function append(Hexagram $tag, int $reason, int $submitter, string $subject): int
    {
        Native::int($reason, "reason", 0, 255);
        $seq = Native::new("uint64_t");
        Native::check(Native::ffi()->zxv_hexlog_append(
            $this->capability->ptr(),
            $this->ptr(),
            $tag->tag,
            $reason,
            Native::uint32($submitter, "submitter"),
            Native::bytes(Native::exact($subject, "subject", 32)),
            \FFI::addr($seq),
        ), $this->context);
        return $seq->cdata;
    }

    /** Number of records. */
    public function count(): int
    {
        $out = Native::new("uint32_t");
        Native::check(Native::ffi()->zxv_hexlog_count($this->ptr(), \FFI::addr($out)), $this->context);
        return $out->cdata;
    }

    /** The chain head: the last record's hash (32 zero bytes when empty). */
    public function head(): string
    {
        $out = Native::new("uint8_t[32]");
        Native::check(Native::ffi()->zxv_hexlog_head($this->ptr(), $out), $this->context);
        return Native::read($out, 32);
    }

    /** Record $index, oldest first. */
    public function record(int $index): QuarantineRecord
    {
        $seq = Native::new("uint64_t");
        $tag = Native::new("uint8_t");
        $reason = Native::new("uint8_t");
        $who = Native::new("uint32_t");
        $subj = Native::new("uint8_t[32]");
        $prev = Native::new("uint8_t[32]");
        $hash = Native::new("uint8_t[32]");
        Native::check(Native::ffi()->zxv_hexlog_at(
            $this->ptr(),
            Native::uint32($index, "index"),
            \FFI::addr($seq),
            \FFI::addr($tag),
            \FFI::addr($reason),
            \FFI::addr($who),
            $subj,
            $prev,
            $hash,
        ), $this->context);
        return new QuarantineRecord(
            $seq->cdata,
            Hexagram::fromTag($tag->cdata),
            $reason->cdata,
            $who->cdata,
            Native::read($subj, 32),
            Native::read($prev, 32),
            Native::read($hash, 32),
        );
    }

    /** @return \Generator<int, QuarantineRecord> */
    public function getIterator(): \Generator
    {
        $n = $this->count();
        for ($i = 0; $i < $n; $i++) {
            yield $i => $this->record($i);
        }
    }

    /**
     * Recompute every link.
     *
     * @throws Exception\ZxvFatalError (ZXV_E_INVARIANT) if the chain is broken; the context halts
     */
    public function verify(): void
    {
        Native::check(Native::ffi()->zxv_hexlog_verify($this->ptr()), $this->context);
    }
}
