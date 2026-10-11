// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// zxv-channel.mjs — the off-chain half of ZxvStateChannel.
//
//   ZXV ledger (libzxv) --zxv_evm_ledger_state--> encoding + digest
//        --> every party signs (EIP-191, its own Ethereum wallet)
//        --> submitState(channelId, nonce, balances, signatures) on chain
//
// libzxv is reached through `koffi` (a plain FFI loader), using only the
// handful of C calls this flow needs. A full Node SDK would replace this.
// ZXV has no secp256k1: signing is done by the parties' wallets (ethers).

import koffi from 'koffi';
import { AbiCoder, getBytes, hexlify, Signature } from 'ethers';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const HERE = path.dirname(fileURLToPath(import.meta.url));

/** Status codes this helper names (see zxv_api.h). */
export const ZXV_OK = 0;
export const ZXV_E_BUFFER = -2;

/** Capability bits used here. */
export const CAP_LEDGER_READ = 1n << 0n;
export const CAP_LEDGER_POST = 1n << 1n;
export const CAP_EVM = 1n << 9n;
export const CAP_ALL = (1n << 10n) - 1n;

/** Account kinds. */
export const HOLDER = 0;
export const ISSUER = 1;

/** Error raised for any non-OK libzxv status. */
export class ZxvError extends Error {
  /** @param {number} status @param {string} statusName @param {string} detail */
  constructor(status, statusName, detail) {
    super(`${statusName} (${status}): ${detail}`);
    this.status = status;
    this.statusName = statusName;
    this.detail = detail;
  }
}

/**
 * Load libzxv and bind the calls the channel flow needs.
 * @param {string} [libPath] defaults to $ZXV_LIBRARY_PATH, then sdk/libzxv/build/libzxv.{so,dylib}
 */
export function loadZxv(libPath) {
  const p = libPath ?? process.env.ZXV_LIBRARY_PATH ?? path.resolve(HERE, '../../libzxv/build', process.platform === 'darwin' ? 'libzxv.dylib' : process.platform === 'win32' ? 'zxv.dll' : 'libzxv.so');
  const lib = koffi.load(p);
  koffi.opaque('zxv_ctx');
  koffi.opaque('zxv_cap');
  const f = {
    version: lib.func('int32_t zxv_version(_Out_ uint32_t *major, _Out_ uint32_t *minor, _Out_ uint32_t *patch)'),
    statusName: lib.func('const char *zxv_status_name(int32_t status)'),
    lastError: lib.func('int32_t zxv_last_error(_Out_ uint8_t *buf, size_t cap, _Out_ size_t *out_len)'),
    ctxCreate: lib.func('int32_t zxv_ctx_create(_Out_ zxv_ctx **out)'),
    ctxDestroy: lib.func('void zxv_ctx_destroy(zxv_ctx *ctx)'),
    ccyRegisterPrivate: lib.func('int32_t zxv_ccy_register_private(zxv_ctx *ctx, const char *code, uint8_t minor)'),
    openAccount: lib.func('int32_t zxv_ledger_open_account(zxv_ctx *ctx, const char *name, const char *ccy, int32_t kind, _Out_ uint32_t *out_id)'),
    capCreate: lib.func('int32_t zxv_cap_create(zxv_ctx *ctx, uint32_t pid, const uint8_t *identity, uint64_t caps, _Out_ zxv_cap **out)'),
    capDestroy: lib.func('void zxv_cap_destroy(zxv_cap *cap)'),
    post: lib.func('int32_t zxv_ledger_post_cap(const zxv_cap *cap, uint32_t from, uint32_t to, int64_t amount, const char *ccy, const char *ref, _Out_ uint64_t *out_entry)'),
    balance: lib.func('int32_t zxv_ledger_balance_cap(const zxv_cap *cap, uint32_t account, _Out_ int64_t *d, _Out_ int64_t *c, _Out_ int64_t *e)'),
    encodeState: lib.func('int32_t zxv_evm_encode_state(const zxv_cap *cap, const uint8_t *channel_id, uint64_t nonce, const uint8_t *addresses, const int64_t *balances, uint32_t count, _Out_ uint8_t *buf, size_t cap_bytes, _Out_ size_t *out_len, _Out_ uint8_t *digest)'),
    ledgerState: lib.func('int32_t zxv_evm_ledger_state(const zxv_cap *cap, const uint8_t *channel_id, uint64_t nonce, const char *ccy, const uint32_t *accounts, const uint8_t *addresses, uint32_t count, _Out_ uint8_t *buf, size_t cap_bytes, _Out_ size_t *out_len, _Out_ uint8_t *digest)'),
  };
  const maj = [0], min = [0], pat = [0];
  f.version(maj, min, pat);
  if (maj[0] !== 1 || min[0] < 1) throw new Error(`libzxv ${maj[0]}.${min[0]} is not ABI 1.x (x >= 1)`);

  /** Throw ZxvError unless rc is ZXV_OK. */
  function check(rc) {
    if (rc === ZXV_OK) return;
    const buf = Buffer.alloc(512);
    const len = [0];
    f.lastError(buf, buf.length, len);
    throw new ZxvError(rc, f.statusName(rc), buf.subarray(0, Math.min(Number(len[0]), 511)).toString('utf8'));
  }
  return { f, check };
}

/**
 * A tiny ZXV ledger for one channel: a private unit, an issuer (the bridge
 * that mints units against on-chain deposits) and one holder account per
 * party. Real deployments use a full language SDK; this is the demo path.
 */
export class ZxvLedger {
  /** @param {ReturnType<typeof loadZxv>} zxv @param {string} unit e.g. "GWEI" */
  constructor(zxv, unit = 'GWEI') {
    this.zxv = zxv;
    this.unit = unit;
    const out = [null];
    zxv.check(zxv.f.ctxCreate(out));
    this.ctx = out[0];
    zxv.check(zxv.f.ccyRegisterPrivate(this.ctx, unit, 0));
    const cap = [null];
    zxv.check(zxv.f.capCreate(this.ctx, 1, null, CAP_ALL, cap));
    this.cap = cap[0];
    this.bridge = this.openAccount('bridge', ISSUER);
    this.refs = 0;
  }

  /** Open an account; returns its id. */
  openAccount(name, kind = HOLDER) {
    const id = [0];
    this.zxv.check(this.zxv.f.openAccount(this.ctx, name, this.unit, kind, id));
    return id[0];
  }

  /** Post `units` from one account to another (exact integers). */
  transfer(from, to, units) {
    const entry = [0n];
    this.zxv.check(this.zxv.f.post(this.cap, from, to, BigInt(units), this.unit, `ref-${++this.refs}`, entry));
    return entry[0];
  }

  /** EQUITY (888) balance of an account. */
  equity(account) {
    const d = [0n], c = [0n], e = [0n];
    this.zxv.check(this.zxv.f.balance(this.cap, account, d, c, e));
    return BigInt(e[0]);
  }

  /**
   * zxv_evm_ledger_state: the channel state of `accounts` (each party's
   * EQUITY), encoded for ZxvStateChannel.
   * @returns {{encoding: string, digest: string, balances: bigint[]}}
   */
  channelState(channelId, nonce, accounts, addresses) {
    const n = accounts.length;
    const need = (6 + 2 * n) * 32;
    const buf = Buffer.alloc(need);
    const digest = Buffer.alloc(32);
    const outLen = [0];
    const addrBuf = Buffer.concat(addresses.map((a) => Buffer.from(getBytes(a))));
    this.zxv.check(this.zxv.f.ledgerState(this.cap, Buffer.from(getBytes(channelId)), BigInt(nonce), this.unit,
      Uint32Array.from(accounts), addrBuf, n, buf, buf.length, outLen, digest));
    const encoding = hexlify(buf.subarray(0, Number(outLen[0])));
    return { encoding, digest: hexlify(digest), balances: decodeState(encoding).balances };
  }

  /** Free the native context. */
  close() {
    this.zxv.f.capDestroy(this.cap);
    this.zxv.f.ctxDestroy(this.ctx);
  }
}

/** Encode a state from explicit balances (zxv_evm_encode_state). */
export function encodeState(zxv, cap, channelId, nonce, addresses, balances) {
  const n = addresses.length;
  const buf = Buffer.alloc((6 + 2 * n) * 32);
  const digest = Buffer.alloc(32);
  const outLen = [0];
  const addrBuf = Buffer.concat(addresses.map((a) => Buffer.from(getBytes(a))));
  zxv.check(zxv.f.encodeState(cap, Buffer.from(getBytes(channelId)), BigInt(nonce), addrBuf,
    BigInt64Array.from(balances.map(BigInt)), n, buf, buf.length, outLen, digest));
  return { encoding: hexlify(buf.subarray(0, Number(outLen[0]))), digest: hexlify(digest) };
}

/** Decode an encoding back into its four fields. */
export function decodeState(encoding) {
  const [channelId, nonce, parties, balances] = AbiCoder.defaultAbiCoder()
    .decode(['bytes32', 'uint256', 'address[]', 'int256[]'], encoding);
  return { channelId, nonce, parties: [...parties], balances: [...balances] };
}

/**
 * One party signs a state digest the way the contract checks it:
 * EIP-191 personal message over the 32 digest bytes, 65-byte r||s||v.
 * @param {import('ethers').Signer} wallet
 */
export async function signState(wallet, digest) {
  const sig = await wallet.signMessage(getBytes(digest));
  return Signature.from(sig).serialized;
}

/**
 * Collect every party's signature (in party order) and build the
 * submitState arguments.
 */
export async function buildSubmission(channelId, nonce, state, wallets) {
  const signatures = [];
  for (const w of wallets) signatures.push(await signState(w, state.digest));
  return [channelId, BigInt(nonce), state.balances, signatures];
}
