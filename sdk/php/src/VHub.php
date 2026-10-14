<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

/**
 * ABI 1.5: the V-Bill hub (zxv_vhub_*), a DEMONSTRATION of cross-currency
 * clearing through V-Bills, with test currencies only.
 *
 * Each test currency has ONE exact peg against the V-Bill, set only by its
 * issuer with an ML-DSA-87 signature. A swap from currency A to currency B
 * goes A -> V-Bill -> B as ONE balanced, all-or-nothing posting on the
 * hub's own in-process ledger (v = a*denA/numA, b = v*numB/denB).
 *
 * EXACT. Both divisions must be exact or the swap is refused (ZXV_E_RANGE);
 * nothing is rounded and no rate is a float. quote() gives the largest
 * exact amount not above a requested one.
 *
 * RULES. Every rule of the sending AND receiving currency must allow a
 * swap; a currency with no rule refuses every swap through it (fail closed,
 * ZXV_E_STATE). A limit refusal is ZxvRefusedException ZXV_E_LIMIT.
 *
 * CAPABILITIES. Changing anything needs LEDGER_POST; quote, balance,
 * currency, balances and check need LEDGER_READ.
 *
 *     $hub = new VHub($cap);
 *     $hub->addCurrency(0, "ZTA", 2, $issuerPk);
 *     $msg = VHub::pegMessage(0, 350, 1, 1);
 *     $hub->setPeg(0, 350, 1, 1, (new MlDsa87($cap))->sign($sk, $msg, deterministic: true));
 *
 * Thread-safe in C. Freed before its context (R6).
 *
 * Honest limits: test currencies only (ISO 4217 codes are refused) and no
 * real money: the ledger lives in process memory and is lost on close. No
 * network: connected to no settlement rail, central bank or issuer. Peg
 * signatures use ML-DSA-87 (not FIPS 140-3 validated). V-Bills are likely
 * securities in many jurisdictions; real use needs legal review and a
 * participating issuer. A rule reports what it decided; it is not itself a
 * legal check.
 */
final class VHub extends NativeHandle
{
    public const MAX_CCY = 8;
    public const MAX_RULES = 4;
    public const LIMIT_SENDERS = 16;
    public const PEG_MSG_BYTES = 40;
    public const CODE_MAX = 12;
    /** Largest single amount (2^59). */
    public const AMOUNT_MAX = 1 << 59;
    /** The V-Bill issuer's account: always 0. */
    public const VBILL_ISSUER = 0;
    public const ISSUER_KEY_BYTES = 2592;
    public const SIGNATURE_BYTES = 4627;

    /**
     * The exact 40 bytes an issuer signs to set a peg: "zxv/vhub-peg/v1" ||
     * slot || num || den || epoch (u64 little-endian). Sign with ML-DSA-87
     * and an empty context.
     */
    public static function pegMessage(int $slot, int|string|float $num, int|string|float $den, int|string|float $epoch): string
    {
        $out = Native::new("uint8_t[" . self::PEG_MSG_BYTES . "]");
        Native::check(Native::ffi15()->zxv_vhub_peg_msg(Native::uint8($slot, "slot"), Native::u64($num, "num"), Native::u64($den, "den"), Native::u64($epoch, "epoch"), $out));
        return Native::read($out, self::PEG_MSG_BYTES);
    }

    /**
     * A new hub on the capability's context.
     * @param ?string $seed 32 bytes from which posting keys and UETRs are derived; null draws a random seed
     * @throws Exception\CapabilityDeniedException without LEDGER_POST
     */
    public function __construct(public readonly Capability $capability, ?string $seed = null)
    {
        $s = $seed === null ? null : Native::fixed($seed, "seed", 32);
        $out = Native::new("zxv_vhub*");
        Native::check(Native::ffi15()->zxv_vhub_create($capability->ptr(), $s, \FFI::addr($out)), $capability->context);
        parent::__construct($capability->context, $out, "zxv_vhub_destroy");
    }

    /**
     * Register test currency $slot (0..7), opening its issuer, pool and
     * V-Bill float accounts. The peg starts unset. A failure registers nothing.
     * @param string $code 3..12 of A-Z and 0-9; not ISO 4217, "VBL" or "VFV"
     * @param string $issuerPublicKey ML-DSA-87 public key (ISSUER_KEY_BYTES); it alone may set the peg
     * @return array{issuerAccount:int, poolAccount:int, vfloatAccount:int, pegNum:int|string, pegDen:int|string, pegEpoch:int|string}
     * @throws Exception\ZxvUsageException ZXV_E_CURRENCY (ISO 4217 code), ZXV_E_DUPLICATE
     */
    public function addCurrency(int $slot, string $code, int $minorUnits, string $issuerPublicKey, int|string|float $poolFunding = 0, int|string|float $vbillBacking = 0): array
    {
        $pk = Native::fixed($issuerPublicKey, "issuerPublicKey", self::ISSUER_KEY_BYTES);
        $this->rc(Native::ffi15()->zxv_vhub_add_ccy($this->capability->ptr(), $this->ptr(), Native::uint8($slot, "slot"), Native::cstr($code, "code"),
            Native::uint8($minorUnits, "minorUnits"), $pk, Native::u64($poolFunding, "poolFunding"), Native::u64($vbillBacking, "vbillBacking")));
        return $this->currency($slot);
    }

    /**
     * A currency's accounts and current peg (pegDen 0 while unset).
     * @return array{issuerAccount:int, poolAccount:int, vfloatAccount:int, pegNum:int|string, pegDen:int|string, pegEpoch:int|string}
     * @throws Exception\ZxvUsageException ZXV_E_NOT_FOUND
     */
    public function currency(int $slot): array
    {
        [$ia, $iaP] = Native::out("uint32_t");
        [$pa, $paP] = Native::out("uint32_t");
        [$va, $vaP] = Native::out("uint32_t");
        [$n, $nP] = Native::out("uint64_t");
        [$d, $dP] = Native::out("uint64_t");
        [$e, $eP] = Native::out("uint64_t");
        $this->rc(Native::ffi15()->zxv_vhub_ccy_info($this->capability->ptr(), $this->ptr(), Native::uint8($slot, "slot"), $iaP, $paP, $vaP, $nP, $dP, $eP));
        return ["issuerAccount" => $ia->cdata, "poolAccount" => $pa->cdata, "vfloatAccount" => $va->cdata,
            "pegNum" => Native::u64Out($n->cdata), "pegDen" => Native::u64Out($d->cdata), "pegEpoch" => Native::u64Out($e->cdata)];
    }

    /**
     * Set a currency's peg to num/den at $epoch (above the current one),
     * given the issuer's ML-DSA-87 signature over pegMessage(). A refused peg changes nothing.
     * @throws Exception\ZxvRefusedException ZXV_E_PEG (stale or replayed epoch)
     * @throws Exception\VerificationFailedException not the issuer's signature over these numbers
     * @throws Exception\ZxvUsageException ZXV_E_RANGE (num or den 0)
     */
    public function setPeg(int $slot, int|string|float $num, int|string|float $den, int|string|float $epoch, string $signature): self
    {
        $sig = Native::fixed($signature, "signature", self::SIGNATURE_BYTES);
        $this->rc(Native::ffi15()->zxv_vhub_set_peg($this->capability->ptr(), $this->ptr(), Native::uint8($slot, "slot"), Native::u64($num, "num"),
            Native::u64($den, "den"), Native::u64($epoch, "epoch"), $sig));
        return $this;
    }

    /** Open a holder account in currency $slot for $owner. @throws Exception\ZxvRefusedException ZXV_E_CAPACITY */
    public function openAccount(int $slot, int $owner): int
    {
        [$o, $oP] = Native::out("uint32_t");
        $this->rc(Native::ffi15()->zxv_vhub_open_account($this->capability->ptr(), $this->ptr(), Native::uint8($slot, "slot"), Native::uint32($owner, "owner"), $oP));
        return $o->cdata;
    }

    /** Test faucet: issue $amount to an account (by its issuer). */
    public function issue(int $account, int|string|float $amount): self
    {
        $this->rc(Native::ffi15()->zxv_vhub_issue($this->capability->ptr(), $this->ptr(), Native::uint32($account, "account"), Native::u64($amount, "amount")));
        return $this;
    }

    /** @return array{debit:int|string, credit:int|string, equity:int} one account's rails */
    public function balance(int $account): array
    {
        $r = $this->balanceRc(Native::uint32($account, "account"));
        Native::check($r[0], $this->context);
        return $r[1];
    }

    /** @return list<array{debit:int|string, credit:int|string, equity:int}> every account, 0 first */
    public function balances(): array
    {
        $list = [];
        while (true) {
            [$rc, $b] = $this->balanceRc(count($list));
            if ($rc === Status::E_NOT_FOUND) {
                return $list;
            }
            Native::check($rc, $this->context);
            $list[] = $b;
        }
    }

    /**
     * Add the limit rule to currency $slot: at most $limit minor units out
     * per sender per rule epoch. One limit tracks 16 senders per epoch and
     * refuses a 17th (fail closed).
     * @throws Exception\ZxvRefusedException ZXV_E_CAPACITY (4 rules already)
     */
    public function addLimit(int $slot, int|string|float $limit): self
    {
        $this->rc(Native::ffi15()->zxv_vhub_add_limit($this->capability->ptr(), $this->ptr(), Native::uint8($slot, "slot"), Native::u64($limit, "limit")));
        return $this;
    }

    /** Start the next rule epoch; returns it. */
    public function nextEpoch(): int|string
    {
        [$o, $oP] = Native::out("uint64_t");
        $this->rc(Native::ffi15()->zxv_vhub_next_epoch($this->capability->ptr(), $this->ptr(), $oP));
        return Native::u64Out($o->cdata);
    }

    /**
     * The largest exact amount of $from not above $want and what it would
     * carry. Runs no rules and checks no balances.
     * @return array{amount:int|string, vbills:int|string, received:int|string}
     * @throws Exception\ZxvRefusedException ZXV_E_PEG (a peg unset)
     */
    public function quote(int $from, int $to, int|string|float $want): array
    {
        [$a, $aP] = Native::out("uint64_t");
        [$v, $vP] = Native::out("uint64_t");
        [$r, $rP] = Native::out("uint64_t");
        $this->rc(Native::ffi15()->zxv_vhub_quote($this->capability->ptr(), $this->ptr(), Native::uint8($from, "from"), Native::uint8($to, "to"), Native::u64($want, "want"), $aP, $vP, $rP));
        return ["amount" => Native::u64Out($a->cdata), "vbills" => Native::u64Out($v->cdata), "received" => Native::u64Out($r->cdata)];
    }

    /**
     * Swap $amount of currency $from from $sender to $receiver in currency
     * $to, as one six-line posting at $tick (caller-supplied, never the wall
     * clock). A refused swap changes nothing.
     * @return array{vbills:int|string, received:int|string}
     * @throws Exception\ZxvRefusedException ZXV_E_LIMIT, ZXV_E_STATE (no rule), ZXV_E_PEG
     * @throws Exception\InsufficientFundsException
     * @throws Exception\ZxvUsageException ZXV_E_RANGE (0, too large, or inexact under the pegs), ZXV_E_CURRENCY
     * @throws Exception\ZxvFatalError ZXV_E_INVARIANT (the context halts)
     */
    public function swap(int $from, int $to, int $sender, int $receiver, int|string|float $amount, int|string|float $tick): array
    {
        [$v, $vP] = Native::out("uint64_t");
        [$r, $rP] = Native::out("uint64_t");
        $this->rc(Native::ffi15()->zxv_vhub_swap($this->capability->ptr(), $this->ptr(), Native::uint8($from, "from"), Native::uint8($to, "to"),
            Native::uint32($sender, "sender"), Native::uint32($receiver, "receiver"), Native::u64($amount, "amount"), Native::u64($tick, "tick"), $vP, $rP));
        return ["vbills" => Native::u64Out($v->cdata), "received" => Native::u64Out($r->cdata)];
    }

    /**
     * Verify the whole hub ledger (A = L + E per account, balanced per
     * asset, the posting hash chain). Returns the postings so far.
     * @throws Exception\ZxvFatalError ZXV_E_INVARIANT (the context halts)
     */
    public function check(): int|string
    {
        [$o, $oP] = Native::out("uint64_t");
        $this->rc(Native::ffi15()->zxv_vhub_check($this->capability->ptr(), $this->ptr(), $oP));
        return Native::u64Out($o->cdata);
    }

    /** @return array{0:int, 1:array{debit:int|string, credit:int|string, equity:int}} */
    private function balanceRc(int $account): array
    {
        [$d, $dP] = Native::out("uint64_t");
        [$c, $cP] = Native::out("uint64_t");
        [$e, $eP] = Native::out("int64_t");
        $rc = Native::ffi15()->zxv_vhub_balance($this->capability->ptr(), $this->ptr(), $account, $dP, $cP, $eP);
        return [$rc, ["debit" => Native::u64Out($d->cdata), "credit" => Native::u64Out($c->cdata), "equity" => $e->cdata]];
    }

    private function rc(int $rc): void
    {
        Native::check($rc, $this->context);
    }
}
