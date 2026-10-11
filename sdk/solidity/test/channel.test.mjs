// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// ZxvStateChannel against a real (in-process) EVM, real secp256k1
// signatures (ethers wallets) and the real libzxv (koffi).
//
//   npm test

import { test, before } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { Wallet, keccak256, getBytes, hexlify, Signature, SigningKey, toBeHex } from 'ethers';
import { compile, LocalChain, Revert } from './localchain.mjs';
import {
  loadZxv, ZxvLedger, encodeState, decodeState, signState, buildSubmission, CAP_ALL, CAP_LEDGER_READ,
} from '../offchain/zxv-channel.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(HERE, '..');
const VECTORS = JSON.parse(fs.readFileSync(path.resolve(ROOT, '../conformance/vectors.json'), 'utf8'));
const EVM = VECTORS.evm_state;

const GWEI = 1_000_000_000n;
const PERIOD = 3600n;

let art, chain, channel, zxv;
const deployer = Wallet.createRandom().address;
// Fixed keys so failures are reproducible.
const alice = new Wallet('0x' + '11'.repeat(32));
const bob = new Wallet('0x' + '22'.repeat(32));
const carol = new Wallet('0x' + '33'.repeat(32));

async function expectRevert(promise, name) {
  await assert.rejects(promise, (e) => {
    assert.ok(e instanceof Revert, `expected a revert, got ${e}`);
    assert.equal(e.errorName, name);
    return true;
  });
}

async function openChannel(parties, salt, unit = GWEI, period = PERIOD) {
  const addrs = parties.map((p) => (typeof p === 'string' ? p : p.address));
  const id = await channel.view('channelIdFor', [deployer, addrs, unit, period, salt], deployer);
  const r = await channel.send(deployer, 'open', [addrs, unit, period, salt]);
  assert.equal(r.result[0], id);
  return id;
}

before(async () => {
  art = compile([path.join(ROOT, 'ZxvStateChannel.sol'), path.join(HERE, 'ReentrantParty.sol')]);
  chain = await LocalChain.create();
  for (const w of [deployer, alice.address, bob.address, carol.address]) await chain.setBalance(w, 1000n * 10n ** 18n);
  channel = await chain.deploy(art.ZxvStateChannel, deployer);
  zxv = loadZxv();
});

test('compiles with no warnings', () => {
  assert.deepEqual(art.warnings.map((w) => w.formattedMessage), []);
});

test('on-chain abi.encode and keccak256 equal vectors.json evm_state', async () => {
  const cid = '0x' + EVM.channel_id;
  const addrs = EVM.addresses.map((a) => '0x' + a);
  const enc = await channel.view('encodeState', [cid, EVM.nonce, addrs, EVM.balances]);
  assert.equal(enc, '0x' + EVM.encoding);
  const digest = await channel.view('stateDigest', [cid, EVM.nonce, addrs, EVM.balances]);
  assert.equal(digest, '0x' + EVM.digest);
  assert.equal(keccak256('0x' + EVM.encoding), '0x' + EVM.digest);
});

test('libzxv zxv_evm_encode_state equals the contract byte for byte', async () => {
  const L = new ZxvLedger(zxv);
  try {
    const cid = '0x' + EVM.channel_id;
    const addrs = EVM.addresses.map((a) => '0x' + a);
    const s = encodeState(zxv, L.cap, cid, EVM.nonce, addrs, EVM.balances);
    assert.equal(s.encoding, '0x' + EVM.encoding);
    assert.equal(s.digest, '0x' + EVM.digest);
    // A bigger, random-ish state: still identical to Solidity's encoder.
    const many = Array.from({ length: 9 }, (_, i) => Wallet.createRandom().address);
    const bals = many.map((_, i) => (i % 2 ? -1n : 1n) * (2n ** 53n - 1n - BigInt(i)));
    const s2 = encodeState(zxv, L.cap, keccak256('0x01'), 2n ** 63n, many, bals);
    assert.equal(s2.encoding, await channel.view('encodeState', [keccak256('0x01'), 2n ** 63n, many, bals]));
    assert.equal(s2.digest, await channel.view('stateDigest', [keccak256('0x01'), 2n ** 63n, many, bals]));
  } finally {
    L.close();
  }
});

test('the EVM capability is checked by libzxv', () => {
  const L = new ZxvLedger(zxv);
  const capOut = [null];
  zxv.check(zxv.f.capCreate(L.ctx, 2, null, CAP_LEDGER_READ, capOut));
  try {
    assert.throws(() => encodeState(zxv, capOut[0], '0x' + EVM.channel_id, 1, ['0x' + EVM.addresses[0]], [1]),
      (e) => e.statusName === 'ZXV_E_CAP');
  } finally {
    zxv.f.capDestroy(capOut[0]);
    L.close();
  }
});

test('full cycle: deposit -> ZXV ledger -> sign -> submit -> challenge -> settle -> withdraw', async () => {
  const parties = [alice, bob, carol];
  const addrs = parties.map((p) => p.address);
  const id = await openChannel(parties, toBeHex(1, 32));

  // 1. Deposits on chain (whole gwei).
  const dep = [5n * 10n ** 17n, 3n * 10n ** 17n, 2n * 10n ** 17n]; // 0.5, 0.3, 0.2 ETH
  for (let i = 0; i < 3; i++) await channel.send(addrs[i], 'deposit', [id], dep[i]);
  await expectRevert(channel.send(alice.address, 'deposit', [id], 1n), 'NotUnitMultiple');
  await expectRevert(channel.send(deployer, 'deposit', [id], GWEI), 'NotParty');

  // 2. The ZXV ledger mirrors them in GWEI units, then the parties trade.
  const L = new ZxvLedger(zxv, 'GWEI');
  try {
    const acct = addrs.map((_, i) => L.openAccount(`party-${i}`));
    for (let i = 0; i < 3; i++) L.transfer(L.bridge, acct[i], dep[i] / GWEI);
    L.transfer(acct[0], acct[1], 125_000_000n); // alice pays bob 0.125 ETH
    L.transfer(acct[2], acct[0], 1n); //          carol pays alice 1 gwei

    // 3. State nonce 1, from the ledger, signed by everyone.
    const s1 = L.channelState(id, 1, acct, addrs);
    assert.deepEqual(s1.balances, [375_000_001n, 425_000_000n, 199_999_999n]);
    assert.equal(s1.digest, await channel.view('stateDigest', [id, 1, addrs, s1.balances]));
    const sub1 = await buildSubmission(id, 1, s1, parties);

    // A wrong signer, a bad order, a tampered balance are all refused.
    const swapped = [sub1[3][1], sub1[3][0], sub1[3][2]];
    await expectRevert(channel.send(deployer, 'submitState', [id, 1, s1.balances, swapped]), 'BadSignature');
    const tampered = [s1.balances[0] - 1n, s1.balances[1] + 1n, s1.balances[2]];
    await expectRevert(channel.send(deployer, 'submitState', [id, 1, tampered, sub1[3]]), 'BadSignature');
    await expectRevert(channel.send(deployer, 'submitState', [id, 0, s1.balances, sub1[3]]), 'StaleNonce');

    await channel.send(deployer, 'submitState', sub1);
    await expectRevert(channel.send(alice.address, 'deposit', [id], GWEI), 'WrongStatus');

    // 4. A later state (nonce 2) supersedes it inside the window.
    L.transfer(acct[1], acct[2], 25_000_000n);
    const s2 = L.channelState(id, 2, acct, addrs);
    const sub2 = await buildSubmission(id, 2, s2, parties);
    chain.advance(60);
    await channel.send(bob.address, 'submitState', sub2);
    // Replaying the older state is stale.
    await expectRevert(channel.send(deployer, 'submitState', sub1), 'StaleNonce');
    await expectRevert(channel.send(deployer, 'settle', [id]), 'WindowOpen');

    // 5. After the window, settle credits, withdraw pays.
    chain.advance(PERIOD);
    await expectRevert(channel.send(deployer, 'submitState', await buildSubmission(id, 3, L.channelState(id, 3, acct, addrs), parties)), 'WindowClosed');
    await channel.send(deployer, 'settle', [id]);
    await expectRevert(channel.send(deployer, 'settle', [id]), 'WrongStatus');

    const expected = s2.balances.map((b) => b * GWEI);
    assert.equal(expected.reduce((a, b) => a + b), dep.reduce((a, b) => a + b));
    for (let i = 0; i < 3; i++) {
      assert.equal(await channel.view('withdrawable', [addrs[i]]), expected[i]);
      const before = await chain.balanceOf(addrs[i]);
      await channel.send(addrs[i], 'withdraw', []);
      assert.equal(await chain.balanceOf(addrs[i]), before + expected[i]);
    }
    await expectRevert(channel.send(alice.address, 'withdraw', []), 'NothingToWithdraw');
    assert.equal(await chain.balanceOf(channel.address), 0n);
    const info = await channel.view('getChannel', [id]);
    assert.equal(info.status, 3n);
    assert.equal(info.nonce, 2n);
  } finally {
    L.close();
  }
});

test('conservation: negative holdings and wrong sums are refused', async () => {
  const parties = [alice, bob];
  const addrs = parties.map((p) => p.address);
  const id = await openChannel(parties, toBeHex(2, 32), 1n);
  await channel.send(alice.address, 'deposit', [id], 1000n);
  await channel.send(bob.address, 'deposit', [id], 500n);
  const L = new ZxvLedger(zxv);
  try {
    // An encoded ledger state may be negative (ISSUER accounts), but a
    // channel payout may not.
    const neg = encodeState(zxv, L.cap, id, 1, addrs, [1600n, -100n]);
    await expectRevert(channel.send(deployer, 'submitState', await buildSubmission(id, 1, { ...neg, balances: [1600n, -100n] }, parties)), 'NegativeBalance');
    const over = encodeState(zxv, L.cap, id, 1, addrs, [1000n, 501n]);
    await expectRevert(channel.send(deployer, 'submitState', await buildSubmission(id, 1, { ...over, balances: [1000n, 501n] }, parties)), 'SumMismatch');
    const ok = encodeState(zxv, L.cap, id, 1, addrs, [0n, 1500n]);
    await channel.send(deployer, 'submitState', await buildSubmission(id, 1, { ...ok, balances: [0n, 1500n] }, parties));
  } finally {
    L.close();
  }
});

test('high-s (malleable) signatures are refused', async () => {
  const parties = [alice, bob];
  const addrs = parties.map((p) => p.address);
  const id = await openChannel(parties, toBeHex(3, 32), 1n);
  await channel.send(alice.address, 'deposit', [id], 10n);
  const L = new ZxvLedger(zxv);
  try {
    const st = encodeState(zxv, L.cap, id, 1, addrs, [4n, 6n]);
    const sigs = [await signState(alice, st.digest), await signState(bob, st.digest)];
    const s = Signature.from(sigs[0]);
    const N = 0xfffffffffffffffffffffffffffffffebaaedce6af48a03bbfd25e8cd0364141n;
    const highS = hexlify(new Uint8Array([...getBytes(s.r), ...getBytes(toBeHex(N - BigInt(s.s), 32)), s.v === 27 ? 28 : 27]));
    await expectRevert(channel.send(deployer, 'submitState', [id, 1, [4n, 6n], [highS, sigs[1]]]), 'BadSignature');
    await channel.send(deployer, 'submitState', [id, 1, [4n, 6n], sigs]);
  } finally {
    L.close();
  }
});

test('closeWithDeposits: a party can exit without signatures; a signed state still wins', async () => {
  const parties = [alice, bob];
  const addrs = parties.map((p) => p.address);
  const id = await openChannel(parties, toBeHex(4, 32), 1n);
  await channel.send(alice.address, 'deposit', [id], 70n);
  await channel.send(bob.address, 'deposit', [id], 30n);
  await expectRevert(channel.send(deployer, 'closeWithDeposits', [id]), 'NotParty');
  await channel.send(bob.address, 'closeWithDeposits', [id]);
  let info = await channel.view('getChannel', [id]);
  assert.deepEqual([...info.balances], [70n, 30n]);
  const L = new ZxvLedger(zxv);
  try {
    const st = encodeState(zxv, L.cap, id, 1, addrs, [40n, 60n]);
    await channel.send(alice.address, 'submitState', await buildSubmission(id, 1, { ...st, balances: [40n, 60n] }, parties));
  } finally {
    L.close();
  }
  chain.advance(PERIOD);
  await channel.send(deployer, 'settle', [id]);
  info = await channel.view('getChannel', [id]);
  assert.deepEqual([...info.balances], [40n, 60n]);
});

test('withdraw is reentrancy-safe (pull pattern + guard)', async () => {
  const attacker = await chain.deploy(art.ReentrantParty, deployer, [channel.address]);
  const id = await openChannel([attacker.address, alice.address], toBeHex(5, 32), 1n);
  await attacker.send(deployer, 'fund', [id], 100n);
  await channel.send(alice.address, 'deposit', [id], 100n);
  await attacker.send(deployer, 'close', [id]);
  chain.advance(PERIOD);
  await channel.send(deployer, 'settle', [id]);
  const before = await chain.balanceOf(channel.address);
  // The re-entrant withdraw hits the guard, so the outer transfer fails and
  // the whole withdrawal reverts: nothing leaves the contract.
  await assert.rejects(attacker.send(deployer, 'attack', []), (e) => e instanceof Revert);
  assert.equal(await chain.balanceOf(channel.address), before);
  assert.equal(await channel.view('withdrawable', [attacker.address]), 100n);
  await channel.send(alice.address, 'withdraw', []);
});

test('open validates parties and parameters', async () => {
  await expectRevert(channel.send(deployer, 'open', [[alice.address], 1n, PERIOD, toBeHex(9, 32)]), 'BadParties');
  await expectRevert(channel.send(deployer, 'open', [[alice.address, alice.address], 1n, PERIOD, toBeHex(9, 32)]), 'BadParties');
  await expectRevert(channel.send(deployer, 'open', [[alice.address, bob.address], 0n, PERIOD, toBeHex(9, 32)]), 'BadParameter');
  await expectRevert(channel.send(deployer, 'open', [[alice.address, bob.address], 1n, 1n, toBeHex(9, 32)]), 'BadParameter');
  const id = await openChannel([alice, bob], toBeHex(9, 32), 1n);
  await expectRevert(channel.send(deployer, 'open', [[alice.address, bob.address], 1n, PERIOD, toBeHex(9, 32)]), 'WrongStatus');
  assert.notEqual(id, '0x' + '00'.repeat(32));
});
