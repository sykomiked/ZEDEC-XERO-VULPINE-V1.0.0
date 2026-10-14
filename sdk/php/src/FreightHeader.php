<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

use FFI\CData;

/**
 * ABI 1.5: a freight header, an immutable value: the serialized 64 bytes
 * and their fields. Constructing one parses and checks the bytes
 * (ZxvUsageException ZXV_E_FORMAT on bad magic, reserved bytes or fields).
 */
final class FreightHeader
{
    public readonly string $bytes;
    public readonly int $version;
    /** Data rows, 1..168. */
    public readonly int $k;
    /** 0 or Freight::F_OPSTREAM. */
    public readonly int $flags;
    public readonly int $payloadLen;
    public readonly int $seq;
    public readonly int $seqCount;
    /** uint64 (an int, or a decimal string past PHP_INT_MAX). */
    public readonly int|string $freightId;
    /** The receiver's expansion limit (op streams only; else 0). */
    public readonly int|string $expandLimit;
    /** SHA-256 of the payload, 32 bytes. */
    public readonly string $payloadSha256;

    public function __construct(string $bytes)
    {
        $b = Native::fixed($bytes, "header", Freight::HEADER_BYTES);
        $o = [];
        foreach (["uint8_t", "uint8_t", "uint8_t", "uint16_t", "uint16_t", "uint16_t", "uint64_t", "uint64_t"] as $t) {
            $o[] = Native::out($t);
        }
        $sha = Native::new("uint8_t[32]");
        Native::check(Native::ffi15()->zxv_freight_header_parse($b, $o[0][1], $o[1][1], $o[2][1], $o[3][1], $o[4][1], $o[5][1], $o[6][1], $o[7][1], $sha));
        $this->bytes = $bytes;
        $this->version = $o[0][0]->cdata;
        $this->k = $o[1][0]->cdata;
        $this->flags = $o[2][0]->cdata;
        $this->payloadLen = $o[3][0]->cdata;
        $this->seq = $o[4][0]->cdata;
        $this->seqCount = $o[5][0]->cdata;
        $this->freightId = Native::u64Out($o[6][0]->cdata);
        $this->expandLimit = Native::u64Out($o[7][0]->cdata);
        $this->payloadSha256 = Native::read($sha, 32);
    }

    /**
     * The header of a single freight (seq 0 of 1) carrying $payload with $k
     * data rows; it hashes the payload.
     * @throws Exception\ZxvUsageException ZXV_E_ARG (bad k, expandLimit without opstream), ZXV_E_TOO_LONG
     */
    public static function forPayload(string $payload, int|string|float $freightId, int $k, bool $opstream = false, int|string|float $expandLimit = 0): self
    {
        $out = Native::new("uint8_t[" . Freight::HEADER_BYTES . "]");
        Native::check(Native::ffi15()->zxv_freight_header_init(Native::u64($freightId, "freightId"), Native::uint8($k, "k"), $opstream ? Freight::F_OPSTREAM : 0,
            Native::u64($expandLimit, "expandLimit"), Native::bytes($payload, true), strlen($payload), $out));
        return new self(Native::read($out, Freight::HEADER_BYTES));
    }

    /** Serialize a header from its fields (version is always Freight::VERSION). */
    public static function build(int $k, int $payloadLen, int $seq, int $seqCount, int|string|float $freightId, string $payloadSha256, int $flags = 0, int|string|float $expandLimit = 0): self
    {
        $sha = Native::fixed($payloadSha256, "payloadSha256", 32);
        $out = Native::new("uint8_t[" . Freight::HEADER_BYTES . "]");
        Native::check(Native::ffi15()->zxv_freight_header_serialize(Native::uint8($k, "k"), Native::uint8($flags, "flags"), Native::uint16($payloadLen, "payloadLen"),
            Native::uint16($seq, "seq"), Native::uint16($seqCount, "seqCount"), Native::u64($freightId, "freightId"), Native::u64($expandLimit, "expandLimit"), $sha, $out));
        return new self(Native::read($out, Freight::HEADER_BYTES));
    }

    /**
     * Check serialized header bytes without decoding them.
     * @throws Exception\ZxvUsageException ZXV_E_FORMAT (bad magic, reserved bytes or fields), ZXV_E_ARG (not 64 bytes)
     */
    public static function check(string $bytes): void
    {
        Native::check(Native::ffi15()->zxv_freight_header_check(Native::fixed($bytes, "header", Freight::HEADER_BYTES)));
    }

    /** @internal */
    public static function coerce(self|string $v): self
    {
        return $v instanceof self ? $v : new self($v);
    }

    public function isOpstream(): bool
    {
        return ($this->flags & Freight::F_OPSTREAM) !== 0;
    }

    public function equals(?self $other): bool
    {
        return $other !== null && $other->bytes === $this->bytes;
    }

    /** @internal */
    public function mem(): CData
    {
        return Native::fixed($this->bytes, "header", Freight::HEADER_BYTES);
    }
}
