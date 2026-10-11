// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The Terminus health indicator: UP only for a live context.

import 'reflect-metadata';

import assert from 'node:assert/strict';
import { describe, it } from 'node:test';

import { Test } from '@nestjs/testing';
import type { ZxvContext, ZxvFatalError } from '@zedec/zxv';

import { ZXV_CONTEXT, ZxvFatalState, ZxvHealthIndicator, ZxvModule } from '../src/index.js';

async function start(onFatal?: (e: ZxvFatalError | undefined) => void) {
  const ref = await Test.createTestingModule({
    imports: [ZxvModule.forRoot({
      capabilities: { teller: 'LEDGER_READ, LEDGER_POST' },
      ...(onFatal ? { onFatal } : {}),
    })],
  }).compile();
  const app = ref.createNestApplication({ logger: false });
  await app.init();
  return app;
}

describe('ZxvHealthIndicator', () => {
  it('is up for a live context, with ABI, backend and capability data', async () => {
    const app = await start();
    try {
      const r = app.get(ZxvHealthIndicator).isHealthy();
      assert.equal(r['zxv'].status, 'up');
      assert.equal(r['zxv']['state'], 'live');
      assert.match(String(r['zxv']['abi']), /^1\.\d+\.\d+$/);
      assert.equal(r['zxv']['backend'], 'native');
      assert.deepEqual(r['zxv']['capabilities'], { teller: 'rights=LEDGER_READ|LEDGER_POST; attestation=TRUE; revoked=false' });
      assert.equal(app.get(ZxvHealthIndicator).isHealthy('ledger')['ledger'].status, 'up');
    } finally {
      await app.close();
    }
  });

  it('is down once the context is halted, and trips the fatal state once', async () => {
    let calls = 0;
    const app = await start(() => { calls++; });
    try {
      app.get<ZxvContext>(ZXV_CONTEXT).halt('test');
      const health = app.get(ZxvHealthIndicator);
      const r = health.isHealthy();
      assert.equal(r['zxv'].status, 'down');
      assert.deepEqual(r['zxv'], { state: 'halted', status: 'down' });
      health.isHealthy();
      assert.equal(app.get(ZxvFatalState).isTripped, true);
      assert.equal(app.get(ZxvFatalState).status, 'ZXV_E_POISONED');
      assert.equal(calls, 1);
    } finally {
      await app.close();
    }
  });

  it('is down once the context is closed', async () => {
    const app = await start();
    const health = app.get(ZxvHealthIndicator);
    const fatal = app.get(ZxvFatalState);
    await app.close();
    assert.deepEqual(health.isHealthy()['zxv'], { state: 'closed', status: 'down' });
    assert.equal(fatal.isTripped, false);
  });

  it('reports a revoked capability without failing', async () => {
    const app = await start();
    try {
      const { ZxvCapabilities } = await import('../src/index.js');
      app.get(ZxvCapabilities).get('teller').revoke();
      const r = app.get(ZxvHealthIndicator).isHealthy();
      assert.equal(r['zxv'].status, 'up');
      assert.match(String((r['zxv']['capabilities'] as Record<string, string>)['teller']), /revoked=true/);
    } finally {
      await app.close();
    }
  });
});
