<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

use Zedec\Zxv\Exception\ZxvUsageException;

/**
 * ABI 1.5: a token wallet, a holding kept canonical at its own octane
 * (zxv_token_wallet).
 *
 * Every change (mint, spend, transfer, setOctane) needs Capability::MINT
 * from the wallet's context; reads need none. The wallet is not tied to a
 * Swarm budget: tokens enter it only by mint(), which creates them. A
 * halted context refuses every call (ZxvFatalError). Thread-safe in C.
 *
 *     $w = new TokenWallet($cap, "racing");   // $cap needs MINT
 *     $w->mint(1000000);
 *     $w->pieces();                           // 17
 *
 * Density carries no premium: see Token.
 */
final class TokenWallet extends NativeHandle
{
    /**
     * An empty wallet.
     * @param string|int $octane a Token::OCTANES name or 0..63
     * @param int $modelId a free uint32 label (e.g. the swarm model that holds it)
     * @throws Exception\CapabilityDeniedException without MINT
     */
    public function __construct(public readonly Capability $capability, string|int $octane = "plain", int $modelId = 0)
    {
        $out = Native::new("zxv_token_wallet*");
        Native::check(Native::ffi15()->zxv_token_wallet_create($capability->ptr(), Native::uint32($modelId, "modelId"), Token::octane($octane), \FFI::addr($out)), $capability->context);
        parent::__construct($capability->context, $out, "zxv_token_wallet_destroy");
    }

    /**
     * Create $amount new tokens in the wallet.
     * @throws ZxvUsageException ZXV_E_RANGE (the value would pass 64 bits; nothing changes)
     */
    public function mint(int|string|float $amount): self
    {
        $this->rc(Native::ffi15()->zxv_token_wallet_mint($this->capability->ptr(), $this->ptr(), Native::u64($amount, "amount")));
        return $this;
    }

    /**
     * Spend (burn) $amount; the change stays canonical.
     * @throws Exception\InsufficientFundsException the wallet holds less (nothing changes)
     */
    public function spend(int|string|float $amount): self
    {
        $this->rc(Native::ffi15()->zxv_token_wallet_spend($this->capability->ptr(), $this->ptr(), Native::u64($amount, "amount")));
        return $this;
    }

    /**
     * Move $amount to wallet $to (of the same context). This wallet keeps
     * its change at its octane; $to receives at ITS octane. The total is
     * conserved exactly; neither wallet changes on failure.
     * @throws Exception\InsufficientFundsException this wallet holds less
     * @throws Exception\CapabilityDeniedException $to belongs to another context
     */
    public function transfer(self $to, int|string|float $amount): self
    {
        $this->rc(Native::ffi15()->zxv_token_wallet_transfer($this->capability->ptr(), $this->ptr(), $to->ptr(), Native::u64($amount, "amount")));
        return $this;
    }

    /** Change the octane; the same value is re-expressed canonically. */
    public function setOctane(string|int $octane): self
    {
        $this->rc(Native::ffi15()->zxv_token_wallet_set_octane($this->capability->ptr(), $this->ptr(), Token::octane($octane)));
        return $this;
    }

    /** @return array{modelId:int, octane:int, value:int|string, pieces:int|string} */
    public function info(): array
    {
        [$m, $mP] = Native::out("uint32_t");
        [$o, $oP] = Native::out("uint32_t");
        [$v, $vP] = Native::out("uint64_t");
        [$p, $pP] = Native::out("uint64_t");
        $this->rc(Native::ffi15()->zxv_token_wallet_info($this->ptr(), $mP, $oP, $vP, $pP));
        return ["modelId" => $m->cdata, "octane" => $o->cdata, "value" => Native::u64Out($v->cdata), "pieces" => Native::u64Out($p->cdata)];
    }

    /** The octane (0..63). */
    public function octane(): int
    {
        return $this->info()["octane"];
    }

    public function value(): int|string
    {
        return $this->info()["value"];
    }

    public function pieces(): int|string
    {
        return $this->info()["pieces"];
    }

    /** @return list<int|string> the 64 counts per tier */
    public function counts(): array
    {
        $out = Native::new("uint64_t[" . Token::TIERS . "]");
        $this->rc(Native::ffi15()->zxv_token_wallet_counts($this->ptr(), $out));
        return Token::counts($out);
    }

    private function rc(int $rc): void
    {
        Native::check($rc, $this->context);
    }
}
