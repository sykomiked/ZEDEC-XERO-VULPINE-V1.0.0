<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

use FFI\CData;

/**
 * ABI 1.5: reassembles one freight set, member by member and in any order,
 * into a buffer it allocates. The first accepted member fixes the set (id,
 * k, flags, count, expandLimit). Pure; NOT thread-safe.
 *
 *     $join = new FreightJoin(strlen($data));
 *     foreach ($members as [$h, $p]) { $join->add($h, $p); }
 *     $join->result() === $data;   // true
 */
final class FreightJoin extends PureHandle
{
    /** @throws Exception\ZxvUsageException ZXV_E_RANGE (capacity above Freight::SET_MAX_BYTES) */
    public function __construct(int|string|float $capacity)
    {
        $c = Native::u64($capacity, "capacity");
        $this->own("zxv_freight_join_destroy", fn (CData $o) => Native::ffi15()->zxv_freight_join_create($c, $o));
    }

    /**
     * Decode one member into its place (a member already received is a
     * no-op; a failed member is not recorded).
     * @param string|list<string> $packets
     * @throws Exception\ZxvRefusedException ZXV_E_STATE (another set, or a non-last member not full), ZXV_E_CAPACITY, ZXV_E_ERASURE
     * @throws Exception\VerificationFailedException
     */
    public function add(FreightHeader|string $header, string|array $packets): self
    {
        $h = FreightHeader::coerce($header);
        $pk = Freight::packetsBytes($packets);
        Native::check(Native::ffi15()->zxv_freight_join_add($this->ptr(), $h->mem(), Native::bytes($pk, true), strlen($pk)));
        return $this;
    }

    /** @return array{received:int, count:int, complete:bool, total:int|string} */
    public function status(): array
    {
        [$r, $rP] = Native::out("uint32_t");
        [$c, $cP] = Native::out("uint32_t");
        [$d, $dP] = Native::out("int32_t");
        [$t, $tP] = Native::out("uint64_t");
        Native::check(Native::ffi15()->zxv_freight_join_status($this->ptr(), $rP, $cP, $dP, $tP));
        return ["received" => $r->cdata, "count" => $c->cdata, "complete" => $d->cdata !== 0, "total" => Native::u64Out($t->cdata)];
    }

    public function isComplete(): bool
    {
        return $this->status()["complete"];
    }

    /** The joined bytes. @throws Exception\ZxvRefusedException ZXV_E_STATE until every member arrived */
    public function result(): string
    {
        $total = $this->status()["total"];
        $hint = is_int($total) ? max($total, 1) : 1;
        return Native::bytesOut(fn ($b, $c, $l) => Native::ffi15()->zxv_freight_join_result($this->ptr(), $b, $c, $l), $hint);
    }
}
