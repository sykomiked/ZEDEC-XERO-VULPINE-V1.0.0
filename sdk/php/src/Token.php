<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

use FFI\CData;
use Zedec\Zxv\Exception\ZxvUsageException;

/**
 * ABI 1.5: swarm AI token denominations and octane (zxv_token_*).
 *
 * Tier k (0..63) has face F(k+2): 1, 2, 3, 5, 8, ... Tier 0 is the plain
 * token; tiers SUPER_TIER (face 144) and up are super tokens. A HOLDING is
 * a list of 64 counts (tokens per tier; a shorter list is padded with
 * zeros); its value is the sum of counts[k] * face(k). OCTANE is the
 * densest tier a holding may use (OCTANES names, or 0..63). The canonical
 * form of a value at an octane is unique.
 *
 *     $h = Token::canonical(1000000, "racing");   // 17 pieces
 *     Token::value($h);                           // [1000000, 17]
 *
 * NO PREMIUM: a dense token is worth exactly its face; density earns no
 * interest, costs no fee and buys no priority. Counts and values are uint64
 * (an int, or a decimal string past PHP_INT_MAX). Arrays passed in are
 * never modified. A value past 64 bits is ZxvUsageException (ZXV_E_RANGE).
 */
final class Token
{
    public const TIERS = 64;
    public const SUPER_TIER = 10;
    public const OCTANES = ["plain" => 0, "regular" => 4, "midgrade" => 9, "premium" => 14, "racing" => 23, "max" => 63];
    public const CODE_MAX_BITS = 93;

    private function __construct()
    {
    }

    /** The face of $tier (0..63). @throws ZxvUsageException ZXV_E_RANGE */
    public static function face(int $tier): int
    {
        [$out, $outP] = Native::out("uint64_t");
        Native::check(Native::ffi15()->zxv_token_face(Native::uint32($tier, "tier"), $outP, null));
        return $out->cdata;
    }

    /** Whether $tier is a super token (face 144 and up). */
    public static function isSuper(int $tier): bool
    {
        [$out, $outP] = Native::out("int32_t");
        Native::check(Native::ffi15()->zxv_token_face(Native::uint32($tier, "tier"), null, $outP));
        return $out->cdata === 1;
    }

    /** The tier of a token face. @throws ZxvUsageException ZXV_E_DENOM */
    public static function tier(int|string|float $face): int
    {
        [$out, $outP] = Native::out("uint32_t");
        Native::check(Native::ffi15()->zxv_token_tier(Native::u64($face, "face"), $outP));
        return $out->cdata;
    }

    /**
     * The value and piece count of a holding.
     * @param list<int|string> $counts
     * @return array{0:int|string,1:int|string} [value, pieces]
     */
    public static function value(array $counts): array
    {
        [$v, $vP] = Native::out("uint64_t");
        [$p, $pP] = Native::out("uint64_t");
        Native::check(Native::ffi15()->zxv_token_value(self::holding($counts), $vP, $pP));
        return [Native::u64Out($v->cdata), Native::u64Out($p->cdata)];
    }

    /**
     * The canonical holding of $value at $octane.
     * @return list<int|string> 64 counts
     */
    public static function canonical(int|string|float $value, string|int $octane): array
    {
        $out = Native::new("uint64_t[" . self::TIERS . "]");
        Native::check(Native::ffi15()->zxv_token_canonical(Native::u64($value, "value"), self::octane($octane), $out));
        return self::counts($out);
    }

    /** @param list<int|string> $counts whether it is the canonical form of its value at $octane */
    public static function isCanonical(array $counts, string|int $octane): bool
    {
        [$out, $outP] = Native::out("int32_t");
        Native::check(Native::ffi15()->zxv_token_is_canonical(self::holding($counts), self::octane($octane), $outP));
        return $out->cdata === 1;
    }

    /**
     * $counts rewritten canonically at $octane (the value never changes).
     * @param list<int|string> $counts
     * @return list<int|string>
     */
    public static function compress(array $counts, string|int $octane): array
    {
        $o = self::octane($octane);
        return self::inPlace($counts, fn (CData $c) => Native::ffi15()->zxv_token_compress($c, $o));
    }

    /**
     * Fuse one tier-k and one tier-(k+1) token into one tier-(k+2) token.
     * @param list<int|string> $counts
     * @return list<int|string>
     * @throws Exception\InsufficientFundsException no tier-k or tier-(k+1) token
     * @throws Exception\ZxvRefusedException ZXV_E_STATE (tier k+2 denser than the octane)
     * @throws ZxvUsageException ZXV_E_RANGE (tier > 61, or a count is full)
     */
    public static function fuse(array $counts, int $tier, string|int $octane): array
    {
        $o = self::octane($octane);
        $t = Native::uint32($tier, "tier");
        return self::inPlace($counts, fn (CData $c) => Native::ffi15()->zxv_token_fuse($c, $o, $t));
    }

    /**
     * Split one tier-k token into one tier-(k-1) and one tier-(k-2) token.
     * @param list<int|string> $counts
     * @return list<int|string>
     * @throws Exception\InsufficientFundsException no tier-k token
     * @throws ZxvUsageException ZXV_E_RANGE (tier 0 or >= 64)
     */
    public static function split(array $counts, int $tier): array
    {
        $t = Native::uint32($tier, "tier");
        return self::inPlace($counts, fn (CData $c) => Native::ffi15()->zxv_token_split($c, $t));
    }

    /** Bits in the Fibonacci code of $value (2..93). @throws ZxvUsageException ZXV_E_RANGE (2^64-1) */
    public static function codeBits(int|string|float $value): int
    {
        [$out, $outP] = Native::out("uint32_t");
        Native::check(Native::ffi15()->zxv_token_code_bits(Native::u64($value, "value"), $outP));
        return $out->cdata;
    }

    /**
     * Encode $values as consecutive Fibonacci codes (Zeckendorf bits of
     * value+1, lowest first, then a closing 1). Bit i is bit i%8 of byte i/8.
     * @param list<int|string> $values each 0..2^64-2
     * @return array{0:string,1:int} [bytes, bit count]
     */
    public static function encodeCodes(array $values): array
    {
        $vals = array_map(fn ($v) => Native::u64($v, "value"), $values);
        $bits = 0;
        foreach ($values as $v) {
            $bits += self::codeBits($v);
        }
        $n = intdiv($bits + 7, 8);
        $buf = Native::new("uint8_t[" . max($n, 1) . "]");
        [$pos, $posP] = Native::out("uint32_t");
        foreach ($vals as $v) {
            Native::check(Native::ffi15()->zxv_token_code_put($buf, $n, $posP, $v));
        }
        return [Native::read($buf, $n), $pos->cdata];
    }

    /**
     * Decode Fibonacci codes from $bytes, starting at bit $bit.
     * @param ?int $count how many values (null: until $bits are used)
     * @param ?int $bits meaningful bits (default all)
     * @return array{0:list<int|string>,1:int} [values, next bit]
     * @throws ZxvUsageException ZXV_E_FORMAT (no closing "11", or past 64 bits), ZXV_E_RANGE ($bit past the end)
     */
    public static function decodeCodes(string $bytes, ?int $count = null, int $bit = 0, ?int $bits = null): array
    {
        $limit = $bits ?? strlen($bytes) * 8;
        $m = Native::bytes($bytes);
        [$pos, $posP] = Native::out("uint32_t");
        $pos->cdata = Native::uint32($bit, "bit");
        [$v, $vP] = Native::out("uint64_t");
        $out = [];
        while ($count !== null ? count($out) < $count : $pos->cdata < $limit) {
            Native::check(Native::ffi15()->zxv_token_code_get($m, strlen($bytes), $posP, $vP));
            $out[] = Native::u64Out($v->cdata);
        }
        return [$out, $pos->cdata];
    }

    /** @internal an OCTANES name or 0..63 */
    public static function octane(string|int $octane): int
    {
        if (is_int($octane)) {
            return Native::uint32($octane, "octane");
        }
        return self::OCTANES[$octane] ?? throw new ZxvUsageException(Status::E_ARG, "unknown octane " . var_export($octane, true));
    }

    /**
     * @internal a holding in a fresh uint64_t[64]
     * @param list<int|string> $counts
     */
    public static function holding(array $counts): CData
    {
        if (count($counts) > self::TIERS) {
            throw new ZxvUsageException(Status::E_ARG, "counts has more than " . self::TIERS . " tiers");
        }
        $c = Native::new("uint64_t[" . self::TIERS . "]");
        foreach (array_values($counts) as $i => $n) {
            $c[$i] = Native::u64($n, "counts[$i]");
        }
        return $c;
    }

    /** @internal @return list<int|string> */
    public static function counts(CData $c): array
    {
        $out = [];
        for ($i = 0; $i < self::TIERS; $i++) {
            $out[] = Native::u64Out($c[$i]);
        }
        return $out;
    }

    /**
     * @param list<int|string> $counts
     * @param callable(CData): int $fn
     * @return list<int|string>
     */
    private static function inPlace(array $counts, callable $fn): array
    {
        $c = self::holding($counts);
        Native::check($fn($c));
        return self::counts($c);
    }
}
