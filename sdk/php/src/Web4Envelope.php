<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

use Zedec\Zxv\Exception\VerificationFailedException;

/**
 * ABI 1.5: a decoded Web 4 envelope, an immutable value. Decoding does not
 * check the signature: call verify() with the sender's public key. Replay,
 * timestamp and rate checks are the receiver's job.
 */
final class Web4Envelope
{
    public readonly string $bytes;
    /** A Web4::KINDS name (or the raw int). */
    public readonly string|int $kind;
    /** MF_* bits. */
    public readonly int $flags;
    public readonly string $from;
    public readonly string $to;
    public readonly int|string $seq;
    public readonly int|string $tsMs;
    public readonly int|string $reqId;
    public readonly int $chunk;
    public readonly string $tool;
    public readonly string $payload;
    /** The attached consent token, or null. */
    public readonly ?string $consent;

    /** @throws Exception\ZxvUsageException ZXV_E_FORMAT */
    public function __construct(string $bytes)
    {
        $m = Native::bytes($bytes, true);
        [$k, $kP] = Native::out("int32_t");
        [$f, $fP] = Native::out("uint32_t");
        $from = Native::new("uint8_t[32]");
        $to = Native::new("uint8_t[32]");
        [$sq, $sqP] = Native::out("uint64_t");
        [$ts, $tsP] = Native::out("uint64_t");
        [$rq, $rqP] = Native::out("uint64_t");
        [$ch, $chP] = Native::out("uint32_t");
        $tool = Native::new("char[32]");
        [$pl, $plP] = Native::out("uint32_t");
        Native::check(Native::ffi15()->zxv_w4_env_decode($m, strlen($bytes), $kP, $fP, $from, $to, $sqP, $tsP, $rqP, $chP, $tool, $plP));
        $this->bytes = $bytes;
        $this->kind = array_search($k->cdata, Web4::KINDS, true) ?: $k->cdata;
        $this->flags = $f->cdata;
        $this->from = Native::read($from, 32);
        $this->to = Native::read($to, 32);
        $this->seq = Native::u64Out($sq->cdata);
        $this->tsMs = Native::u64Out($ts->cdata);
        $this->reqId = Native::u64Out($rq->cdata);
        $this->chunk = $ch->cdata;
        $this->tool = \FFI::string($tool);
        $this->payload = Native::bytesOut(fn ($b, $c, $l) => Native::ffi15()->zxv_w4_env_payload($m, strlen($bytes), $b, $c, $l), max($pl->cdata, 1));
        $tok = Native::new("uint8_t[" . Web4::CONSENT_BYTES . "]");
        $rc = Native::ffi15()->zxv_w4_env_consent($m, strlen($bytes), $tok);
        if ($rc !== Status::E_NOT_FOUND) {
            Native::check($rc);
        }
        $this->consent = $rc === Status::OK ? Native::read($tok, Web4::CONSENT_BYTES) : null;
    }

    public function isFinal(): bool
    {
        return ($this->flags & Web4::MF_FINAL) !== 0;
    }

    /** Whether $senderPublicKey is the sender's (from == its SHA3-256) and the signature verifies. */
    public function verify(string $senderPublicKey): bool
    {
        $rc = Native::ffi15()->zxv_w4_env_verify(Native::bytes($this->bytes, true), strlen($this->bytes), Native::fixed($senderPublicKey, "senderPublicKey", Web4::PK_BYTES));
        if ($rc === Status::E_VERIFY) {
            return false;
        }
        Native::check($rc);
        return true;
    }

    /** Like verify(), but throws VerificationFailedException. */
    public function verifyOrThrow(string $senderPublicKey): self
    {
        if (!$this->verify($senderPublicKey)) {
            throw new VerificationFailedException(Status::E_VERIFY, "envelope signature did not verify");
        }
        return $this;
    }

    public function equals(?self $other): bool
    {
        return $other !== null && $other->bytes === $this->bytes;
    }
}
