<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

use FFI\CData;

/**
 * ABI 1.5: what Treaty::run() produced: per-party net position, final
 * balance and dividend, the netted legs and the fees (all ints, minor
 * units of the treaty's asset). Pure handle; freed by close() or the
 * destructor.
 */
final class TreatyResult extends PureHandle
{
    private function __construct()
    {
    }

    /** @internal */
    public static function wrap(CData $raw): self
    {
        $r = new self();
        $r->adopt("zxv_treaty_result_destroy", $raw);
        return $r;
    }

    public function partyCount(): int
    {
        return $this->summary()[0];
    }

    public function legCount(): int
    {
        return $this->summary()[1];
    }

    /** The assurance fees charged on the legs ("setl"; 0 for "net"). */
    public function fees(): int
    {
        return $this->summary()[2];
    }

    /** @return array{net:int, balance:int, dividend:int} party $index (treaty order) */
    public function party(int $index): array
    {
        [$net, $netP] = Native::out("int64_t");
        [$bal, $balP] = Native::out("int64_t");
        [$div, $divP] = Native::out("int64_t");
        Native::check(Native::ffi15()->zxv_treaty_result_party($this->ptr(), Native::uint32($index, "index"), $netP, $balP, $divP));
        return ["net" => $net->cdata, "balance" => $bal->cdata, "dividend" => $div->cdata];
    }

    /** @return list<array{net:int, balance:int, dividend:int}> */
    public function parties(): array
    {
        $n = $this->partyCount();
        return $n === 0 ? [] : array_map(fn (int $i) => $this->party($i), range(0, $n - 1));
    }

    /** @return array{from:int, to:int, amount:int} netted leg $index */
    public function leg(int $index): array
    {
        [$from, $fromP] = Native::out("uint32_t");
        [$to, $toP] = Native::out("uint32_t");
        [$amt, $amtP] = Native::out("int64_t");
        Native::check(Native::ffi15()->zxv_treaty_result_leg($this->ptr(), Native::uint32($index, "index"), $fromP, $toP, $amtP));
        return ["from" => $from->cdata, "to" => $to->cdata, "amount" => $amt->cdata];
    }

    /** @return list<array{from:int, to:int, amount:int}> */
    public function legs(): array
    {
        $n = $this->legCount();
        return $n === 0 ? [] : array_map(fn (int $i) => $this->leg($i), range(0, $n - 1));
    }

    /** The ZXVRSLT.cpy result record (Treaty::RESULT_LEN bytes; party numbers in it are 1-based). */
    public function toRecord(): string
    {
        return Native::bytesOut(fn ($b, $c, $l) => Native::ffi15()->zxv_treaty_result_record($this->ptr(), $b, $c, $l), Treaty::RESULT_LEN);
    }

    /** @return array{0:int,1:int,2:int} */
    private function summary(): array
    {
        [$np, $npP] = Native::out("uint32_t");
        [$nl, $nlP] = Native::out("uint32_t");
        [$fees, $feesP] = Native::out("int64_t");
        Native::check(Native::ffi15()->zxv_treaty_result_summary($this->ptr(), $npP, $nlP, $feesP));
        return [$np->cdata, $nl->cdata, $fees->cdata];
    }
}
