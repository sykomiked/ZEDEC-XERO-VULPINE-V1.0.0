<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

use FFI\CData;
use Zedec\Zxv\Exception\ZxvUsageException;

/**
 * ABI 1.5: op streams, a freight payload of ops that the receiver expands
 * (zxv_freight_ops_*).
 *
 * Format: "ZXOP", version, flags, the declared total (u64), an optional
 * SHA-256 of the output, then ops: LITERAL bytes; COPY (an LZ77-style back
 * reference, overlap allowed); CIDREF (bytes the receiver resolves by
 * content id, through a callback); SEED (a built-in deterministic
 * generator, GENERATORS). Expansion checks the declared total against the
 * receiver's limit before writing a byte.
 *
 *     $w = new FreightOpsWriter(12);
 *     $w->literal("ab")->copy(2, 10);
 *     FreightOps::expand($w->finish(), 12);   // "abababababab"
 *
 * Honest limits: no size guarantee. Random data encodes slightly LARGER
 * than itself; COPY helps only data that repeats; CIDREF only names bytes
 * the receiver must already hold or fetch; SEED only reproduces output of
 * the built-in generators. No information is created.
 */
final class FreightOps
{
    public const CID_MAX = 128;
    public const GEN_PARAM_MAX = 32;
    /** Largest op stream or expansion handled here (64 MiB). */
    public const MAX = 67108864;
    /**
     * SEED generators: xorshift (8 bytes: u64 LE seed, nonzero), repeat
     * (1..32 bytes repeated), fibword (2 distinct bytes: the Fibonacci
     * word), sierpinski (3 bytes: log2 width 1..15, on, off).
     */
    public const GENERATORS = ["xorshift" => 1, "repeat" => 2, "fibword" => 3, "sierpinski" => 4];

    private function __construct()
    {
    }

    /**
     * A stream's declared total and whether it carries an output SHA-256, without expanding.
     * @return array{0:int|string,1:bool}
     * @throws ZxvUsageException ZXV_E_FORMAT
     */
    public static function peek(string $ops): array
    {
        [$t, $tP] = Native::out("uint64_t");
        [$s, $sP] = Native::out("int32_t");
        Native::check(Native::ffi15()->zxv_freight_ops_peek(Native::bytes($ops, true), strlen($ops), $tP, $sP));
        return [Native::u64Out($t->cdata), $s->cdata !== 0];
    }

    /**
     * Expand an op stream. $resolver resolves CIDREF ops: it receives
     * (string $cidBytes, int $length) and returns a string of exactly
     * $length bytes, or null if it cannot. It runs on the calling thread
     * during this call only and must not expand another stream; any
     * exception it throws is caught and treated as "cannot resolve" (it
     * never unwinds through C). PHP's FFI keeps each callback trampoline
     * until the request ends.
     * @param int $limit the most bytes you accept (<= MAX)
     * @param ?callable(string, int): ?string $resolver
     * @throws ZxvUsageException ZXV_E_TOO_LONG (declared total above $limit, checked first), ZXV_E_FORMAT, ZXV_E_RANGE (limit above MAX)
     * @throws Exception\ZxvUsageException ZXV_E_NOT_FOUND (a CIDREF was not resolved)
     * @throws Exception\VerificationFailedException the output fails the stream's SHA-256
     */
    public static function expand(string $ops, int $limit, ?callable $resolver = null): string
    {
        $lim = Native::uint($limit, "limit");
        [$total] = self::peek($ops);
        $size = !is_int($total) || $total > $lim ? 0 : $total;
        $cb = null;
        if ($resolver !== null) {
            // Held in this frame for the whole call: the C side uses it only during it.
            $cb = static function ($user, $cid, $cidLen, $out, $len) use ($resolver): int {
                try {
                    $bytes = $resolver($cidLen === 0 ? "" : \FFI::string($cid, $cidLen), $len);
                    if (!is_string($bytes) || strlen($bytes) !== $len) {
                        return 1;
                    }
                    if ($len > 0) {
                        \FFI::memcpy($out, $bytes, $len);
                    }
                    return Status::OK;
                } catch (\Throwable) {
                    return 1;
                }
            };
        }
        $buf = $size === 0 ? null : Native::new("uint8_t[$size]");
        [$n, $nP] = Native::out("size_t");
        $rc = Native::ffi15()->zxv_freight_ops_expand(Native::bytes($ops, true), strlen($ops), $lim, $cb, null, $buf, $size, $nP);
        Native::check($rc);
        return $buf === null ? "" : Native::read($buf, $n->cdata);
    }

    /**
     * Run one SEED generator directly.
     * @throws ZxvUsageException ZXV_E_ARG (unknown generator or params it refuses)
     */
    public static function generate(string|int $gen, string $params, int $length): string
    {
        $n = Native::int($length, "length", 0, self::MAX);
        $out = $n === 0 ? null : Native::new("uint8_t[$n]");
        Native::check(Native::ffi15()->zxv_freight_gen_run(self::gen($gen), Native::bytes($params, true), strlen($params), $out, $n));
        return $out === null ? "" : Native::read($out, $n);
    }

    /** Greedy LZ77 encoding of $data into a LITERAL/COPY op stream (may be LARGER than the input). */
    public static function encode(string $data, bool $withSha = true): string
    {
        $d = Native::bytes($data, true);
        $ws = $withSha ? 1 : 0;
        $hint = self::bound(strlen($data), $withSha);
        return Native::bytesOut(fn ($b, $c, $l) => Native::ffi15()->zxv_freight_ops_encode($d, strlen($data), $ws, $b, $c, $l), is_int($hint) ? $hint : 1);
    }

    /** Worst-case encoded size of $n input bytes. */
    public static function bound(int|string|float $n, bool $withSha = true): int|string
    {
        [$o, $oP] = Native::out("uint64_t");
        Native::check(Native::ffi15()->zxv_freight_ops_bound(Native::u64($n, "n"), $withSha ? 1 : 0, $oP));
        return Native::u64Out($o->cdata);
    }

    /** @internal a GENERATORS name or 0..255 */
    public static function gen(string|int $gen): int
    {
        if (is_int($gen)) {
            return Native::uint8($gen, "gen");
        }
        return self::GENERATORS[$gen] ?? throw new ZxvUsageException(Status::E_ARG, "unknown generator " . var_export($gen, true));
    }
}
