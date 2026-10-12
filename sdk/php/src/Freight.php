<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

use Zedec\Zxv\Exception\ZxvUsageException;

/**
 * ABI 1.5: freight packets (zxv_freight_*).
 *
 * A FREIGHT carries up to MAX_PAYLOAD payload bytes as ROWS smart packets
 * of PACKET_BYTES bytes (one UBH-168 frame each): byte 0 is the row index,
 * bytes 1..20 row data. Rows 0..k-1 hold the payload verbatim; rows
 * k..167 are Reed-Solomon parity. ANY k distinct rows rebuild the payload,
 * so up to 168 - k rows may be lost.
 *
 * The 64-byte FreightHeader travels separately. A larger payload is split()
 * into a set of freights that share a freight id and is reassembled by a
 * FreightJoin. The payload may be an op stream (FreightOps).
 *
 *     $h = FreightHeader::forPayload($data, 42, 10);
 *     $packets = Freight::encode($h, $data);                 // 3528 bytes
 *     $rows = array_slice(Freight::rows($packets), 50, 10);  // lose 158 rows
 *     Freight::decode($h, $rows) === $data;                  // true
 *
 * Pure: no context, no capability.
 *
 * Honest limits: erasure coding only. A lost row is recovered; a CORRUPTED
 * row is detected (the payload SHA-256 fails: VerificationFailedException)
 * but not located or repaired, so drop rows you distrust and decode again.
 * The header is neither protected by the code nor authenticated: sign it
 * (or the outer message) if the sender matters. A freight is larger than
 * its payload (3528 wire bytes for at most 3360).
 */
final class Freight
{
    public const PACKET_BYTES = 21;
    public const ROW_PAYLOAD = 20;
    public const ROWS = 168;
    /** Bytes of one encoded freight (168 * 21). */
    public const BYTES = 3528;
    public const MAX_PAYLOAD = 3360;
    public const HEADER_BYTES = 64;
    public const VERSION = 1;
    public const MAX_SET = 16384;
    public const SET_MAX_BYTES = 55050240;
    /** Header flag: the (set's) payload is an op stream. */
    public const F_OPSTREAM = 0x01;

    private function __construct()
    {
    }

    /**
     * Encode the 168 packets (rows in order) of the freight $header describes.
     * @return string BYTES bytes
     * @throws Exception\VerificationFailedException the payload does not match the header's hash
     */
    public static function encode(FreightHeader|string $header, string $payload): string
    {
        $h = FreightHeader::coerce($header);
        $out = Native::new("uint8_t[" . self::BYTES . "]");
        Native::check(Native::ffi15()->zxv_freight_encode($h->mem(), Native::bytes($payload, true), strlen($payload), $out));
        return Native::read($out, self::BYTES);
    }

    /** @return list<string> encoded packets split into their 21-byte rows */
    public static function rows(string $packets): array
    {
        if (strlen($packets) % self::PACKET_BYTES !== 0) {
            throw new ZxvUsageException(Status::E_ARG, "packets must be a multiple of " . self::PACKET_BYTES . " bytes");
        }
        return $packets === "" ? [] : str_split($packets, self::PACKET_BYTES);
    }

    /**
     * Rebuild the payload from whichever packets arrived, in any order
     * (duplicates and row indices above 167 are ignored).
     * @param string|list<string> $packets concatenated 21-byte packets, or a list of them
     * @throws Exception\ZxvRefusedException ZXV_E_ERASURE (fewer than k distinct rows)
     * @throws Exception\VerificationFailedException a corrupted row, or a header from another freight
     */
    public static function decode(FreightHeader|string $header, string|array $packets): string
    {
        $h = FreightHeader::coerce($header);
        $pk = self::packetsBytes($packets);
        $hm = $h->mem();
        $pm = Native::bytes($pk, true);
        return Native::bytesOut(fn ($b, $c, $l) => Native::ffi15()->zxv_freight_decode($hm, $pm, strlen($pk), $b, $c, $l), max($h->payloadLen, 1));
    }

    /** Freights needed to carry $len bytes at $k. @throws ZxvUsageException ZXV_E_TOO_LONG */
    public static function setCount(int|string|float $len, int $k): int
    {
        [$o, $oP] = Native::out("uint32_t");
        Native::check(Native::ffi15()->zxv_freight_set_count(Native::u64($len, "len"), Native::uint8($k, "k"), $oP));
        return $o->cdata;
    }

    /**
     * Member $seq of the set carrying $data.
     * @return array{0:FreightHeader,1:string} [header, packets]
     * @throws ZxvUsageException ZXV_E_RANGE (seq not below the set count)
     */
    public static function splitMember(string $data, int $seq, int|string|float $freightId, int $k, bool $opstream = false, int|string|float $expandLimit = 0): array
    {
        return self::member(Native::bytes($data, true), strlen($data), Native::uint32($seq, "seq"), $freightId, $k, $opstream, $expandLimit);
    }

    /** @return list<array{0:FreightHeader,1:string}> every member of the set carrying $data */
    public static function split(string $data, int|string|float $freightId, int $k, bool $opstream = false, int|string|float $expandLimit = 0): array
    {
        $m = Native::bytes($data, true);
        $n = self::setCount(strlen($data), $k);
        $out = [];
        for ($s = 0; $s < $n; $s++) {
            $out[] = self::member($m, strlen($data), $s, $freightId, $k, $opstream, $expandLimit);
        }
        return $out;
    }

    /** @internal @param string|list<string> $packets */
    public static function packetsBytes(string|array $packets): string
    {
        if (is_string($packets)) {
            return $packets;
        }
        $s = "";
        foreach (array_values($packets) as $i => $p) {
            $s .= Native::exact($p, "packets[$i]", self::PACKET_BYTES);
        }
        return $s;
    }

    /** @return array{0:FreightHeader,1:string} */
    private static function member(?\FFI\CData $mem, int $len, int $seq, int|string|float $freightId, int $k, bool $opstream, int|string|float $expandLimit): array
    {
        $h = Native::new("uint8_t[" . self::HEADER_BYTES . "]");
        $p = Native::new("uint8_t[" . self::BYTES . "]");
        Native::check(Native::ffi15()->zxv_freight_split(Native::u64($freightId, "freightId"), Native::uint8($k, "k"), $opstream ? self::F_OPSTREAM : 0,
            Native::u64($expandLimit, "expandLimit"), $mem, $len, $seq, $h, $p));
        return [new FreightHeader(Native::read($h, self::HEADER_BYTES)), Native::read($p, self::BYTES)];
    }
}
