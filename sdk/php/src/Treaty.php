<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

use FFI\CData;
use Zedec\Zxv\Exception\TreatyException;
use Zedec\Zxv\Exception\ZxvUsageException;

/**
 * ABI 1.5: a treaty, a small multilateral settlement (zxv_treaty_*).
 *
 * Written as canonical Sutra text or as the fixed-width COBOL copybook
 * record (ZXVTRTY.cpy, REC_LEN bytes); both parse to the same treaty.
 * Amounts are PIC S9(18) minor units of the asset (ints); weights are
 * fractions num/den in lowest terms. Pure: no context, no capability.
 *
 *     $t = Treaty::parse("TREATY N USD NET\nPARTY A 5 0 1/2\nPARTY B 0 0 1/2\nOWE A B 5\nEND\n");
 *     $r = $t->run();
 *     $r->legs();       // [["from" => 0, "to" => 1, "amount" => 5]]
 *
 * Honest limits: run() is exact arithmetic in a private engine; it moves no
 * real money and is not a clearing house, a licence or legal advice.
 */
final class Treaty extends PureHandle
{
    /** Length of a treaty record (ZXV_TREATY_REC_LEN). */
    public const REC_LEN = 2542;
    /** Length of a result record (ZXV_TREATY_RESULT_LEN). */
    public const RESULT_LEN = 2422;
    /** Longest canonical Sutra text in bytes. */
    public const SUTRA_MAX = 3843;
    public const MAX_PARTIES = 16;
    public const MAX_OWES = 64;
    /** Largest magnitude of any amount: PIC S9(18). */
    public const PIC18_MAX = 999999999999999999;
    /** Largest weight numerator or denominator: PIC 9(9). */
    public const WEIGHT_MAX = 999999999;
    /** The operation by ZXV_TREATY_OP_* value: "net" nets only; "setl" also posts with the fee and pays claims. */
    public const OPS = [1 => "net", 2 => "setl"];
    /** Why a treaty was refused (ZXV_TREATY_FAULT_*), by value. */
    public const FAULTS = [
        0 => "none", 2 => "magic", 3 => "syntax", 4 => "name", 5 => "asset", 6 => "op", 7 => "count", 8 => "number",
        9 => "party", 10 => "weight", 11 => "claim", 12 => "owe", 13 => "pad",
    ];

    /** A new, empty treaty: it fails validation until a parse fills it. */
    public function __construct()
    {
        $this->own("zxv_treaty_destroy", fn (CData $o) => Native::ffi15()->zxv_treaty_create($o));
    }

    /**
     * Parse canonical Sutra text into a new treaty.
     * @throws TreatyException text outside the grammar or breaking a rule (fault, offset say where)
     * @throws ZxvUsageException ZXV_E_TOO_LONG, ZXV_E_UTF8
     */
    public static function parse(string $text): self
    {
        return (new self())->parseSutra($text);
    }

    /**
     * Parse a copybook record of exactly REC_LEN bytes into a new treaty.
     * @throws TreatyException (fault, offset in the record)
     */
    public static function fromRecord(string $bytes): self
    {
        return (new self())->parseRecord($bytes);
    }

    /** Replace the content with parsed Sutra text; on failure it is unchanged. */
    public function parseSutra(string $text): self
    {
        $t = Native::cstr($text, "text");
        [$off, $offP] = Native::out("uint32_t");
        [$fault, $faultP] = Native::out("int32_t");
        $rc = Native::ffi15()->zxv_treaty_parse_sutra($this->ptr(), $t, $offP, $faultP);
        self::faultCheck($rc, $fault, $off);
        return $this;
    }

    /** Replace the content with a parsed record; on failure it is unchanged. */
    public function parseRecord(string $bytes): self
    {
        $b = Native::bytes($bytes, true);
        [$off, $offP] = Native::out("uint32_t");
        [$fault, $faultP] = Native::out("int32_t");
        $rc = Native::ffi15()->zxv_treaty_parse_record($this->ptr(), $b, strlen($bytes), $offP, $faultP);
        self::faultCheck($rc, $fault, $off);
        return $this;
    }

    /** Empty the treaty (it then fails validation). */
    public function clear(): self
    {
        Native::check(Native::ffi15()->zxv_treaty_clear($this->ptr()));
        return $this;
    }

    /** The canonical Sutra text. @throws TreatyException if it does not validate */
    public function toSutra(): string
    {
        return Native::textOut(fn ($b, $c, $l) => Native::ffi15()->zxv_treaty_emit_sutra($this->ptr(), $b, $c, $l));
    }

    /** The REC_LEN-byte copybook record. @throws TreatyException if it does not validate */
    public function toRecord(): string
    {
        return Native::bytesOut(fn ($b, $c, $l) => Native::ffi15()->zxv_treaty_emit_record($this->ptr(), $b, $c, $l), self::REC_LEN);
    }

    /** @throws TreatyException unless every rule holds (fault set, offset 0) */
    public function validate(): self
    {
        [$fault, $faultP] = Native::out("int32_t");
        $rc = Native::ffi15()->zxv_treaty_validate($this->ptr(), $faultP);
        self::faultCheck($rc, $fault, null);
        return $this;
    }

    public function isValid(): bool
    {
        try {
            $this->validate();
            return true;
        } catch (TreatyException) {
            return false;
        }
    }

    /** Whether two treaties have the same canonical content. */
    public function equals(self $other): bool
    {
        [$eq, $eqP] = Native::out("int32_t");
        Native::check(Native::ffi15()->zxv_treaty_equal($this->ptr(), $other->ptr(), $eqP));
        return $eq->cdata === 1;
    }

    public function name(): string
    {
        return Native::textOut(fn ($b, $c, $l) => Native::ffi15()->zxv_treaty_name($this->ptr(), $b, $c, $l));
    }

    /** The asset code (e.g. "USD"). */
    public function asset(): string
    {
        return Native::textOut(fn ($b, $c, $l) => Native::ffi15()->zxv_treaty_asset($this->ptr(), $b, $c, $l));
    }

    /** "net" or "setl". */
    public function op(): string|int
    {
        $op = $this->header()[0];
        return self::OPS[$op] ?? $op;
    }

    public function partyCount(): int
    {
        return $this->header()[1];
    }

    public function obligationCount(): int
    {
        return $this->header()[2];
    }

    /**
     * Party $index (0-based, in treaty order).
     * @return array{id:string, deposit:int, claim:int, weightNum:int, weightDen:int}
     * @throws Exception\ZxvUsageException ZXV_E_NOT_FOUND past the last party
     */
    public function party(int $index): array
    {
        $i = Native::uint32($index, "index");
        $id = Native::textOut(fn ($b, $c, $l) => Native::ffi15()->zxv_treaty_party_id($this->ptr(), $i, $b, $c, $l));
        [$dep, $depP] = Native::out("int64_t");
        [$claim, $claimP] = Native::out("int64_t");
        [$num, $numP] = Native::out("uint32_t");
        [$den, $denP] = Native::out("uint32_t");
        Native::check(Native::ffi15()->zxv_treaty_party($this->ptr(), $i, $depP, $claimP, $numP, $denP));
        return ["id" => $id, "deposit" => $dep->cdata, "claim" => $claim->cdata, "weightNum" => $num->cdata, "weightDen" => $den->cdata];
    }

    /** @return list<array{id:string, deposit:int, claim:int, weightNum:int, weightDen:int}> */
    public function parties(): array
    {
        $n = $this->partyCount();
        return $n === 0 ? [] : array_map(fn (int $i) => $this->party($i), range(0, $n - 1));
    }

    /**
     * Obligation $index: party $from owes party $to $amount.
     * @return array{from:int, to:int, amount:int}
     */
    public function obligation(int $index): array
    {
        [$from, $fromP] = Native::out("uint32_t");
        [$to, $toP] = Native::out("uint32_t");
        [$amt, $amtP] = Native::out("int64_t");
        Native::check(Native::ffi15()->zxv_treaty_owe($this->ptr(), Native::uint32($index, "index"), $fromP, $toP, $amtP));
        return ["from" => $from->cdata, "to" => $to->cdata, "amount" => $amt->cdata];
    }

    /** @return list<array{from:int, to:int, amount:int}> */
    public function obligations(): array
    {
        $n = $this->obligationCount();
        return $n === 0 ? [] : array_map(fn (int $i) => $this->obligation($i), range(0, $n - 1));
    }

    /**
     * Run the treaty in a fresh private engine: fund the deposits, net the
     * obligations (at most parties - 1 legs), and for "setl" post the legs
     * with the 0.08889% assurance fee, pay the claims back and report each
     * party's dividend share. Deterministic; needs no capability.
     * @throws TreatyException the treaty does not validate
     * @throws Exception\InsufficientFundsException a payer cannot cover its legs plus the fee
     * @throws ZxvUsageException ZXV_E_RANGE (a position leaves PIC S9(18))
     */
    public function run(): TreatyResult
    {
        $out = Native::new("zxv_treaty_result*");
        Native::check(Native::ffi15()->zxv_treaty_run($this->ptr(), \FFI::addr($out)));
        return TreatyResult::wrap($out);
    }

    /** @return array{0:int,1:int,2:int} [op, parties, owes] */
    private function header(): array
    {
        [$op, $opP] = Native::out("int32_t");
        [$np, $npP] = Native::out("uint32_t");
        [$no, $noP] = Native::out("uint32_t");
        Native::check(Native::ffi15()->zxv_treaty_header($this->ptr(), $opP, $npP, $noP));
        return [$op->cdata, $np->cdata, $no->cdata];
    }

    private static function faultCheck(int $rc, CData $fault, ?CData $offset): void
    {
        if ($rc === Status::OK) {
            return;
        }
        $err = Native::errorFor($rc);
        if ($err instanceof TreatyException) {
            $f = $fault->cdata;
            $err->fault = self::FAULTS[$f] ?? $f;
            $err->offset = $offset === null ? 0 : $offset->cdata;
        }
        throw $err;
    }
}
