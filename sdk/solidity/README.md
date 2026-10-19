<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# ZXV Solidity / EVM adapter

`ZxvStateChannel.sol` settles the exact integer balances of a ZXV ledger on
any EVM chain. The ledger runs off chain in `libzxv`; the chain only ever
sees a final state that every party signed, and pays it out in ETH.

The state is byte for byte what `zxv_evm_encode_state` and
`zxv_evm_ledger_state` produce:

```
encoding = abi.encode(bytes32 channelId, uint256 nonce, address[] parties, int256[] balances)
digest   = keccak256(encoding)
signed   = keccak256("\x19Ethereum Signed Message:\n32" ++ digest)   // EIP-191
```

## Install

```sh
cd sdk/solidity
npm install          # solc (wasm), ethers, @ethereumjs/evm, koffi: dev-only
npm test             # compiles and runs the contract in an in-process EVM
npm run example      # the full off-chain -> on-chain flow
```

The contract has no imports: copy `ZxvStateChannel.sol` into a Foundry or
Hardhat project as is (`pragma solidity ^0.8.24`).

## Quickstart (off chain, Node)

```js
import { loadZxv, ZxvLedger, buildSubmission } from './offchain/zxv-channel.mjs';
const ledger = new ZxvLedger(loadZxv(), 'GWEI');          // 1 unit = 1 gwei
const [a, b] = [ledger.openAccount('alice'), ledger.openAccount('bob')];
ledger.transfer(ledger.bridge, a, 1_000_000_000n);         // mirror alice's 1 ETH deposit
ledger.transfer(a, b, 250_000_000n);                       // trade off chain, exactly
const state = ledger.channelState(channelId, 1, [a, b], [alice.address, bob.address]);
const args = await buildSubmission(channelId, 1, state, [aliceWallet, bobWallet]);
await contract.submitState(...args);                       // any account may submit
// after challengePeriod: contract.settle(channelId); each party: contract.withdraw()
```

## Concepts

| ZXV | On chain |
|---|---|
| Ledger account EQUITY (888) balance, exact `int64` minor units | `balances[i]`, `int256`, the party's final holding |
| A private ledger unit, e.g. `GWEI` with 0 minor units | `unitWei`: wei per ledger unit, fixed when the channel opens |
| Conservation (sum of debits = sum of credits) | `sum(balances) * unitWei == totalDeposits`, checked on every submission |
| Holder accounts never go below zero | every balance must be `>= 0`; a negative balance is refused (`NegativeBalance`) |
| `zxv_cap` with `ZXV_CAP_EVM` + `ZXV_CAP_LEDGER_READ` | needed to call `zxv_evm_ledger_state` |
| No secp256k1 in ZXV | each party's own wallet signs the digest |

**Mapping signed ledger balances.** A ZXV ledger balance is signed (an
ISSUER account can be negative), and the encoding carries `int256` so it can
represent that. A channel, though, pays out real ETH, so it treats balances
as final holdings: each must be `>= 0` and they must add up to exactly the
deposits. The intended setup (used in the tests and the example): one ISSUER
"bridge" account mints units against each on-chain deposit, the parties
hold HOLDER accounts, and the channel state is built from the parties'
accounts only, whose equities are never negative and add up to what the
bridge minted.

**Why `unitWei`.** ZXV amounts are capped at 2^53 - 1 so they are exact in
every number representation. In wei that would be only ~0.009 ETH; with
`unitWei = 1 gwei` a channel holds up to ~9 million ETH, with no rounding.

**Channel ids** are derived on chain:
`keccak256(abi.encode(chainid, contract, opener, parties, unitWei, challengePeriod, salt))`
(`channelIdFor`). A signed state therefore cannot be replayed on another
chain, another deployment, or another channel.

## Contract API

| Function | Who | What |
|---|---|---|
| `channelIdFor(opener, parties, unitWei, period, salt)` | view | the id `open` will assign |
| `open(parties, unitWei, period, salt) -> channelId` | anyone | 2..64 distinct non-zero parties, period 60 s .. 30 days |
| `deposit(channelId)` payable | party | multiple of `unitWei`; only before the first submission |
| `submitState(channelId, nonce, balances, signatures)` | anyone | all parties' 65-byte signatures in party order; nonce >= 1; the first starts the challenge window, later ones need a strictly higher nonce and must land before the deadline |
| `closeWithDeposits(channelId)` | party | unilateral exit: puts the deposits state (nonce 0) on file and starts the window |
| `settle(channelId)` | anyone | after the deadline, credits `withdrawable` |
| `withdraw()` | anyone | pulls the caller's credit (reentrancy-guarded) |
| `stateDigest`, `encodeState`, `getChannel`, `withdrawable` | view | cross-checks and inspection |

Errors are custom errors (`BadSignature(i)`, `StaleNonce`, `SumMismatch`,
`NegativeBalance(i)`, `WindowOpen`, `WindowClosed`, `WrongStatus`, ...).
Signatures with a high `s` (EIP-2 malleable) or `v` not 27/28 are refused.

## Files

| Path | What |
|---|---|
| `ZxvStateChannel.sol` | the contract |
| `offchain/zxv-channel.mjs` | ledger -> `zxv_evm_ledger_state` -> sign -> submission (libzxv through `koffi`, signing through `ethers`) |
| `test/channel.test.mjs` | 10 tests: vectors.json `evm_state` encoding and digest on chain and in libzxv, a full open/deposit/submit/challenge/settle/withdraw cycle with real secp256k1 signatures, conservation, malleability, unilateral exit, reentrancy |
| `test/localchain.mjs` | solc + `@ethereumjs/evm` in-process chain used by the tests and the example |
| `test/ReentrantParty.sol` | test-only attacker contract |
| `examples/channel-flow.mjs` | runnable end-to-end example |

## Honest limits

- **Not audited.** Tested in an in-process EVM only; do not hold real value
  in it without an independent audit.
- **ETH only.** No ERC-20 support; one channel, one asset.
- **ZXV has no secp256k1.** libzxv computes the encoding and the Keccak-256
  digest; the parties' own wallets sign. ZXV's post-quantum keys (ML-DSA-87)
  are not checked on chain: Ethereum has no precompile for them.
- **All parties must sign** every state. A party that refuses to sign can
  only be answered with `closeWithDeposits`, which returns deposits, not
  the off-chain trades since then.
- **Liveness.** Like every state channel, a party must watch the chain and
  submit its newest state before the deadline. The deadline is fixed by the
  first submission and is not extended by later ones.
- **Deposits close at the first submission**, and a state must match the
  deposits exactly, so a deposit after states were signed requires re-signing.
- **No on-chain ZXV ledger.** The chain checks signatures and conservation,
  not the trades that led to the state.
- `offchain/zxv-channel.mjs` binds only the few libzxv calls this flow needs;
  it is a demo helper, not the Node SDK.
