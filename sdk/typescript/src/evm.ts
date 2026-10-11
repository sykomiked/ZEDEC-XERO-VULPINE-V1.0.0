// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

import type { Capability } from './context.js';
import { usage } from './errors.js';
import { cstr } from './internal/backend.js';
import { bytes, i64, type IntLike, str, u32, u64 } from './internal/check.js';

/** A party with an explicit balance. */
export interface ChannelParty {
  /** 20-byte Ethereum address. */
  address: Uint8Array;
  /** Signed balance in minor units. */
  balance: IntLike;
}

/** A party whose balance is read from a ledger account's EQUITY (888). */
export interface LedgerParty {
  /** 20-byte Ethereum address. */
  address: Uint8Array;
  /** Ledger account id. */
  account: number;
}

/** An encoded channel state. */
export interface ChannelState {
  /** abi.encode(bytes32 channelId, uint256 nonce, address[] parties, int256[] balances). */
  readonly encoding: Uint8Array;
  /** keccak256(encoding): what the parties sign (EIP-191) in their own wallets. */
  readonly digest: Uint8Array;
}

/**
 * The ledger's exact integer balances encoded for the Solidity
 * state-channel contract (sdk/solidity/ZxvStateChannel.sol). ZXV has no
 * secp256k1: the parties sign {@link ChannelState.digest} with their own
 * Ethereum keys. Calls need {@link Cap.EVM} (and LEDGER_READ for
 * {@link StateChannel.ledgerState}).
 */
export class StateChannel {
  /** The capability every call presents. */
  readonly cap: Capability;
  /** 32-byte channel id. */
  readonly channelId: Uint8Array;

  constructor(cap: Capability, channelId: Uint8Array) {
    this.cap = cap;
    this.channelId = bytes('channelId', channelId, 32).slice();
  }

  /** Encode explicit balances. */
  encodeState(nonce: IntLike, parties: readonly ChannelParty[]): ChannelState {
    const n = parties.length;
    const addrs = new Uint8Array(20 * n);
    const bals = new BigInt64Array(n);
    parties.forEach((p, i) => {
      addrs.set(bytes(`parties[${i}].address`, p.address, 20), 20 * i);
      bals[i] = i64(`parties[${i}].balance`, p.balance);
    });
    return this.#encode('zxv_evm_encode_state', (buf, cap, len, dg) => [this.cap._ptr(), this.channelId, u64('nonce', nonce), addrs, bals,
      n, buf, cap, len, dg]);
  }

  /** Encode the EQUITY balances of ledger accounts, all in `currency`. */
  ledgerState(nonce: IntLike, currency: string, parties: readonly LedgerParty[]): ChannelState {
    const n = parties.length;
    const addrs = new Uint8Array(20 * n);
    const accts = new Uint32Array(n);
    parties.forEach((p, i) => {
      addrs.set(bytes(`parties[${i}].address`, p.address, 20), 20 * i);
      accts[i] = u32(`parties[${i}].account`, p.account);
    });
    return this.#encode('zxv_evm_ledger_state', (buf, cap, len, dg) => [this.cap._ptr(), this.channelId, u64('nonce', nonce),
      cstr(str('currency', currency)), accts, addrs, n, buf, cap, len, dg]);
  }

  #encode(fn: string, build: (buf: Uint8Array | null, cap: number, len: BigUint64Array | Uint32Array, digest: Uint8Array) =>
    (number | bigint | ArrayBufferView | null)[]): ChannelState {
    if (this.cap.closed) throw usage('the capability is closed');
    const digest = new Uint8Array(32);
    const ctx = this.cap.context;
    const encoding = ctx._core.bytes(ctx._state, fn, (b, c, l) => build(b, c, l, digest));
    return Object.freeze({ encoding: encoding.slice(), digest });
  }
}
