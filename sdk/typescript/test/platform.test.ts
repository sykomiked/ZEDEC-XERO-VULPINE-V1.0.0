// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// Contexts, capabilities, the ledger, fail-closed FATAL handling, budgets
// (sync, async waiting, listeners), phase deferral, attestations, secrets,
// ISO 20022, and the exactness rules at the JS boundary, on both backends.

import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { pathToFileURL } from 'node:url';
import { describe, test } from 'node:test';

import {
  AccountKind, Attestations, BudgetExhaustedError, Budgets, Cap, CapabilityDeniedError, Decision, Ed25519, EventState,
  FloatRejectedError, Health, InsufficientFundsError, Iso20022Message, Ledger, MxAmount, MxField, MxKind, NettingCycle,
  NotImplementedError, PHASE_CYCLE, PhaseClock, Rational, SecretKey, StepFlag, Subject, Truth, type Zxv, ZxvError,
  ZxvFatalError, ZxvRefusedError, ZxvUsageError, Swarm,
} from '../src/index.js';
import { BACKENDS, load, pkgRoot } from './helpers.js';

describe('Truth has no implicit boolean or number', () => {
  test('explicit conversions only', () => {
    assert.throws(() => +(Truth.TRUE as unknown as number), TypeError);
    assert.throws(() => (Truth.TRUE as unknown as number) == 1, TypeError);
    assert.equal(`${Truth.BOTH}`, 'BOTH');
    assert.equal(Truth.BOTH.toBoolean({ onBoth: false, onNeither: false }), false);
    assert.equal(Truth.NEITHER.toBoolean({ onBoth: false, onNeither: true }), true);
    assert.equal(Truth.TRUE.isCertainlyTrue(), true);
    assert.throws(() => Truth.BOTH.require('gate'), /BOTH, not TRUE/);
    assert.equal(Truth.fromEvidence(2, 1), Truth.BOTH);
    assert.throws(() => Truth.of(4), ZxvUsageError);
    assert.ok(Object.isFrozen(Truth.TRUE));
  });

  test('Rational has no implicit number either', () => {
    assert.throws(() => (Rational.of(1, 2) as unknown as number) * 2, TypeError);
    assert.equal(JSON.stringify({ r: Rational.parse('12.75') }), '{"r":"12.75"}');
    assert.equal(Rational.parse('0.1').add(Rational.parse('0.2')).toString(), '0.3');
  });
});

for (const kind of BACKENDS) {
  describe(`platform (${kind})`, () => {
    let zxv: Zxv;

    test('load', async () => {
      zxv = await load(kind);
    });

    test('capabilities: attenuate, deny, revoke, BOTH attestation', () => {
      using ctx = zxv.createContext();
      const root = ctx.rootCapability({ processId: 7 });
      assert.throws(() => ctx.rootCapability({ caps: 1n << 40n }), ZxvUsageError);
      const ro = root.attenuate(Cap.LEDGER_READ);
      assert.equal(ro.check(Cap.LEDGER_READ), true);
      assert.equal(ro.check(Cap.LEDGER_POST), false);
      assert.throws(() => ro.require(Cap.LEDGER_POST), CapabilityDeniedError);
      const wider = ro.attenuate(Cap.ALL);
      assert.equal(wider.check(Cap.LEDGER_POST), false, 'attenuation never widens');
      assert.equal(ro.info().caps, Cap.LEDGER_READ);
      assert.equal(root.info().processId, 7);

      root.setAttestation(Truth.BOTH);
      try {
        root.require(Cap.LEDGER_READ);
        assert.fail('BOTH must block');
      } catch (e) {
        assert.ok(e instanceof CapabilityDeniedError && e instanceof ZxvRefusedError);
        assert.equal(e.statusName, 'ZXV_E_CAP');
        assert.match(e.detail, /contradictory/);
      }
      assert.equal(root.info().attestation, Truth.BOTH);
      root.setAttestation(Truth.TRUE);

      ctx.enableCurrency(root, 'NGN', 2);
      const ledger = Ledger.of(root);
      const issuer = ledger.openAccount('issuer', 'NGN', AccountKind.ISSUER);
      const alice = ledger.openAccount('alice', 'NGN');
      ledger.post(issuer, alice, 500n, 'NGN', 'r1');
      assert.throws(() => Ledger.of(ro).post(issuer, alice, 1n, 'NGN', 'r2'), CapabilityDeniedError);
      assert.equal(Ledger.of(ro).balance(alice).equity, 500n);

      root.revoke();
      assert.equal(ro.check(Cap.LEDGER_READ), false, 'revoking the parent revokes the child');
      assert.equal(ro.info().revoked, true);
    });

    test('ledger: exact amounts, funds, journal, floats refused', () => {
      using ctx = zxv.createContext();
      const cap = ctx.rootCapability();
      ctx.enableCurrency(cap, 'USD');
      ctx.registerPrivateUnit(cap, 'VFV', 2);
      assert.deepEqual(ctx.enabledCurrencies(), ['USD', 'VFV']);
      assert.equal(ctx.currency('USD').minorUnits, 2);
      const l = Ledger.of(cap);
      const bank = l.openAccount('bank', 'USD', AccountKind.ISSUER);
      const bob = l.openAccount('bob', 'USD');
      const carol = l.openAccount('carol', 'USD');
      const e1 = l.post(bank, bob, 9007199254740991n, 'USD', 'max');
      assert.equal(e1, 1n);
      l.post(bob, carol, 1n, 'USD', 'one');
      assert.deepEqual(l.balance(bob), { debit: 9007199254740991n, credit: 1n, equity: 9007199254740990n });
      assert.throws(() => l.post(carol, bob, 2n, 'USD', 'over'), InsufficientFundsError);
      assert.throws(() => l.post(bank, bob, 1.5, 'USD', 'float'), FloatRejectedError);
      assert.throws(() => l.post(bank, bob, 2 ** 60, 'USD', 'unsafe'), ZxvUsageError);
      assert.throws(() => l.post(bank, bob, 1n, 'USD', 'one'), ZxvUsageError, 'duplicate reference');
      assert.deepEqual(l.journal(carol), [{ entryId: 2n, amount: 1n, counterparty: bob, reference: 'one' }]);
      assert.deepEqual(l.account(bob), { id: bob, name: 'bob', currency: 'USD', kind: AccountKind.HOLDER });
      assert.equal(l.accountCount(), 3);
      l.check();
    });

    test('FATAL: a halted context is poisoned and refuses everything', () => {
      const ctx = zxv.createContext();
      const cap = ctx.rootCapability();
      ctx.enableCurrency(cap, 'NGN');
      const l = Ledger.of(cap);
      const a = l.openAccount('a', 'NGN', AccountKind.ISSUER);
      const b = l.openAccount('b', 'NGN');
      ctx.halt('test halt');
      assert.equal(ctx.isPoisoned, true);
      let err: unknown;
      try {
        l.post(a, b, 1n, 'NGN', 'after');
      } catch (e) {
        err = e;
      }
      assert.ok(err instanceof ZxvFatalError, 'ZxvFatalError');
      assert.ok(!(err instanceof ZxvError), 'outside the ordinary hierarchy');
      assert.equal(err.fatal, true);
      assert.equal(err.statusName, 'ZXV_E_POISONED');
      assert.throws(() => l.balance(b), ZxvFatalError, 'every later call');
      assert.throws(() => ctx.rootCapability(), ZxvFatalError);
      ctx.close(); // a poisoned context can still be closed
      assert.throws(() => ctx.rootCapability(), ZxvUsageError);
    });

    test("FATAL with fatalPolicy 'abort' terminates the process", () => {
      const entry = pathToFileURL(join(pkgRoot, 'dist', 'src', 'index.js')).href;
      const code = `import { Zxv } from ${JSON.stringify(entry)};
        const zxv = await Zxv.load({ backend: ${JSON.stringify(kind)} });
        const ctx = zxv.createContext({ fatalPolicy: 'abort' });
        ctx.halt('kill switch');
        try { ctx.rootCapability(); } catch { console.log('CAUGHT'); }
        console.log('SURVIVED');`;
      const r = spawnSync(process.execPath, ['--input-type=module', '-e', code], { cwd: tmpdir(), encoding: 'utf8' });
      assert.equal(r.signal, 'SIGABRT', `stdout: ${r.stdout} stderr: ${r.stderr}`);
      assert.doesNotMatch(r.stdout, /CAUGHT|SURVIVED/);
      assert.match(r.stderr, /fatalPolicy 'abort'/);
    });

    test('budgets: exact consume, exhaustion, INVALID, listeners', () => {
      using ctx = zxv.createContext();
      using budgets = Budgets.open(ctx.rootCapability());
      const b = budgets.create('tokens', 10);
      const denied: string[] = [];
      const off = b.onExhausted((e) => denied.push(e.amount.toString()));
      b.consume(Rational.of(7, 2));
      b.consume('6.5');
      assert.throws(() => b.consume(Rational.of(1, 3)), BudgetExhaustedError);
      assert.throws(() => b.consume(-1), (e: unknown) => e instanceof ZxvUsageError && e.statusName === 'ZXV_E_RMAG_INVALID');
      assert.throws(() => b.consume(0.5), FloatRejectedError);
      assert.deepEqual(denied, ['1/3']);
      off();
      const st = b.status();
      assert.ok(st.remaining.equals(0) && st.used.equals(10));
      assert.equal(st.epoch, 0n);
      b.release('1/2');
      assert.ok(b.status().remaining.equals(Rational.of(1, 2)));
      assert.equal(b.status().epoch, 1n);
      assert.throws(() => Budgets.open(ctx.rootCapability({ caps: Cap.LEDGER_READ })).create('x', 1), CapabilityDeniedError);
    });

    test('budgets: consumeAsync waits for a release; timeout; abort', async () => {
      using ctx = zxv.createContext();
      using budgets = Budgets.open(ctx.rootCapability());
      const b = budgets.create('gpu', 5);
      b.consume(5);
      let done = false;
      const waiting = b.consumeAsync(2).then(() => { done = true; });
      await new Promise((ok) => setTimeout(ok, 5));
      assert.equal(done, false, 'still waiting while exhausted');
      b.release(1);
      await new Promise((ok) => setTimeout(ok, 5));
      assert.equal(done, false, 'one unit is not enough for two');
      b.setLimit(6);
      await waiting;
      assert.ok(b.status().used.equals(6));
      await assert.rejects(b.consumeAsync(1, { timeoutMs: 10 }), BudgetExhaustedError);
      const ac = new AbortController();
      const p = b.consumeAsync(1, { signal: ac.signal });
      ac.abort(new Error('stop waiting'));
      await assert.rejects(p, /stop waiting/);
      await assert.rejects(b.consumeAsync(-1), ZxvUsageError, 'INVALID fails at once');
    });

    test('phase: defer, advance, awaitCleared, admit', async () => {
      using ctx = zxv.createContext();
      using clock = PhaseClock.open(ctx.rootCapability());
      const ev = clock.defer(PHASE_CYCLE);
      assert.equal(clock.nextDue(), PHASE_CYCLE);
      let cleared = false;
      const w = ev.awaitCleared().then(() => { cleared = true; });
      clock.advance(PHASE_CYCLE - 1n);
      await new Promise((ok) => setTimeout(ok, 1));
      assert.equal(ev.state(), EventState.PENDING);
      assert.equal(cleared, false, 'still S0 one tick early');
      assert.deepEqual(clock.advance(1), { now: 27720n, cleared: 1 });
      await w;
      assert.equal(ev.state(), EventState.CLEARED);
      assert.equal(clock.nextDue(), undefined);
      await clock.untilTick(5n); // already passed
      const later = clock.untilTick(PHASE_CYCLE * 2n);
      clock.advance(PHASE_CYCLE);
      await later;

      const c2 = clock.defer(clock.now() + 10n);
      const p = c2.awaitCleared();
      c2.cancel();
      await assert.rejects(p, /cancelled/);

      assert.equal(clock.admit(6, 1, StepFlag.S0_PENDING).decision, Decision.DEFER);
      clock.setHealth(6, Health.HEALTHY);
      const a = clock.admit(6, 2, StepFlag.REQUIRES_HEALTH);
      assert.equal(a.decision, Decision.ADMIT);
      assert.ok(a.token > 0n);
      assert.throws(() => PhaseClock.open(ctx.rootCapability({ caps: Cap.LEDGER_READ })).advance(1), CapabilityDeniedError);
    });

    test('phase: the realtime driver clears waiters from a timer', async () => {
      using ctx = zxv.createContext();
      using clock = PhaseClock.open(ctx.rootCapability());
      const ev = clock.defer(50n);
      const stop = clock.startRealtime(10_000n, 1); // 10,000 ticks per second
      await ev.awaitCleared();
      stop();
      assert.ok(clock.now() >= 50n);
    });

    test('attestations hold contradictions; the gate needs certainty', () => {
      using ctx = zxv.createContext();
      using reg = Attestations.create(ctx.rootCapability());
      const i = reg.attest('model.gguf', { attester: 'loader', subject: Subject.MODEL_PACK });
      assert.equal(reg.gate(), Truth.TRUE);
      reg.evidence(i, true);
      assert.equal(reg.evidence(i, false), Truth.BOTH);
      assert.deepEqual(reg.state(i), { truth: Truth.BOTH, positive: 2, negative: 1 });
      assert.equal(reg.gate(), Truth.FALSE, 'BOTH blocks the actuator gate');
      assert.equal(reg.find('model.gguf'), i);
      assert.equal(reg.find('nope'), undefined);
    });

    test('secrets live off-heap and are wiped on close', () => {
      using ctx = zxv.createContext();
      const cap = ctx.rootCapability();
      const kp = Ed25519.generate(cap);
      const sk: SecretKey = kp.secretKey;
      assert.equal(sk.length, 32);
      assert.equal(sk.exportBytesUnsafe().length, 32);
      sk.close();
      assert.equal(sk.closed, true);
      assert.throws(() => sk.exportBytesUnsafe(), ZxvUsageError);
      assert.throws(() => Ed25519.sign(cap, sk, 'x'), ZxvUsageError);
      assert.throws(() => Ed25519.sign(ctx.rootCapability({ caps: Cap.CRYPTO_KEM }), Ed25519.generate(cap).secretKey, 'x'),
        CapabilityDeniedError);
      {
        using s2 = zxv.secretKey(new Uint8Array(32).fill(7));
        assert.equal(s2.closed, false);
      }
    });

    test('closing a context closes its dependents first', () => {
      const ctx = zxv.createContext();
      const root = ctx.rootCapability();
      const child = root.attenuate(Cap.LEDGER_READ);
      const budgets = Budgets.open(root);
      ctx.close();
      assert.ok(child.closed && root.closed && budgets.closed && ctx.closed);
      ctx.close(); // idempotent
    });

    test('ISO 20022, ISO 4217 and the pending kernel modules', () => {
      using ctx = zxv.createContext();
      const root = ctx.rootCapability();
      using m = Iso20022Message.create(root, MxKind.PACS008);
      m.setText(MxField.MSG_ID, 'MSG-0001').setText(MxField.CRE_DT_TM, '2026-10-09T12:00:00Z')
        .setText(MxField.DEBTOR_NAME, 'Ada & Sons <Lagos>').setText(MxField.DEBTOR_ACCT, 'NG0001')
        .setText(MxField.CREDITOR_NAME, 'Kwame Mensah').setText(MxField.CREDITOR_ACCT, 'GH0002')
        .setText(MxField.END_TO_END_ID, 'E2E-1');
      assert.throws(() => m.setText(MxField.CCY, 'VFV'), ZxvUsageError, 'VFV refused on the wire');
      m.setText(MxField.CCY, 'NGN').setAmount(MxAmount.SETTLEMENT, 250050n, 2);
      const xml = m.render();
      assert.match(xml, /Ada &amp; Sons &lt;Lagos&gt;/);
      assert.match(xml, /2500\.50/);
      assert.equal(zxv.iso4217.byAlpha('NGN')?.numeric, 566);
      assert.equal(zxv.iso4217.byAlpha('VFV'), undefined);
      assert.equal(zxv.iso4217.byNumeric(566)?.alpha, 'NGN');
      assert.equal(zxv.iso4217.byNumeric(777), undefined);
      assert.equal(zxv.iso4217.name('NGN'), 'Naira');
      assert.ok(zxv.iso4217.list().includes('GHS'));
      assert.equal(zxv.iso4217.isAuMember('NG'), true);
      assert.equal(zxv.railNumeric(1), 777);
      assert.equal(zxv.cardCheckDigit(2, '79927398713'), true);
      assert.throws(() => Iso20022Message.create(root, MxKind.PACS002), NotImplementedError);
      assert.throws(() => NettingCycle.open(ctx, 'C1', 'NGN'), NotImplementedError);
    });
    test('ABI 1.2: the _cap forms need the right bit', () => {
      using ctx = zxv.createContext();
      const root = ctx.rootCapability();
      const post = root.attenuate(Cap.LEDGER_POST);
      const read = root.attenuate(Cap.LEDGER_READ);
      const denied = (f: () => unknown) =>
        assert.throws(f, (e: unknown) => e instanceof CapabilityDeniedError && e.statusName === 'ZXV_E_CAP');
      ctx.enableCurrency(post, 'NGN');
      ctx.enableCurrency(post, 'GHS', 2);
      ctx.registerPrivateUnit(post, 'VFV', 2);
      assert.deepEqual(ctx.enabledCurrencies(), ['NGN', 'GHS', 'VFV']);
      denied(() => ctx.enableCurrency(read, 'KES'));
      denied(() => ctx.enableCurrency(read, 'KES', 2));
      denied(() => ctx.registerPrivateUnit(read, 'VFX', 2));
      denied(() => Ledger.of(read).openAccount('x', 'NGN'));
      const w = Ledger.of(post);
      const bank = w.openAccount('bank', 'NGN', AccountKind.ISSUER);
      const ada = w.openAccount('ada', 'NGN');
      w.post(bank, ada, 7n, 'NGN', 'R-1');
      const r = Ledger.of(read);
      assert.equal(r.accountCount(), 2);
      assert.deepEqual(r.account(ada), { id: ada, name: 'ada', currency: 'NGN', kind: AccountKind.HOLDER });
      assert.equal(r.journal(ada)[0]?.reference, 'R-1');
      denied(() => w.accountCount());
      denied(() => w.account(ada));
      denied(() => w.journal(ada));
      denied(() => Iso20022Message.create(post, MxKind.PACS008));
      const m = Iso20022Message.create(read, MxKind.PACS008);
      m.setText(MxField.MSG_ID, 'M-1');
      read.setAttestation(Truth.NEITHER);
      denied(() => m.setText(MxField.MSG_ID, 'M-2'));
      read.close();
      assert.equal(m.closed, true, 'closing the cap closes its messages first');
      denied(() => Budgets.open(post));
      denied(() => PhaseClock.open(post));
      denied(() => Attestations.create(post));
      denied(() => Swarm.create(post, { levels: 2, tokensPerCycle: 10 }));
      Budgets.open(root.attenuate(Cap.BUDGET)).close();
      PhaseClock.open(root.attenuate(Cap.PHASE)).close();
      Attestations.create(root.attenuate(Cap.ATTEST)).close();
      Swarm.create(root.attenuate(Cap.SWARM), { levels: 2, tokensPerCycle: 10 }).close();
    });

    test('ABI 1.2: a capability of another context is refused', () => {
      using ctx = zxv.createContext();
      using other = zxv.createContext();
      const foreign = other.rootCapability();
      for (const f of [() => ctx.enableCurrency(foreign, 'NGN'), () => ctx.enableCurrency(foreign, 'NGN', 2),
        () => ctx.registerPrivateUnit(foreign, 'VFV', 2)]) {
        assert.throws(f, (e: unknown) => e instanceof CapabilityDeniedError && e.statusName === 'ZXV_E_CAP' &&
          /different context/.test(e.detail));
      }
      assert.deepEqual(ctx.enabledCurrencies(), []);
      assert.deepEqual(other.enabledCurrencies(), []);
    });

    test('ABI 1.2: the deprecated no-capability forms still work', () => {
      using ctx = zxv.createContext();
      ctx.enableCurrency('NGN');
      ctx.enableCurrency('GHS', 2);
      ctx.registerPrivateUnit('VFV', 2);
      Iso20022Message.create(ctx, MxKind.PACS008).close();
      assert.deepEqual(ctx.enabledCurrencies(), ['NGN', 'GHS', 'VFV']);
    });
  });
}
