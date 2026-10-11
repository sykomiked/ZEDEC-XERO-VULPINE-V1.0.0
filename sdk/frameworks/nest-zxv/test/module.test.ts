// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// ZxvModule against the real libzxv (native backend): providers, the root
// never injectable, option validation, async options, shutdown order.

import 'reflect-metadata';

import assert from 'node:assert/strict';
import { describe, it } from 'node:test';

import { Inject, Injectable, Module } from '@nestjs/common';
import { ModulesContainer } from '@nestjs/core';
import { Test } from '@nestjs/testing';
import { AccountKind, Cap, Capability, CapabilityDeniedError, Ledger, Zxv, ZxvContext, ZxvUsageError } from '@zedec/zxv';

import {
  capabilityToken, InjectCapability, ZXV_CONTEXT, ZXV_LIBRARY, ZxvCapabilities, ZxvConfigurationError, ZxvModule,
  type ZxvModuleOptions,
} from '../src/index.js';

const TELLER_AUDITOR: ZxvModuleOptions = {
  currencies: ['NGN'],
  privateUnits: [{ code: 'VFV', minorUnits: 2 }],
  rootRights: ['LEDGER_READ', 'LEDGER_POST'],
  capabilities: { teller: ['LEDGER_READ', 'LEDGER_POST'], auditor: 'ledger_read' },
};

@Injectable()
class Teller {
  constructor(@InjectCapability('teller') readonly cap: Capability) {}
}

async function start(options: ZxvModuleOptions = TELLER_AUDITOR) {
  const providers = options.capabilities?.['teller'] === undefined ? [] : [Teller];
  const ref = await Test.createTestingModule({ imports: [ZxvModule.forRoot(options)], providers }).compile();
  const app = ref.createNestApplication({ logger: false });
  await app.init();
  return app;
}

describe('ZxvModule.forRoot', () => {
  it('provides a live context, the library and narrow named capabilities', async () => {
    const app = await start();
    try {
      const ctx = app.get<ZxvContext>(ZXV_CONTEXT);
      const zxv = app.get<Zxv>(ZXV_LIBRARY);
      assert.ok(ctx instanceof ZxvContext);
      assert.equal(ctx.isPoisoned, false);
      assert.equal(zxv.backend, 'native');
      assert.ok(zxv.version.major === 1 && zxv.version.minor >= 3);
      assert.deepEqual(ctx.enabledCurrencies(), ['NGN', 'VFV']);

      const teller = app.get(Teller).cap;
      const auditor = app.get<Capability>(capabilityToken('auditor'));
      assert.equal(teller.info().caps, Cap.LEDGER_READ | Cap.LEDGER_POST);
      assert.equal(auditor.info().caps, Cap.LEDGER_READ);

      const post = Ledger.of(teller);
      const bank = post.openAccount('bank', 'NGN', AccountKind.ISSUER);
      const ada = post.openAccount('ada', 'NGN');
      post.post(bank, ada, 150_00n, 'NGN', 'E2E-1');
      assert.equal(Ledger.of(auditor).balance(ada).equity, 150_00n);
      assert.throws(() => Ledger.of(auditor).post(ada, bank, 1n, 'NGN', 'E2E-2'), CapabilityDeniedError);
    } finally {
      await app.close();
    }
  });

  it('never makes the root capability injectable', async () => {
    const app = await start();
    try {
      const caps = app.get(ZxvCapabilities);
      assert.deepEqual(caps.names, ['teller', 'auditor']);
      assert.throws(() => caps.get('root'));
      assert.throws(() => app.get(capabilityToken('root')));

      // Every Capability any provider holds is one of the named ones.
      const named = new Set([caps.get('teller'), caps.get('auditor')]);
      const seen: Capability[] = [];
      for (const mod of app.get(ModulesContainer).values()) {
        for (const wrapper of mod.providers.values()) {
          if (wrapper.instance instanceof Capability) seen.push(wrapper.instance);
        }
      }
      assert.ok(seen.length >= 2);
      for (const c of seen) assert.ok(named.has(c), 'a provider holds a capability that is not a named one');
    } finally {
      await app.close();
    }
  });

  it('refuses options that cannot work, listing every problem', () => {
    const bad: [ZxvModuleOptions, RegExp][] = [
      [{ capabilities: { teller: ['LEDGER_PRINT' as 'LEDGER_POST'] } }, /unknown right "LEDGER_PRINT"/],
      [{ rootRights: ['LEDGER_READ'], capabilities: { teller: ['LEDGER_POST'] } }, /which the root does not hold/],
      [{ capabilities: { root: ['LEDGER_READ'] } }, /'root' is reserved/],
      [{ capabilities: { 'tel ler': ['LEDGER_READ'] } }, /not a valid name/],
      [{ capabilities: { teller: [] } }, /holds no right/],
      [{ capabilities: { teller: 3 as unknown as bigint } }, /rights must be names/],
      [{ capabilities: { teller: 1n << 40n } }, /not a non-empty set of Cap bits/],
      [{ currencies: ['naira'] }, /not an ISO 4217 alpha-3 code/],
      [{ privateUnits: [{ code: 'vfv', minorUnits: 9 }] }, /upper-case[\s\S]*integer 0..8/],
      [{ backend: 'gpu' as 'native' }, /backend/],
      [{ fatalPolicy: 'ignore' as 'throw' }, /fatalPolicy/],
    ];
    for (const [options, message] of bad) {
      assert.throws(() => ZxvModule.forRoot(options), (e: unknown) => e instanceof ZxvConfigurationError && message.test(e.message));
    }
  });

  it('fails start-up for a currency libzxv does not accept', async () => {
    await assert.rejects(start({ currencies: ['XXX'] }), ZxvUsageError);
  });
});

@Injectable()
class Settings {
  readonly zxv: ZxvModuleOptions = { currencies: ['GHS'], capabilities: { reader: 'LEDGER_READ' } };
}

@Module({ providers: [Settings], exports: [Settings] })
class SettingsModule {}

describe('ZxvModule.forRootAsync', () => {
  it('builds the context from a factory', async () => {
    const ref = await Test.createTestingModule({
      imports: [
        ZxvModule.forRootAsync({
          imports: [SettingsModule],
          inject: [Settings],
          useFactory: (s: Settings) => s.zxv,
          capabilityNames: ['reader'],
        }),
      ],
    }).compile();
    const app = ref.createNestApplication({ logger: false });
    await app.init();
    try {
      assert.deepEqual(app.get<ZxvContext>(ZXV_CONTEXT).enabledCurrencies(), ['GHS']);
      assert.equal(app.get<Capability>(capabilityToken('reader')).info().caps, Cap.LEDGER_READ);
    } finally {
      await app.close();
    }
  });

  it('refuses a factory whose capabilities differ from the declared names', async () => {
    const build = Test.createTestingModule({
      imports: [ZxvModule.forRootAsync({ useFactory: async () => ({ capabilities: { writer: 'LEDGER_POST' } }), capabilityNames: ['reader'] })],
    }).compile();
    await assert.rejects(build, (e: unknown) => e instanceof ZxvConfigurationError
      && /writer: not listed/.test(e.message) && /'reader' has no rights/.test(e.message));
  });

  it('refuses a reserved or duplicate declared name at once', () => {
    assert.throws(() => ZxvModule.forRootAsync({ useFactory: () => ({}), capabilityNames: ['root'] }), ZxvConfigurationError);
    assert.throws(() => ZxvModule.forRootAsync({ useFactory: () => ({}), capabilityNames: ['a', 'a'] }), ZxvConfigurationError);
  });
});

describe('shutdown', () => {
  it('closes named capabilities, then the root, then the context (ABI.md R6)', async () => {
    const app = await start();
    const ctx = app.get<ZxvContext>(ZXV_CONTEXT);
    const caps = app.get(ZxvCapabilities);
    const names = new Map<object, string>([[caps.get('teller'), 'teller'], [caps.get('auditor'), 'auditor'], [ctx, 'context']]);
    const order: string[] = [];

    const capClose = Capability.prototype.close;
    const ctxClose = ZxvContext.prototype.close;
    Capability.prototype.close = function close(this: Capability) {
      if (!this.closed && this.context === ctx) order.push(names.get(this) ?? 'root');
      capClose.call(this);
    };
    ZxvContext.prototype.close = function close(this: ZxvContext) {
      if (!this.closed && this === ctx) order.push('context');
      ctxClose.call(this);
    };
    try {
      await app.close();
    } finally {
      Capability.prototype.close = capClose;
      ZxvContext.prototype.close = ctxClose;
    }

    assert.deepEqual(order, ['auditor', 'teller', 'root', 'context']);
    assert.equal(ctx.closed, true);
    assert.equal(caps.get('teller').closed, true);
  });

  it('lets other providers use the context in onModuleDestroy', async () => {
    let balanceAtDestroy: bigint | undefined;

    @Injectable()
    class Reader {
      readonly #ledger: Ledger;
      readonly #account: number;

      constructor(@Inject(capabilityToken('teller')) teller: Capability) {
        this.#ledger = Ledger.of(teller);
        this.#account = this.#ledger.openAccount('ops', 'NGN');
      }

      onModuleDestroy(): void {
        balanceAtDestroy = this.#ledger.balance(this.#account).equity;
      }
    }

    const ref = await Test.createTestingModule({ imports: [ZxvModule.forRoot(TELLER_AUDITOR)], providers: [Reader] }).compile();
    const app = ref.createNestApplication({ logger: false });
    await app.init();
    await app.close();
    assert.equal(balanceAtDestroy, 0n);
  });
});
