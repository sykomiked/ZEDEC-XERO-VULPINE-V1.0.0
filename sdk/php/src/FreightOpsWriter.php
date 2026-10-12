<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

use FFI\CData;

/**
 * ABI 1.5: builds an op stream op by op into a buffer of $maxBytes. Each op
 * returns $this; the first error throws, and is sticky (every later op and
 * finish() throw it again). Pure; NOT thread-safe.
 */
final class FreightOpsWriter extends PureHandle
{
    /**
     * @param int|string|float $declaredTotal what the ops must produce in all
     * @param ?string $sha256 32-byte SHA-256 of that output (expansion verifies it)
     * @param int|string|float $maxBytes the stream buffer size (14..FreightOps::MAX; 46 with sha256)
     */
    public function __construct(int|string|float $declaredTotal, ?string $sha256 = null, int|string|float $maxBytes = 65536)
    {
        $t = Native::u64($declaredTotal, "declaredTotal");
        $m = Native::u64($maxBytes, "maxBytes");
        $s = $sha256 === null ? null : Native::fixed($sha256, "sha256", 32);
        $this->own("zxv_freight_ops_writer_destroy", fn (CData $o) => Native::ffi15()->zxv_freight_ops_writer_create($t, $s, $m, $o));
    }

    /** A LITERAL op: these bytes. */
    public function literal(string $bytes): self
    {
        Native::check(Native::ffi15()->zxv_freight_ops_put_literal($this->ptr(), Native::bytes($bytes, true), strlen($bytes)));
        return $this;
    }

    /** A COPY op: $length bytes starting $distance bytes back (overlap allowed). */
    public function copy(int|string|float $distance, int|string|float $length): self
    {
        Native::check(Native::ffi15()->zxv_freight_ops_put_copy($this->ptr(), Native::u64($distance, "distance"), Native::u64($length, "length")));
        return $this;
    }

    /** A CIDREF op: $length bytes the receiver resolves by content id (a Cid or 1..128 raw id bytes). */
    public function cidref(Cid|string $cid, int|string|float $length): self
    {
        $c = $cid instanceof Cid ? $cid->bytes : $cid;
        Native::check(Native::ffi15()->zxv_freight_ops_put_cidref($this->ptr(), Native::bytes($c, true), strlen($c), Native::u64($length, "length")));
        return $this;
    }

    /** A SEED op: $length bytes of generator $gen with $params. */
    public function seed(string|int $gen, string $params, int|string|float $length): self
    {
        Native::check(Native::ffi15()->zxv_freight_ops_put_seed($this->ptr(), FreightOps::gen($gen), Native::bytes($params, true), strlen($params), Native::u64($length, "length")));
        return $this;
    }

    /**
     * The finished stream; the writer stays usable.
     * @throws Exception\ZxvRefusedException ZXV_E_STATE (fewer bytes than declared), or the sticky error
     */
    public function finish(): string
    {
        return Native::bytesOut(fn ($b, $c, $l) => Native::ffi15()->zxv_freight_ops_writer_finish($this->ptr(), $b, $c, $l), 1024);
    }
}
