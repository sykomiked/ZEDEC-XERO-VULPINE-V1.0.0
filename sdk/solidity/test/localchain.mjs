// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// A minimal in-process EVM for tests: solc (wasm) compiles, @ethereumjs/evm
// executes, ethers encodes calls. Time is a variable the test moves.

import solc from 'solc';
import fs from 'node:fs';
import path from 'node:path';
import { createEVM } from '@ethereumjs/evm';
import { Address, createAccount, createAddressFromString } from '@ethereumjs/util';
import { Interface } from 'ethers';

/** Compile Solidity sources; returns { name: { abi, bytecode } }. */
export function compile(files) {
  const sources = {};
  for (const f of files) sources[path.basename(f)] = { content: fs.readFileSync(f, 'utf8') };
  const input = {
    language: 'Solidity',
    sources,
    settings: {
      optimizer: { enabled: true, runs: 200 },
      evmVersion: 'cancun',
      outputSelection: { '*': { '*': ['abi', 'evm.bytecode.object'] } },
    },
  };
  const out = JSON.parse(solc.compile(JSON.stringify(input)));
  const errors = (out.errors ?? []).filter((e) => e.severity === 'error');
  if (errors.length) throw new Error(errors.map((e) => e.formattedMessage).join('\n'));
  const result = { warnings: (out.errors ?? []).filter((e) => e.severity !== 'error') };
  for (const file of Object.values(out.contracts)) {
    for (const [name, c] of Object.entries(file)) result[name] = { abi: c.abi, bytecode: '0x' + c.evm.bytecode.object };
  }
  result.solcVersion = solc.version();
  return result;
}

const hexToBytes = (h) => Uint8Array.from(Buffer.from(h.replace(/^0x/, ''), 'hex'));
const bytesToHex = (b) => '0x' + Buffer.from(b).toString('hex');

/** Revert raised by LocalChain.call; `data` is the revert payload. */
export class Revert extends Error {
  constructor(data, iface) {
    let name = 'revert';
    try {
      const parsed = iface?.parseError(data);
      if (parsed) name = parsed.name;
    } catch { /* unknown error selector */ }
    super(`reverted: ${name}`);
    this.errorName = name;
    this.data = data;
  }
}

export class LocalChain {
  static async create() {
    const c = new LocalChain();
    c.evm = await createEVM();
    c.timestamp = 1_700_000_000n;
    c.number = 1n;
    return c;
  }

  block() {
    const ts = this.timestamp;
    const n = this.number;
    return {
      header: {
        number: n,
        coinbase: new Address(new Uint8Array(20)),
        timestamp: ts,
        difficulty: 0n,
        prevRandao: new Uint8Array(32),
        gasLimit: 30_000_000n,
        baseFeePerGas: 0n,
        getBlobGasPrice: () => 0n,
      },
    };
  }

  /** Move time forward by `seconds`. */
  advance(seconds) {
    this.timestamp += BigInt(seconds);
    this.number += 1n;
  }

  async setBalance(addr, wei) {
    const a = createAddressFromString(addr);
    await this.evm.stateManager.putAccount(a, createAccount({ nonce: 0n, balance: wei }));
  }

  async balanceOf(addr) {
    const acc = await this.evm.stateManager.getAccount(createAddressFromString(addr));
    return acc ? acc.balance : 0n;
  }

  async deploy(artifact, from, args = []) {
    const iface = new Interface(artifact.abi);
    const data = artifact.bytecode + iface.encodeDeploy(args).slice(2);
    const res = await this.evm.runCall({
      caller: createAddressFromString(from),
      origin: createAddressFromString(from),
      data: hexToBytes(data),
      gasLimit: 30_000_000n,
      block: this.block(),
    });
    if (res.execResult.exceptionError) throw new Error('deploy failed: ' + res.execResult.exceptionError.error);
    return new Contract(this, res.createdAddress.toString(), iface);
  }
}

export class Contract {
  constructor(chain, address, iface) {
    this.chain = chain;
    this.address = address;
    this.iface = iface;
  }

  /** Execute (state-changing) `fn` as `from`. Throws Revert on revert. */
  async send(from, fn, args = [], value = 0n) {
    const data = this.iface.encodeFunctionData(fn, args);
    const res = await this.chain.evm.runCall({
      caller: createAddressFromString(from),
      origin: createAddressFromString(from),
      to: createAddressFromString(this.address),
      data: hexToBytes(data),
      value,
      gasLimit: 30_000_000n,
      block: this.chain.block(),
    });
    const ex = res.execResult;
    if (ex.exceptionError) throw new Revert(bytesToHex(ex.returnValue), this.iface);
    const decoded = this.iface.decodeFunctionResult(fn, bytesToHex(ex.returnValue));
    return { result: decoded, gasUsed: ex.executionGasUsed, logs: ex.logs ?? [] };
  }

  /** Read-only call; returns the decoded result (single value unwrapped). */
  async view(fn, args = [], from = '0x' + '00'.repeat(20)) {
    const r = await this.send(from, fn, args);
    return r.result.length === 1 ? r.result[0] : r.result;
  }
}
