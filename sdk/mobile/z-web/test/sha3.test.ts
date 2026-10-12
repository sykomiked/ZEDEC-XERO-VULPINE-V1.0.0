// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The TypeScript SHA3-256 against FIPS 202 examples, vectors.json, and
// libzxv's zxv_sha3_256 (the wasm build, in-process) on random inputs that
// cover every padding boundary.

import assert from 'node:assert/strict';
import { randomBytes } from 'node:crypto';
import { test } from 'node:test';

import { Zxv } from '@zedec/zxv';

import { isDigestHex, sha3_256, sha3_256Hex, toHex } from '../src/index.js';
import { V, wasm } from './helpers.js';

const enc = new TextEncoder();

test('FIPS 202 examples', () => {
  assert.equal(sha3_256Hex(new Uint8Array(0)), 'a7ffc6f8bf1ed76651c14756a061d662f580ff4de43b49fa82d80a4b80f8434a');
  assert.equal(sha3_256Hex(enc.encode('abc')), '3a985da74fe225b2045c172d6bd390bd855f086e3e9d525b46bfe24511431532');
  assert.equal(
    sha3_256Hex(enc.encode('abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq')),
    '41c0dba2a9d6240849100376a8235e2c82e1b9998a999e21db32dd97496d3376',
  );
  assert.equal(sha3_256Hex(new Uint8Array(1_000_000).fill(0x61)), '5c8875ae474a3634ba4fd55ec85bffd661f32aca75c6d699d0cdcb6c115891c1');
});

test('vectors.json hash entries', () => {
  assert.ok(V.hash.length > 0);
  for (const h of V.hash) assert.equal(sha3_256Hex(enc.encode(h.utf8)), h.sha3_256, h.utf8);
});

test('equals libzxv zxv_sha3_256 on every length 0..420 and on 3 MiB', async () => {
  const zxv = await Zxv.load({ backend: 'wasm', wasm: wasm.bytes });
  for (let n = 0; n <= 420; n++) {
    const d = new Uint8Array(randomBytes(n));
    assert.equal(toHex(sha3_256(d)), toHex(zxv.hashes.sha3_256(d)), `length ${n}`);
  }
  const big = new Uint8Array(randomBytes(3 << 20));
  assert.equal(sha3_256Hex(big), toHex(zxv.hashes.sha3_256(big)));
});

test('digest syntax', () => {
  assert.ok(isDigestHex('A'.repeat(64)));
  assert.ok(!isDigestHex('a'.repeat(63)));
  assert.ok(!isDigestHex('g'.repeat(64)));
  assert.ok(!isDigestHex(42));
});
