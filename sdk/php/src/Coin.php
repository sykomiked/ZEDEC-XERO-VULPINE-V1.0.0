<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

use Zedec\Zxv\Exception\ZxvUsageException;

/**
 * ABI 1.5: Fibonacci coins, the VFV ladder (zxv_coin_*).
 *
 * Rung r (1..RUNGS) has face F(r+1): 1, 2, 3, 5, 8, 13, ... up to TOP_FACE
 * (rung 45); rung 0 is the ceremonial V0 note, worth nothing. Any other
 * face (4, 6, 100, ...) is not a coin (ZXV_E_DENOM). To pay an arbitrary
 * amount, split() it.
 *
 *     Coin::split(100);   // [10, 5, 3]  (89 + 8 + 3)
 *
 * Amounts are uint64: an int, or a decimal string past PHP_INT_MAX. Pure
 * and integer-only. Honest limit: minting coins is not in this ABI;
 * TokenWallet is the only minting surface.
 */
final class Coin
{
    public const RUNGS = 45;
    /** Face of rung 45: F(46). */
    public const TOP_FACE = 1836311903;
    /** Default ceiling on the coins split() builds. */
    public const MAX_SPLIT = 16777216;

    private function __construct()
    {
    }

    /**
     * The face of rung $rung (0..RUNGS).
     * @throws ZxvUsageException ZXV_E_RANGE (rung > 45)
     */
    public static function face(int $rung): int
    {
        [$out, $outP] = Native::out("uint64_t");
        Native::check(Native::ffi15()->zxv_coin_face(Native::uint32($rung, "rung"), $outP));
        return $out->cdata;
    }

    /**
     * The rung of a coin face.
     * @throws ZxvUsageException ZXV_E_DENOM (not a face)
     */
    public static function rung(int|string|float $face): int
    {
        [$out, $outP] = Native::out("uint32_t");
        Native::check(Native::ffi15()->zxv_coin_rung(Native::u64($face, "face"), $outP));
        return $out->cdata;
    }

    /** Whether $face is a coin face. */
    public static function isFace(int|string|float $face): bool
    {
        try {
            self::rung($face);
            return true;
        } catch (ZxvUsageException $e) {
            if ($e->getStatus() === Status::E_DENOM) {
                return false;
            }
            throw $e;
        }
    }

    /**
     * The coins for a whole amount: its unique fewest-coin split into rungs,
     * largest first (amounts at or above TOP_FACE take rung-45 coins first).
     * @param int $maxCoins refuse (ZXV_E_RANGE) rather than build more coins
     *   than this; count large splits with splitCount()
     * @return list<int> rungs
     * @throws ZxvUsageException ZXV_E_RANGE (more than $maxCoins, or more
     *   than 4,294,967,294 coins: amounts of about 7.9e18 and up)
     */
    public static function split(int|string|float $amount, int $maxCoins = self::MAX_SPLIT): array
    {
        $a = Native::u64($amount, "amount");
        $count = self::splitCount($a === -1 ? "18446744073709551615" : Native::u64Out($a));
        if ($count === 0) {
            return [];
        }
        if ($count > $maxCoins) {
            throw new ZxvUsageException(Status::E_RANGE, "a split of $amount needs $count coins (maxCoins $maxCoins); use splitCount()");
        }
        $buf = Native::new("uint32_t[$count]");
        [$n, $nP] = Native::out("size_t");
        Native::check(Native::ffi15()->zxv_coin_split($a, $buf, $count, $nP));
        $out = [];
        for ($i = 0; $i < $n->cdata; $i++) {
            $out[] = $buf[$i];
        }
        return $out;
    }

    /** How many coins split($amount) gives, without building them. */
    public static function splitCount(int|string|float $amount): int
    {
        [$n, $nP] = Native::out("size_t");
        $rc = Native::ffi15()->zxv_coin_split(Native::u64($amount, "amount"), null, 0, $nP);
        if ($rc !== Status::E_BUFFER) {
            Native::check($rc);
        }
        return $n->cdata;
    }
}
