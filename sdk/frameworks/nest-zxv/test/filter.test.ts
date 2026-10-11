// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The exception filter over HTTP: every status class, no native text in
// any body, fatal answered 503 and tripping the state.

import 'reflect-metadata';

import assert from 'node:assert/strict';
import { describe, it } from 'node:test';

import { Controller, Get, type INestApplication, Param } from '@nestjs/common';
import { APP_FILTER } from '@nestjs/core';
import { Test } from '@nestjs/testing';
import {
  AccountKind, BudgetExhaustedError, type Capability, CapabilityDeniedError, InsufficientFundsError, Ledger, NotImplementedError,
  Status, VerificationFailedError, ZxvError, ZxvFatalError, ZxvRefusedError, ZxvSystemError, ZxvUsageError,
} from '@zedec/zxv';
import request from 'supertest';

import { InjectCapability, ZxvExceptionFilter, ZxvFatalState, ZxvModule } from '../src/index.js';

const SECRET = 'acct-4471-ADA-OKAFOR';

const CASES: Record<string, [() => ZxvError | ZxvFatalError, number, string, string]> = {
  arg: [() => new ZxvUsageError(Status.E_ARG, 'ZXV_E_ARG', SECRET), 400, 'ZXV_E_ARG', 'usage'],
  notfound: [() => new ZxvUsageError(Status.E_NOT_FOUND, 'ZXV_E_NOT_FOUND', SECRET), 404, 'ZXV_E_NOT_FOUND', 'usage'],
  duplicate: [() => new ZxvUsageError(Status.E_DUPLICATE, 'ZXV_E_DUPLICATE', SECRET), 409, 'ZXV_E_DUPLICATE', 'usage'],
  cap: [() => new CapabilityDeniedError(Status.E_CAP, 'ZXV_E_CAP', SECRET), 403, 'ZXV_E_CAP', 'refused'],
  funds: [() => new InsufficientFundsError(Status.E_FUNDS, 'ZXV_E_FUNDS', SECRET), 422, 'ZXV_E_FUNDS', 'refused'],
  verify: [() => new VerificationFailedError(Status.E_VERIFY, 'ZXV_E_VERIFY', SECRET), 422, 'ZXV_E_VERIFY', 'refused'],
  budget: [() => new BudgetExhaustedError(Status.E_BUDGET, 'ZXV_E_BUDGET', SECRET), 429, 'ZXV_E_BUDGET', 'refused'],
  notimpl: [() => new NotImplementedError(Status.E_NOTIMPL, 'ZXV_E_NOTIMPL', SECRET), 501, 'ZXV_E_NOTIMPL', 'refused'],
  capacity: [() => new ZxvRefusedError(Status.E_CAPACITY, 'ZXV_E_CAPACITY', SECRET), 503, 'ZXV_E_CAPACITY', 'refused'],
  state: [() => new ZxvRefusedError(Status.E_STATE, 'ZXV_E_STATE', SECRET), 409, 'ZXV_E_STATE', 'refused'],
  io: [() => new ZxvSystemError(Status.E_IO, 'ZXV_E_IO', SECRET), 500, 'ZXV_E_IO', 'system'],
  unknown: [() => new ZxvSystemError(-77, 'ZXV_E_UNKNOWN(-77)', SECRET), 500, 'ZXV_E_INTERNAL', 'system'],
  fatal: [() => new ZxvFatalError(Status.E_POISONED, 'ZXV_E_POISONED', SECRET), 503, 'ZXV_E_POISONED', 'fatal'],
};

@Controller()
class Throwing {
  readonly #ledger: Ledger;
  readonly #a: number;
  readonly #b: number;

  constructor(@InjectCapability('teller') teller: Capability) {
    this.#ledger = Ledger.of(teller);
    this.#a = this.#ledger.openAccount('bank', 'NGN', AccountKind.ISSUER);
    this.#b = this.#ledger.openAccount(SECRET.slice(0, 20), 'NGN');
  }

  @Get('throw/:kind')
  throwIt(@Param('kind') kind: string): never {
    const c = CASES[kind];
    if (c === undefined) throw new Error('not a ZXV error');
    throw c[0]();
  }

  /** A real native refusal whose detail names the reference. */
  @Get('duplicate')
  duplicate(): string {
    this.#ledger.post(this.#a, this.#b, 1n, 'NGN', SECRET);
    this.#ledger.post(this.#a, this.#b, 1n, 'NGN', SECRET);
    return 'unreachable';
  }

  /** A real native refusal: a holder going below zero. */
  @Get('overdraw')
  overdraw(): string {
    this.#ledger.post(this.#b, this.#a, 10_000_000n, 'NGN', 'OVER-1');
    return 'unreachable';
  }
}

async function start(): Promise<INestApplication> {
  const ref = await Test.createTestingModule({
    imports: [ZxvModule.forRoot({ currencies: ['NGN'], capabilities: { teller: 'LEDGER_READ,LEDGER_POST' } })],
    controllers: [Throwing],
    providers: [{ provide: APP_FILTER, useExisting: ZxvExceptionFilter }],
  }).compile();
  const app = ref.createNestApplication({ logger: false });
  await app.init();
  return app;
}

describe('ZxvExceptionFilter', () => {
  it('maps every status class to HTTP with a problem body and no native text', async () => {
    const app = await start();
    try {
      for (const [kind, [, status, code, cls]] of Object.entries(CASES)) {
        if (kind === 'fatal') continue;
        const res = await request(app.getHttpServer()).get(`/throw/${kind}`);
        assert.equal(res.status, status, kind);
        assert.match(res.headers['content-type'] ?? '', /^application\/problem\+json/);
        assert.equal(res.headers['cache-control'], 'no-store');
        assert.deepEqual(Object.keys(res.body).sort(), ['class', 'code', 'status', 'title', 'type']);
        assert.equal(res.body.code, code);
        assert.equal(res.body.class, cls);
        assert.equal(res.body.status, status);
        assert.equal(res.body.type, `urn:zxv:problem:${code.toLowerCase()}`);
        assert.ok(!res.text.includes(SECRET), `${kind} leaked the detail text`);
      }
      assert.equal(app.get(ZxvFatalState).isTripped, false);
    } finally {
      await app.close();
    }
  });

  it('answers real native refusals without their detail', async () => {
    const app = await start();
    try {
      const dup = await request(app.getHttpServer()).get('/duplicate');
      assert.equal(dup.status, 409);
      assert.equal(dup.body.code, 'ZXV_E_DUPLICATE');
      assert.ok(!dup.text.includes(SECRET));
      const funds = await request(app.getHttpServer()).get('/overdraw');
      assert.equal(funds.status, 422);
      assert.equal(funds.body.code, 'ZXV_E_FUNDS');
      assert.ok(!funds.text.includes('ADA'));
    } finally {
      await app.close();
    }
  });

  it('answers a fatal error 503 and trips the fatal state', async () => {
    const app = await start();
    try {
      const res = await request(app.getHttpServer()).get('/throw/fatal');
      assert.equal(res.status, 503);
      assert.equal(res.body.code, 'ZXV_E_POISONED');
      assert.equal(res.body.class, 'fatal');
      assert.equal(res.body.title, 'Service halted');
      assert.ok(!res.text.includes(SECRET));
      assert.equal(app.get(ZxvFatalState).isTripped, true);
    } finally {
      await app.close();
    }
  });

  it('leaves other exceptions to Nest', async () => {
    const app = await start();
    try {
      const res = await request(app.getHttpServer()).get('/throw/other');
      assert.equal(res.status, 500);
      assert.deepEqual(res.body, { statusCode: 500, message: 'Internal server error' });
    } finally {
      await app.close();
    }
  });
});
