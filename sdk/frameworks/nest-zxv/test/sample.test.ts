// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The sample application end to end, over HTTP, with the exact JSON parser.

import 'reflect-metadata';

import assert from 'node:assert/strict';
import { after, before, describe, it } from 'node:test';

import type { INestApplication } from '@nestjs/common';
import { Test } from '@nestjs/testing';
import request from 'supertest';

import { AppModule } from '../sample/app.module.js';
import { exactJson, ZxvFatalState } from '../src/index.js';

async function start(haltEndpoint: boolean): Promise<INestApplication> {
  const ref = await Test.createTestingModule({ imports: [AppModule.register({ haltEndpoint })] }).compile();
  const app = ref.createNestApplication({ bodyParser: false, logger: false });
  app.use(exactJson({ limit: 4096 }));
  await app.init();
  return app;
}

const json = (app: INestApplication, path: string, body: string) =>
  request(app.getHttpServer()).post(path).set('content-type', 'application/json').send(body);

describe('sample', () => {
  let app: INestApplication;
  before(async () => { app = await start(false); });
  after(async () => { await app.close(); });

  it('quotes exactly', async () => {
    const res = await json(app, '/quote', '{"price":"12.75","quantity":"1/3"}');
    assert.equal(res.status, 200);
    assert.deepEqual(res.body, { total: '4.25' });
    const third = await json(app, '/quote', '{"price":"1","quantity":"1/3"}');
    assert.deepEqual(third.body, { total: '1/3' });
    const ints = await json(app, '/quote', '{"price":3,"quantity":"0.5"}');
    assert.deepEqual(ints.body, { total: '1.5' });
  });

  it('refuses JSON floats anywhere in the body before parsing', async () => {
    for (const body of ['{"price":12.75,"quantity":"1"}', '{"price":"1","quantity":1.0}', '{"price":"1","quantity":1e3}',
      '{"price":"1","quantity":9007199254740993}', '{"price":"1","quantity":"1","x":[0.5]}']) {
      const res = await json(app, '/quote', body);
      assert.equal(res.status, 400, body);
      assert.equal(res.body.type, 'urn:zxv:problem:json_float');
    }
  });

  it('refuses malformed, unknown and inexact fields', async () => {
    assert.equal((await json(app, '/quote', '{"price":"1",')).body.type, 'urn:zxv:problem:json_syntax');
    assert.equal((await json(app, '/quote', '{"price":"1e1","quantity":"1"}')).status, 400);
    assert.equal((await json(app, '/quote', '{"price":"1","quantity":"1","discount":"0"}')).status, 400);
    assert.equal((await json(app, '/quote', '{"price":"1"}')).status, 400);
    assert.equal((await json(app, '/quote', `{"price":"${'1'.repeat(5000)}"}`)).status, 413);
  });

  it('adds query values exactly', async () => {
    const res = await request(app.getHttpServer()).get('/sum?a=0.1&b=0.2');
    assert.deepEqual(res.body, { sum: '0.3' });
    assert.equal((await request(app.getHttpServer()).get('/sum?a=1e-1&b=0.2')).status, 400);
    assert.equal((await request(app.getHttpServer()).get('/sum?a=0.1')).status, 400);
  });

  it('posts with teller, refuses the auditor 403, reads balances as exact strings', async () => {
    const paid = await json(app, '/payments', '{"amountMinor":15000,"reference":"E2E-1"}');
    assert.equal(paid.status, 201);
    assert.match(paid.body.entryId, /^\d+$/);
    const big = await json(app, '/payments', '{"amountMinor":"25","reference":"E2E-3"}');
    assert.equal(big.status, 201);

    const refused = await json(app, '/refunds', '{"amountMinor":100,"reference":"E2E-2"}');
    assert.equal(refused.status, 403);
    assert.deepEqual(refused.body, {
      type: 'urn:zxv:problem:zxv_e_cap', title: 'Operation refused', status: 403, code: 'ZXV_E_CAP', class: 'refused',
    });

    const dup = await json(app, '/payments', '{"amountMinor":1,"reference":"E2E-1"}');
    assert.equal(dup.status, 409);
    assert.ok(!dup.text.includes('E2E-1'));

    const balance = await request(app.getHttpServer()).get('/balance');
    assert.deepEqual(balance.body, { debit: '15025', credit: '0', equity: '15025' });
  });

  it('is ready', async () => {
    const res = await request(app.getHttpServer()).get('/health/ready');
    assert.equal(res.status, 200);
    assert.equal(res.body.status, 'ok');
    assert.equal(res.body.details.zxv.state, 'live');
  });
});

describe('sample after a halt', () => {
  it('answers 503 for ZXV calls, readiness DOWN, liveness unaffected', async () => {
    const app = await start(true);
    try {
      assert.equal((await request(app.getHttpServer()).get('/health/ready')).status, 200);
      assert.equal((await request(app.getHttpServer()).post('/admin/halt')).status, 202);

      const balance = await request(app.getHttpServer()).get('/balance');
      assert.equal(balance.status, 503);
      assert.equal(balance.body.code, 'ZXV_E_POISONED');
      const pay = await json(app, '/payments', '{"amountMinor":1,"reference":"AFTER"}');
      assert.equal(pay.status, 503);
      assert.equal(app.get(ZxvFatalState).isTripped, true);

      const ready = await request(app.getHttpServer()).get('/health/ready');
      assert.equal(ready.status, 503);
      assert.deepEqual(ready.body.details.zxv, { state: 'halted', status: 'down' });
      assert.equal((await request(app.getHttpServer()).get('/health/live')).status, 200);
      // A pure computation does not touch the context.
      assert.equal((await request(app.getHttpServer()).get('/sum?a=1&b=2')).body.sum, '3');
    } finally {
      await app.close();
    }
  });

  it('has no halt endpoint unless asked', async () => {
    const app = await start(false);
    try {
      assert.equal((await request(app.getHttpServer()).post('/admin/halt')).status, 404);
    } finally {
      await app.close();
    }
  });
});
