<?php

// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

declare(strict_types=1);

namespace Zedec\Zxv;

use Zedec\Zxv\Exception\ZxvUsageException;

/**
 * The ledger's exact balances as a state for sdk/solidity/ZxvStateChannel.sol:
 * abi.encode(bytes32 channelId, uint256 nonce, address[] parties,
 * int256[] balances), digest = keccak256(encoding). The parties sign the
 * digest (EIP-191) in their own wallets: ZXV has no secp256k1.
 */
final class StateChannel
{
    /** @param Capability $capability needs EVM (and LEDGER_READ for ledgerState) */
    public function __construct(public readonly Capability $capability)
    {
    }

    /**
     * @param string $channelId 32 bytes (binary or 0x hex)
     * @param list<string> $parties 20-byte addresses (binary or 0x hex)
     * @param list<int> $balances int64 each
     */
    public function encodeState(string $channelId, int $nonce, array $parties, array $balances): ChannelState
    {
        if (count($parties) !== count($balances)) {
            throw new ZxvUsageException(Status::E_ARG, "parties and balances differ in length");
        }
        $n = count($parties);
        $bal = Native::new("int64_t[" . max($n, 1) . "]");
        foreach (array_values($balances) as $i => $b) {
            $bal[$i] = Native::int($b, "balance");
        }
        $id = self::bin($channelId, 32, "channelId");
        $addr = self::addresses($parties);
        return $this->call($n, fn ($buf, $cap, $len, $dg) => Native::ffi()->zxv_evm_encode_state($this->capability->ptr(), $id, Native::uint($nonce, "nonce"), $addr, $bal, $n, $buf, $cap, $len, $dg));
    }

    /**
     * Each party's EQUITY (888) balance, read from ledger $accounts.
     * @param list<int> $accounts
     * @param list<string> $parties
     */
    public function ledgerState(string $channelId, int $nonce, string $currency, array $accounts, array $parties): ChannelState
    {
        if (count($parties) !== count($accounts)) {
            throw new ZxvUsageException(Status::E_ARG, "parties and accounts differ in length");
        }
        $n = count($parties);
        $acc = Native::new("uint32_t[" . max($n, 1) . "]");
        foreach (array_values($accounts) as $i => $a) {
            $acc[$i] = Native::uint32($a, "account");
        }
        $id = self::bin($channelId, 32, "channelId");
        $addr = self::addresses($parties);
        return $this->call($n, fn ($buf, $cap, $len, $dg) => Native::ffi()->zxv_evm_ledger_state($this->capability->ptr(), $id, Native::uint($nonce, "nonce"), $currency, $acc, $addr, $n, $buf, $cap, $len, $dg));
    }

    private function call(int $n, \Closure $fn): ChannelState
    {
        $need = (6 + 2 * $n) * 32;
        $buf = Native::new("uint8_t[" . max($need, 1) . "]");
        $len = Native::new("size_t");
        $dg = Native::new("uint8_t[32]");
        Native::check($fn($buf, $need, \FFI::addr($len), $dg), $this->capability->context);
        return new ChannelState(Native::read($buf, $len->cdata), Native::read($dg, 32));
    }

    /** @param list<string> $parties */
    private static function addresses(array $parties): ?\FFI\CData
    {
        return Native::bytes(implode("", array_map(fn ($a) => self::raw($a, 20, "party address"), $parties)), true);
    }

    private static function bin(string $v, int $size, string $name): \FFI\CData
    {
        return Native::bytes(self::raw($v, $size, $name));
    }

    private static function raw(string $v, int $size, string $name): string
    {
        if (str_starts_with($v, "0x") && strlen($v) === 2 + 2 * $size) {
            $v = (string) hex2bin(substr($v, 2));
        }
        return Native::exact($v, $name, $size);
    }
}
