<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

/**
 * ABI 1.6: a play-lane sandbox (zxv_vino_play). Issuers post play backing
 * and issue Fibonacci-face vouchers to holders, who pass them on and redeem
 * them. Play units come from nowhere, are worth nothing, can never become
 * live value, and are lost on close(). See {@see Vino} for the rules.
 *
 *     $play = new VinoPlay($cap);                   // $cap needs Capability::VINO
 *     $shop = $play->openIssuer(700);
 *     $alice = $play->openHolder(801);
 *     $play->back($shop, 100);                      // play units, not money
 *     $id = $play->issue($shop, $alice, 34);
 *     $play->redeem($id, $alice);                   // retired; pays nothing out
 *
 * Every call uses the capability the sandbox was created with, which must
 * hold VINO. A refused call changes nothing. Thread-safe in C. Close it
 * before its context; closing the context closes it too, and any later use
 * throws ZxvUsageException (ZXV_E_STATE). Amounts, faces and voucher ids
 * are uint64: an int, or a decimal string past PHP_INT_MAX; floats are refused.
 */
final class VinoPlay extends NativeHandle
{
    /**
     * Create an empty play sandbox on the capability's context.
     * @param Capability $capability needs Capability::VINO
     * @throws Exception\CapabilityDeniedException without VINO (or revoked)
     * @throws Exception\ZxvFatalError the context is poisoned
     */
    public function __construct(public readonly Capability $capability)
    {
        $out = Native::new("zxv_vino_play*");
        Native::check(Native::ffi16()->zxv_vino_play_create($capability->ptr(), \FFI::addr($out)), $capability->context);
        parent::__construct($capability->context, $out, "zxv_vino_play_destroy");
    }

    /**
     * Open an account for $owner (an id of your choosing; owners need not be
     * distinct). Returns the new account id (ids count up from 0).
     * @param bool $issuer true opens an issuer, false a holder
     * @throws Exception\ZxvRefusedException ZXV_E_CAPACITY (Vino::MAX_ACCOUNTS already open)
     */
    public function openAccount(int $owner, bool $issuer): int
    {
        [$o, $oP] = Native::out("uint32_t");
        $this->rc(Native::ffi16()->zxv_vino_play_open($this->capability->ptr(), $this->ptr(), Native::uint32($owner, "owner"), $issuer ? 1 : 0, $oP));
        return $o->cdata;
    }

    /** Open an issuer account (see openAccount()). Returns its id. */
    public function openIssuer(int $owner): int
    {
        return $this->openAccount($owner, true);
    }

    /** Open a holder account (see openAccount()). Returns its id. */
    public function openHolder(int $owner): int
    {
        return $this->openAccount($owner, false);
    }

    /**
     * Post $amount play units of backing to an issuer. This is the play
     * faucet: play units come from nowhere and are worth nothing.
     * @param int|string $amount 1..Vino::AMOUNT_MAX
     * @throws Exception\ZxvUsageException ZXV_E_ARG (not an issuer), ZXV_E_NOT_FOUND,
     *   ZXV_E_RANGE (0, too large, or backing would pass 2^62)
     */
    public function back(int $issuer, int|string|float $amount): self
    {
        $this->rc(Native::ffi16()->zxv_vino_play_back($this->capability->ptr(), $this->ptr(), Native::uint32($issuer, "issuer"), Native::u64($amount, "amount")));
        return $this;
    }

    /**
     * Withdraw $amount of an issuer's backing, only while it stays at 1.8x
     * coverage of what it has outstanding. A refusal changes nothing.
     * @throws Exception\CoverageException the issuer would fall below 1.8x coverage
     * @throws Exception\InsufficientFundsException more than the issuer's backing
     * @throws Exception\ZxvUsageException as back()
     */
    public function unback(int $issuer, int|string|float $amount): self
    {
        $this->rc(Native::ffi16()->zxv_vino_play_unback($this->capability->ptr(), $this->ptr(), Native::uint32($issuer, "issuer"), Native::u64($amount, "amount")));
        return $this;
    }

    /**
     * Issue one voucher of Fibonacci face $face from $issuer to $holder on
     * $rail. Returns its id (> 0, never reused in this sandbox).
     * @param int|string $face a Fibonacci ladder face (1, 2, 3, 5, 8, ...)
     * @param VinoRail|int $rail the rail it starts on (an int is passed to the library as is)
     * @throws Exception\CoverageException the issuer would fall below 1.8x coverage
     * @throws Exception\ZxvUsageException ZXV_E_DENOM (not a ladder face), ZXV_E_ARG (issuer not
     *   an issuer, holder not a holder, bad rail), ZXV_E_NOT_FOUND
     * @throws Exception\ZxvRefusedException ZXV_E_CAPACITY (Vino::MAX_VOUCHERS active)
     */
    public function issue(int $issuer, int $holder, int|string|float $face, VinoRail|int $rail = VinoRail::Bearer): int|string
    {
        [$o, $oP] = Native::out("uint64_t");
        $this->rc(Native::ffi16()->zxv_vino_issue($this->capability->ptr(), $this->ptr(), Native::uint32($issuer, "issuer"), Native::uint32($holder, "holder"),
            Native::u64($face, "face"), Vino::rail($rail), $oP));
        return Native::u64Out($o->cdata);
    }

    /**
     * Move an active voucher from holder $from to holder $to, spending it on
     * $rail, which must be its active rail. The rail does not change.
     * @throws Exception\WrongRailException $rail is not the voucher's active rail
     * @throws Exception\ZxvUsageException ZXV_E_STATE (redeemed), ZXV_E_ARG ($from is not its
     *   holder, $to not a holder, $from == $to, bad rail), ZXV_E_NOT_FOUND
     */
    public function transfer(int|string|float $voucher, int $from, int $to, VinoRail|int $rail): self
    {
        $this->rc(Native::ffi16()->zxv_vino_transfer($this->capability->ptr(), $this->ptr(), Native::u64($voucher, "voucher"), Native::uint32($from, "from"),
            Native::uint32($to, "to"), Vino::rail($rail)));
        return $this;
    }

    /**
     * Switch an active voucher, by its holder, to $rail (bearer <-> ghost).
     * Nothing moves; the voucher only changes which rail it can be spent on.
     * @throws Exception\WrongRailException it is already on $rail
     * @throws Exception\ZxvUsageException ZXV_E_STATE (redeemed), ZXV_E_ARG (not its holder, bad rail), ZXV_E_NOT_FOUND
     */
    public function setRail(int|string|float $voucher, int $holder, VinoRail|int $rail): self
    {
        $this->rc(Native::ffi16()->zxv_vino_set_rail($this->capability->ptr(), $this->ptr(), Native::u64($voucher, "voucher"), Native::uint32($holder, "holder"),
            Vino::rail($rail)));
        return $this;
    }

    /**
     * Redeem an active voucher, by its holder, to its issuer: the voucher is
     * retired and the issuer's outstanding falls by its face. Nothing is paid
     * out to anyone.
     * @throws Exception\ZxvUsageException ZXV_E_STATE (already redeemed), ZXV_E_ARG (not its holder), ZXV_E_NOT_FOUND
     */
    public function redeem(int|string|float $voucher, int $holder): self
    {
        $this->rc(Native::ffi16()->zxv_vino_redeem($this->capability->ptr(), $this->ptr(), Native::u64($voucher, "voucher"), Native::uint32($holder, "holder")));
        return $this;
    }

    /**
     * One voucher's face, issuer, holder, rail and state. Redeemed vouchers
     * stay listed by id.
     * @throws Exception\ZxvUsageException ZXV_E_NOT_FOUND (no such voucher)
     */
    public function voucher(int|string|float $id): VinoVoucher
    {
        [$f, $fP] = Native::out("uint64_t");
        [$i, $iP] = Native::out("uint32_t");
        [$h, $hP] = Native::out("uint32_t");
        [$r, $rP] = Native::out("int32_t");
        [$s, $sP] = Native::out("int32_t");
        $raw = Native::u64($id, "voucher");
        $this->rc(Native::ffi16()->zxv_vino_voucher_info($this->capability->ptr(), $this->ptr(), $raw, $fP, $iP, $hP, $rP, $sP));
        return new VinoVoucher(Native::u64Out($raw), Native::u64Out($f->cdata), $i->cdata, $h->cdata,
            VinoRail::tryFrom($r->cdata) ?? $r->cdata, VinoVoucherState::tryFrom($s->cdata) ?? $s->cdata);
    }

    /**
     * One account's owner, kind and totals.
     * @throws Exception\ZxvUsageException ZXV_E_NOT_FOUND (no such account)
     */
    public function account(int $id): VinoAccount
    {
        [$o, $oP] = Native::out("uint32_t");
        [$k, $kP] = Native::out("int32_t");
        [$b, $bP] = Native::out("uint64_t");
        [$f, $fP] = Native::out("uint64_t");
        [$c, $cP] = Native::out("uint32_t");
        $this->rc(Native::ffi16()->zxv_vino_account_info($this->capability->ptr(), $this->ptr(), Native::uint32($id, "account"), $oP, $kP, $bP, $fP, $cP));
        return new VinoAccount($id, $o->cdata, $k->cdata === 1, Native::u64Out($b->cdata), Native::u64Out($f->cdata), $c->cdata);
    }

    /**
     * The ids of the active vouchers an account issued (issuer) or holds
     * (holder), ascending. Redeemed vouchers are not listed.
     * @return list<int|string>
     * @throws Exception\ZxvUsageException ZXV_E_NOT_FOUND (no such account)
     */
    public function vouchers(int $account): array
    {
        $acct = Native::uint32($account, "account");
        [$n, $nP] = Native::out("size_t");
        $capLen = 0;
        // Two-call pattern: ask for the count, then fetch. Another thread may
        // issue in between, so regrow a few times before giving up.
        for ($try = 0; $try < 4; $try++) {
            $ids = $capLen === 0 ? null : Native::new("uint64_t[$capLen]");
            $rc = Native::ffi16()->zxv_vino_vouchers($this->capability->ptr(), $this->ptr(), $acct, $ids, $capLen, $nP);
            if ($rc === Status::E_BUFFER) {
                $capLen = $n->cdata;
                continue;
            }
            $this->rc($rc);
            $out = [];
            for ($i = 0; $i < $n->cdata; $i++) {
                /** @var \FFI\CData $ids */
                $out[] = Native::u64Out($ids[$i]);
            }
            return $out;
        }
        $this->rc(Status::E_BUFFER);
        return [];
    }

    /**
     * Re-check every invariant: each active voucher has a ladder face, one
     * rail, an issuer and a holder; each issuer's outstanding equals the sum
     * of its active faces and keeps 1.8x coverage; each holder's total
     * equals what it holds.
     * @throws Exception\ZxvFatalError ZXV_E_INVARIANT (the context is poisoned)
     */
    public function check(): self
    {
        $this->rc(Native::ffi16()->zxv_vino_play_check($this->capability->ptr(), $this->ptr()));
        return $this;
    }

    private function rc(int $rc): void
    {
        Native::check($rc, $this->context);
    }
}
