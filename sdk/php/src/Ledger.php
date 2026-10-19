<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

/**
 * The triple ledger of a context, through a capability. Amounts are exact
 * int minor units (cents, kobo, ...); every posting is balanced.
 *
 *     $ledger = new Ledger($cap);
 *     $bank = $ledger->openAccount("bank", "NGN", Ledger::ISSUER);
 *     $ada = $ledger->openAccount("ada", "NGN");
 *     $ledger->post($bank, $ada, 50_000, "NGN", "E2E-1");
 *     $ledger->balance($ada)->equity;   // 50000
 */
final class Ledger
{
    /** May never go below zero. */
    public const HOLDER = 0;
    /** May carry a CREDIT (777) balance: issuance, nostro. */
    public const ISSUER = 1;
    /** Largest amount per posting and largest |balance|: 2^53 - 1. */
    public const AMOUNT_MAX = 9007199254740991;

    /** @param Capability $capability needs LEDGER_READ and/or LEDGER_POST */
    public function __construct(public readonly Capability $capability)
    {
    }

    /** Open an account in an enabled currency; returns its id. Needs LEDGER_POST. */
    public function openAccount(string $name, string $currency, int $kind = self::HOLDER): int
    {
        $out = Native::new("uint32_t");
        Native::check(Native::ffi()->zxv_ledger_open_account_cap($this->capability->ptr(), Native::utf8($name, "name"), $currency, Native::int($kind, "kind", 0, 1), \FFI::addr($out)), $this->ctx());
        return $out->cdata;
    }

    /**
     * Post a balanced transfer; returns the entry id.
     * @throws Exception\InsufficientFundsException a holder would go below zero
     * @throws Exception\CapabilityDeniedException the cap lacks LEDGER_POST
     * @throws Exception\FloatRejectedException for a float amount
     */
    public function post(int $from, int $to, int|float $amount, string $currency, string $reference): int
    {
        $out = Native::new("uint64_t");
        Native::check(Native::ffi()->zxv_ledger_post_cap($this->capability->ptr(), Native::uint32($from, "from"), Native::uint32($to, "to"), Native::int($amount, "amount"), $currency, Native::utf8($reference, "reference"), \FFI::addr($out)), $this->ctx());
        return $out->cdata;
    }

    /** Rail balances: DEBIT 555 received, CREDIT 777 sent, EQUITY 888 = debit - credit. */
    public function balance(int $account): Balance
    {
        $d = Native::new("int64_t");
        $c = Native::new("int64_t");
        $e = Native::new("int64_t");
        Native::check(Native::ffi()->zxv_ledger_balance_cap($this->capability->ptr(), Native::uint32($account, "account"), \FFI::addr($d), \FFI::addr($c), \FFI::addr($e)), $this->ctx());
        return new Balance($d->cdata, $c->cdata, $e->cdata);
    }

    /** Verify the invariants; a broken one poisons the context (ZxvFatalError). */
    public function check(): bool
    {
        Native::check(Native::ffi()->zxv_ledger_check_cap($this->capability->ptr()), $this->ctx());
        return true;
    }

    /** Number of accounts. Needs LEDGER_READ. */
    public function accountCount(): int
    {
        $n = Native::new("uint32_t");
        Native::check(Native::ffi()->zxv_ledger_account_count_cap($this->capability->ptr(), \FFI::addr($n)), $this->ctx());
        return $n->cdata;
    }

    /**
     * The journal of one account, oldest first: entry id, signed amount
     * (+ received, - sent), counterparty, reference. Needs LEDGER_READ.
     * @return list<array{entryId:int, amount:int, counterparty:int, reference:string}>
     */
    public function journal(int $account): array
    {
        $a = Native::uint32($account, "account");
        $n = Native::new("uint32_t");
        Native::check(Native::ffi()->zxv_ledger_entry_count_cap($this->capability->ptr(), $a, \FFI::addr($n)), $this->ctx());
        $out = [];
        for ($i = 0; $i < $n->cdata; $i++) {
            $id = Native::new("uint64_t");
            $amt = Native::new("int64_t");
            $cp = Native::new("uint32_t");
            [$rc, $ref] = Native::text(fn ($b, $c, $l) => Native::ffi()->zxv_ledger_entry_at_cap($this->capability->ptr(), $a, $i, \FFI::addr($id), \FFI::addr($amt), \FFI::addr($cp), $b, $c, $l));
            Native::check($rc, $this->ctx());
            $out[] = ["entryId" => $id->cdata, "amount" => $amt->cdata, "counterparty" => $cp->cdata, "reference" => (string) $ref];
        }
        return $out;
    }

    /**
     * Name, currency and kind of an account. Needs LEDGER_READ.
     * @return array{id:int, name:string, currency:string, kind:int}
     */
    public function account(int $id): array
    {
        $a = Native::uint32($id, "account");
        [$rc, $name] = Native::text(fn ($b, $c, $l) => Native::ffi()->zxv_ledger_account_name_cap($this->capability->ptr(), $a, $b, $c, $l));
        Native::check($rc, $this->ctx());
        [$rc, $ccy] = Native::text(fn ($b, $c, $l) => Native::ffi()->zxv_ledger_account_ccy_cap($this->capability->ptr(), $a, $b, $c, $l));
        Native::check($rc, $this->ctx());
        $k = Native::new("int32_t");
        Native::check(Native::ffi()->zxv_ledger_account_kind_cap($this->capability->ptr(), $a, \FFI::addr($k)), $this->ctx());
        return ["id" => $a, "name" => (string) $name, "currency" => (string) $ccy, "kind" => $k->cdata];
    }

    private function ctx(): Context
    {
        return $this->capability->context;
    }
}
