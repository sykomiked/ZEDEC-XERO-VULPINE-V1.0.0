<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

use Zedec\Zxv\Exception\ZxvUsageException;

/**
 * A UBH-168 harmonic wire frame: 21 octets = tag, then five 32-bit words in
 * fixed L-B-L-B-L order: W0 little-endian (S+), W1 big-endian (S-), W2 LE
 * (S+), W3 BE (S-), W4 LE (S+). Identical on every host.
 *
 * Packing and unpacking are pure PHP (no native library needed);
 * nativeBytes() cross-checks against libzxv.
 */
final class Ubh168Frame
{
    public const BYTES = 21;
    public const TRUTH_UNKNOWN = 0;
    public const TRUTH_TRUE = 1;
    public const TRUTH_FALSE = 2;
    public const TRUTH_NEUTRAL = 3;
    public const TRUTH_GLUT = 4;
    public const TRUTH_PARADOX = 5;

    /** @var array{0:int,1:int,2:int} */
    public readonly array $sPlus;
    /** @var array{0:int,1:int} */
    public readonly array $sMinus;

    /**
     * @param list<int> $sPlus three uint32 words (W0, W2, W4)
     * @param list<int> $sMinus two uint32 words (W1, W3)
     */
    public function __construct(public readonly int $tag, array $sPlus, array $sMinus)
    {
        Native::int($tag, "tag", 0, 255);
        if (count($sPlus) !== 3 || count($sMinus) !== 2) {
            throw new ZxvUsageException(Status::E_ARG, "sPlus needs 3 words, sMinus 2");
        }
        $this->sPlus = array_map(fn ($w) => Native::uint32($w, "sPlus word"), array_values($sPlus));
        $this->sMinus = array_map(fn ($w) => Native::uint32($w, "sMinus word"), array_values($sMinus));
    }

    /** tag = 100*sync + 10*shell + trunk (pure). */
    public static function tag(int $trunk, int $shell, int $sync): int
    {
        Native::int($trunk, "trunk", 0, 9);
        Native::int($shell, "shell", 0, 9);
        Native::int($sync, "sync", 0, 1);
        return 100 * $sync + 10 * $shell + $trunk;
    }

    /** Parse 21 bytes (pure). */
    public static function fromBytes(string $bytes): self
    {
        $u = unpack("Ctag/Vw0/Nw1/Vw2/Nw3/Vw4", Native::exact($bytes, "frame", self::BYTES));
        return new self($u["tag"], [$u["w0"], $u["w2"], $u["w4"]], [$u["w1"], $u["w3"]]);
    }

    /** The idle pilot frame of a trunk (libzxv; never all zeros). */
    public static function idle(int $trunk): self
    {
        $out = Native::new("uint8_t[21]");
        Native::check(Native::ffi()->zxv_wire_idle(Native::int($trunk, "trunk", 0, 9), $out));
        return self::fromBytes(Native::read($out, self::BYTES));
    }

    /** The 21 wire octets (pure PHP). */
    public function toBytes(): string
    {
        return pack("CVNVNV", $this->tag, $this->sPlus[0], $this->sMinus[0], $this->sPlus[1], $this->sMinus[1], $this->sPlus[2]);
    }

    /** The same octets from libzxv's zxv_wire_pack (cross-check). */
    public function nativeBytes(): string
    {
        $sp = Native::new("uint32_t[3]");
        $sm = Native::new("uint32_t[2]");
        foreach ($this->sPlus as $i => $w) {
            $sp[$i] = $w;
        }
        foreach ($this->sMinus as $i => $w) {
            $sm[$i] = $w;
        }
        $out = Native::new("uint8_t[21]");
        Native::check(Native::ffi()->zxv_wire_pack($this->tag, $sp, $sm, $out));
        return Native::read($out, self::BYTES);
    }

    /** The W5 correlation resolver (libzxv): a TRUTH_* constant. */
    public function resolve(): int
    {
        $out = Native::new("int32_t");
        Native::check(Native::ffi()->zxv_wire_resolve(Native::bytes($this->toBytes()), \FFI::addr($out)));
        return $out->cdata;
    }
}
