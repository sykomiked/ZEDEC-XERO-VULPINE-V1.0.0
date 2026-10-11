// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The whole off-chain -> on-chain flow, on an in-process EVM:
//
//   1. two parties open a channel and deposit ETH
//   2. the ZXV ledger mirrors the deposits (1 unit = 1 gwei) and they trade
//   3. zxv_evm_ledger_state turns the ledger into the channel state
//   4. both wallets sign the digest (EIP-191); anyone submits
//   5. after the challenge window: settle, then each party withdraws
//
//   npm run example

import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { Wallet, formatEther, toBeHex } from 'ethers';
import { compile, LocalChain } from '../test/localchain.mjs';
import { loadZxv, ZxvLedger, buildSubmission } from '../offchain/zxv-channel.mjs';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const GWEI = 10n ** 9n;
const PERIOD = 3600n;

const art = compile([path.join(ROOT, 'ZxvStateChannel.sol')]);
const chain = await LocalChain.create();
const operator = Wallet.createRandom().address;
const alice = Wallet.createRandom();
const bob = Wallet.createRandom();
for (const a of [operator, alice.address, bob.address]) await chain.setBalance(a, 10n ** 19n);
const channel = await chain.deploy(art.ZxvStateChannel, operator);
console.log(`ZxvStateChannel at ${channel.address} (solc ${art.solcVersion.split('+')[0]})`);

// 1. open + deposit
const parties = [alice.address, bob.address];
const salt = toBeHex(42, 32);
const id = await channel.view('channelIdFor', [operator, parties, GWEI, PERIOD, salt], operator);
await channel.send(operator, 'open', [parties, GWEI, PERIOD, salt]);
await channel.send(alice.address, 'deposit', [id], 10n ** 18n); // 1 ETH
await channel.send(bob.address, 'deposit', [id], 5n * 10n ** 17n); // 0.5 ETH
console.log(`channel ${id}\n  deposits: alice 1 ETH, bob 0.5 ETH`);

// 2. the ZXV ledger, off chain
const zxv = loadZxv();
const ledger = new ZxvLedger(zxv, 'GWEI');
const [a, b] = [ledger.openAccount('alice'), ledger.openAccount('bob')];
ledger.transfer(ledger.bridge, a, 10n ** 18n / GWEI);
ledger.transfer(ledger.bridge, b, 5n * 10n ** 17n / GWEI);
for (let i = 0; i < 3; i++) ledger.transfer(a, b, 33_333_333n); // three exact payments
console.log(`  ledger: alice ${ledger.equity(a)} GWEI, bob ${ledger.equity(b)} GWEI`);

// 3. ledger -> state
const state = ledger.channelState(id, 1, [a, b], parties);
console.log(`  state nonce 1 digest ${state.digest}`);

// 4. sign + submit
await channel.send(operator, 'submitState', await buildSubmission(id, 1, state, [alice, bob]));

// 5. wait out the window, settle, withdraw
chain.advance(PERIOD);
await channel.send(operator, 'settle', [id]);
for (const [name, w] of [['alice', alice], ['bob', bob]]) {
  const due = await channel.view('withdrawable', [w.address]);
  await channel.send(w.address, 'withdraw', []);
  console.log(`  ${name} withdrew ${formatEther(due)} ETH`);
}
console.log(`  contract balance now ${await chain.balanceOf(channel.address)} wei`);
ledger.close();
