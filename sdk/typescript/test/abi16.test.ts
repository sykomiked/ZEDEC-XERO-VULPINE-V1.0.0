// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// ABI 1.6 (vino: Vino Embed) on both backends against vectors.json
// (status_abi16 and vino: requests, decode refusals, receipt, play
// scenario, meter scenario), plus handle lifetimes, the vouchers buffer
// path, error mapping, a capability refusal and a poisoned context.

import assert from 'node:assert/strict';
import { describe, test } from 'node:test';

import { errorFor } from '../src/errors.js';
import {
  Cap, CapabilityDeniedError, CoverageError, Feature2, LimitExceededError, MlDsa87, PaidChanceRefusedError, PaymentRequest, Receipt,
  RequestExpiredError, SpendMeter, Status, VerificationFailedError, VINO_AMOUNT_MAX, VINO_RECEIPT_MSG_BYTES, VINO_REQUEST_BYTES,
  VinoAccountKind, VinoLane, VinoPlay, VinoRail, VinoRequestFlag, VinoVoucherState, WrongLaneError, WrongRailError, type Zxv,
  ZxvError, ZxvFatalError, ZxvRefusedError, ZxvUsageError,
} from '../src/index.js';
import { BACKENDS, load, V } from './helpers.js';

// eslint-disable-next-line @typescript-eslint/no-explicit-any
type Any = any;
const X = V as unknown as Any;
const hex = (b: Uint8Array): string => Buffer.from(b).toString('hex');
const unhex = (h: string): Uint8Array => new Uint8Array(Buffer.from(h, 'hex'));
const B = (x: string | number): bigint => BigInt(x);

/** "ZXV_OK", or the status name of what `f` threw. */
function st(f: () => unknown): string {
  try {
    f();
    return 'ZXV_OK';
  } catch (e) {
    if (e instanceof ZxvError || e instanceof ZxvFatalError) return e.statusName;
    throw e;
  }
}

interface ReqVec {
  lane: number; flags: number; merchant: string; nonce: string; amount: string; created_at: string; expires_at: string; memo: string;
  status: string; bytes?: string; id?: string;
}

const request = (r: ReqVec): PaymentRequest => new PaymentRequest({
  lane: r.lane as VinoLane, flags: r.flags, merchant: unhex(r.merchant), nonce: unhex(r.nonce), amount: B(r.amount),
  createdAt: B(r.created_at), expiresAt: B(r.expires_at), memo: r.memo,
});

describe('ABI 1.6 status table and constants (pure)', () => {
  test('errorFor maps every new status to its class and type', () => {
    const want: Record<string, new (...a: Any[]) => Error> = {
      ZXV_E_RAIL: WrongRailError, ZXV_E_COVERAGE: CoverageError, ZXV_E_LANE: WrongLaneError, ZXV_E_EXPIRED: RequestExpiredError,
      ZXV_E_CHANCE: PaidChanceRefusedError,
    };
    assert.equal((X.status_abi16 as unknown[]).length, 5);
    for (const s of X.status_abi16 as { code: number; name: string; class: number }[]) {
      const e = errorFor(s.code, s.class, s.name, 'detail');
      const cls = want[s.name];
      assert.ok(cls, s.name);
      assert.ok(e instanceof cls, s.name);
      assert.ok(s.class === 1 ? e instanceof ZxvUsageError : e instanceof ZxvRefusedError, s.name);
      assert.equal((e as ZxvError).status, s.code);
      assert.equal((Status as Record<string, number>)[s.name.slice(4)], s.code);
    }
  });

  test('capability and feature constants', () => {
    assert.equal(Cap.VINO, 1n << 15n);
    assert.equal(Cap.ALL_1_6, (1n << 16n) - 1n);
    assert.equal(Cap.ALL_1_5, (1n << 15n) - 1n);
    assert.equal(Feature2.VINO, 0x100);
    assert.equal(VINO_AMOUNT_MAX, 1n << 59n);
  });
});

for (const kind of BACKENDS) {
  describe(`ABI 1.6 on the ${kind} backend`, () => {
    let zxv: Zxv;

    test('loads ABI 1.6 with VINO and the new status names', async () => {
      zxv = await load(kind);
      assert.equal(zxv.version.minor, 6);
      assert.ok(zxv.has('VINO'));
      assert.equal(zxv.featureMask2, 0x1ff);
      assert.deepEqual(zxv.features2().at(-1), 'VINO');
      for (const s of X.status_abi16 as { code: number; name: string; class: number }[]) {
        assert.equal(zxv.statusName(s.code), s.name);
        assert.equal(zxv.statusClass(s.code), s.class);
      }
    });

    test('the cap bit 1<<15 is now known; 1<<16 is still refused', () => {
      using ctx = zxv.createContext();
      const cap = ctx.rootCapability({ caps: Cap.ALL_1_6 });
      assert.equal(cap.info().caps, Cap.ALL_1_6);
      assert.equal(st(() => ctx.rootCapability({ caps: Cap.ALL_1_6 + 1n })), 'ZXV_E_ARG');
    });

    test('requests: encode every vector, exact bytes and ids, refusal names', () => {
      for (const r of X.vino.requests as ReqVec[]) {
        assert.equal(st(() => request(r).encode(zxv)), r.status, JSON.stringify(r));
        if (r.status !== 'ZXV_OK') continue;
        const e = request(r).encode(zxv);
        assert.equal(e.bytes.length, VINO_REQUEST_BYTES);
        assert.equal(hex(e.bytes), r.bytes);
        assert.equal(hex(e.id), r.id);
        const d = PaymentRequest.decode(zxv, e.bytes);
        assert.equal(hex(d.id), r.id);
        assert.equal(d.request.lane, r.lane);
        assert.equal(d.request.flags, r.flags);
        assert.equal(hex(d.request.merchant), r.merchant);
        assert.equal(hex(d.request.nonce), r.nonce);
        assert.equal(d.request.amount, B(r.amount));
        assert.equal(d.request.createdAt, B(r.created_at));
        assert.equal(d.request.expiresAt, B(r.expires_at));
        assert.equal(d.request.memo, r.memo);
        assert.equal(hex(d.request.encode(zxv).bytes), r.bytes);
      }
      // the typed errors behind the names
      const base = (X.vino.requests as ReqVec[])[0]!;
      assert.throws(() => request({ ...base, lane: 3 }).encode(zxv), WrongLaneError);
      assert.throws(() => request({ ...base, flags: VinoRequestFlag.CHANCE }).encode(zxv), PaidChanceRefusedError);
      // CHANCE is fine on the PLAY lane
      assert.equal(st(() => request({ ...base, lane: VinoLane.PLAY, flags: VinoRequestFlag.CHANCE }).encode(zxv)), 'ZXV_OK');
      // a non-ASCII memo reaches C as UTF-8 and is refused, not mangled
      assert.equal(st(() => request({ ...base, memo: 'café' }).encode(zxv)), 'ZXV_E_FORMAT');
      assert.equal(st(() => request({ ...base, memo: 'a\0b' })), 'ZXV_E_FORMAT');
      // amounts are exact bigints: a fractional number never reaches C
      assert.equal(st(() => new PaymentRequest({ lane: VinoLane.LIVE, merchant: unhex(base.merchant), nonce: unhex(base.nonce),
        amount: 1.5 as unknown as bigint, createdAt: 1n, expiresAt: 2n })), 'ZXV_E_FLOAT');
    });

    test('requests: every decode refusal by changed byte', () => {
      const good = unhex((X.vino.requests as ReqVec[])[0]!.bytes!);
      for (const d of X.vino.decode as { offset: number; value: number; status: string }[]) {
        const b = good.slice();
        b[d.offset] = d.value;
        assert.equal(st(() => PaymentRequest.decode(zxv, b)), d.status, JSON.stringify(d));
      }
      assert.equal(st(() => PaymentRequest.decode(zxv, good.subarray(0, 159))), 'ZXV_E_ARG');
    });

    test('receipt: message, fixed-key signature, every check, sign round trip', () => {
      const R = X.vino.receipt;
      using ctx = zxv.createContext();
      const cap = ctx.rootCapability({ caps: Cap.CRYPTO_SIGN });
      const kp = MlDsa87.fromSeed(cap, unhex(R.seed));
      using sk = kp.secretKey;
      assert.equal(hex(zxv.hashes.sha3_256(kp.publicKey)), R.pk_sha3_256);
      const req = unhex((X.vino.requests as ReqVec[])[0]!.bytes!);
      const id = PaymentRequest.decode(zxv, req).id;
      const msg = zxv.vino.receiptMessage(id, kp.publicKey, B(R.approved_at));
      assert.equal(msg.length, VINO_RECEIPT_MSG_BYTES);
      assert.equal(hex(msg), R.msg);
      const sig = MlDsa87.signDeterministic(cap, sk, msg);
      assert.equal(hex(sig), R.sig);
      for (const c of R.checks as { approved_at: string; now: string; flip_sig: number; status: string }[]) {
        const s = sig.slice();
        if (c.flip_sig >= 0) s[c.flip_sig] = (s[c.flip_sig] ?? 0) ^ 1;
        const rc = new Receipt({ request: req, walletPublicKey: kp.publicKey, approvedAt: B(c.approved_at), signature: s });
        assert.equal(st(() => rc.verify(zxv, B(c.now))), c.status, JSON.stringify(c));
        assert.equal(st(() => zxv.vino.verifyReceipt(req, kp.publicKey, B(c.approved_at), s, B(c.now))), c.status);
        assert.equal(rc.isValid(zxv, B(c.now)), c.status === 'ZXV_OK');
      }
      // wallet side: hedged signing verifies; outside the window is refused
      const rc = Receipt.sign(cap, req, sk, kp.publicKey, B(R.approved_at));
      rc.verify(zxv, B(R.approved_at));
      assert.throws(() => rc.verify(zxv, 1700000601n), RequestExpiredError);
      assert.throws(() => Receipt.sign(cap, req, sk, kp.publicKey, 1699999999n), RequestExpiredError);
      const bad = rc.signature;
      bad[0] = (bad[0] ?? 0) ^ 1;
      assert.throws(() => zxv.vino.verifyReceipt(req, kp.publicKey, B(R.approved_at), bad, B(R.approved_at)), VerificationFailedError);
      // the receipt holds copies: mutating what it returned changes nothing
      assert.equal(rc.isValid(zxv, B(R.approved_at)), true);
      // signing needs CRYPTO_SIGN
      const noSign = ctx.rootCapability({ caps: Cap.VINO });
      assert.throws(() => Receipt.sign(noSign, req, sk, kp.publicKey, B(R.approved_at)), CapabilityDeniedError);
    });

    test('play: the vector scenario step by step, then final accounts and vouchers', () => {
      const P = X.vino.play;
      using ctx = zxv.createContext();
      using play = VinoPlay.create(ctx.rootCapability({ caps: Cap.VINO }));
      for (const s of P.steps as Any[]) {
        let out: unknown;
        const f = (): void => {
          switch (s.op) {
            case 'open': out = play.openAccount(s.owner, s.is_issuer as VinoAccountKind); break;
            case 'back': play.back(s.issuer, B(s.amount)); break;
            case 'unback': play.unback(s.issuer, B(s.amount)); break;
            case 'issue': out = play.issue(s.issuer, s.holder, B(s.face), s.rail as VinoRail); break;
            case 'transfer': play.transfer(B(s.voucher), s.from, s.to, s.rail as VinoRail); break;
            case 'set_rail': play.setRail(B(s.voucher), s.holder, s.rail as VinoRail); break;
            case 'redeem': play.redeem(B(s.voucher), s.holder); break;
            default: throw new Error(`unknown op ${s.op}`);
          }
        };
        assert.equal(st(f), s.status, JSON.stringify(s));
        if (s.status === 'ZXV_OK' && s.op === 'open') assert.equal(out, s.account);
        if (s.status === 'ZXV_OK' && s.op === 'issue') assert.equal(out, B(s.id));
      }
      for (const a of P.accounts as Any[]) {
        const i = play.account(a.account);
        assert.deepEqual({ ...i }, {
          account: a.account, owner: a.owner, isIssuer: a.is_issuer === 1, backing: B(a.backing), faces: B(a.faces), count: a.count,
        });
        assert.deepEqual(play.vouchers(a.account), (a.vouchers as string[]).map(B));
      }
      for (const v of P.vouchers as Any[]) {
        assert.deepEqual({ ...play.voucher(B(v.id)) }, {
          id: B(v.id), face: B(v.face), issuer: v.issuer, holder: v.holder, rail: v.rail, state: v.state,
        });
      }
      play.check();
    });

    test('play: typed errors, the vouchers buffer path, a full shop', () => {
      using ctx = zxv.createContext();
      using play = VinoPlay.create(ctx.rootCapability({ caps: Cap.VINO }));
      const shop = play.openIssuer(1);
      const alice = play.openHolder(2);
      const bob = play.openHolder(3);
      assert.deepEqual(play.vouchers(alice), []); // count 0: the one-call path
      assert.throws(() => play.issue(shop, alice, 1n, VinoRail.BEARER), CoverageError);
      play.back(shop, 1000n);
      // 40 vouchers: the list grows past any first guess
      const ids: bigint[] = [];
      for (let i = 0; i < 40; i++) ids.push(play.issue(shop, alice, 5n, i % 2 === 0 ? VinoRail.BEARER : VinoRail.GHOST));
      assert.deepEqual(play.vouchers(alice), ids);
      assert.deepEqual(play.vouchers(shop), ids);
      assert.equal(play.account(shop).faces, 200n);
      const first = ids[0]!;
      assert.throws(() => play.transfer(first, alice, bob, VinoRail.GHOST), WrongRailError);
      assert.throws(() => play.setRail(first, alice, VinoRail.BEARER), WrongRailError);
      play.transfer(first, alice, bob, VinoRail.BEARER);
      play.setRail(first, bob, VinoRail.GHOST);
      play.redeem(first, bob);
      assert.equal(play.voucher(first).state, VinoVoucherState.REDEEMED);
      assert.deepEqual(play.vouchers(bob), []);
      assert.deepEqual(play.vouchers(alice), ids.slice(1));
      assert.equal(play.account(shop).faces, 195n);
      assert.equal(play.account(shop).backing, 1000n); // redeeming paid nothing out and added nothing
      assert.equal(st(() => play.voucher(999n)), 'ZXV_E_NOT_FOUND');
      assert.equal(st(() => play.issue(shop, alice, 4n, VinoRail.BEARER)), 'ZXV_E_DENOM');
      play.check();
    });

    test('meter: the vector scenario, admit and tryAdmit', () => {
      const M = X.vino.meter;
      using ctx = zxv.createContext();
      const cap = ctx.rootCapability({ caps: Cap.VINO });
      using a = SpendMeter.create(cap, B(M.daily_cap));
      using b = SpendMeter.create(cap, B(M.daily_cap));
      assert.equal(a.dailyCap, B(M.daily_cap));
      for (const s of M.steps as { day: string; amount: string; status: string; left?: string }[]) {
        let left: bigint | undefined;
        assert.equal(st(() => (left = a.admit(B(s.day), B(s.amount)))), s.status, JSON.stringify(s));
        if (s.status === 'ZXV_OK') assert.equal(left, B(s.left!));
        if (s.status === 'ZXV_E_RANGE') {
          assert.equal(st(() => b.tryAdmit(B(s.day), B(s.amount))), 'ZXV_E_RANGE');
        } else {
          const r = b.tryAdmit(B(s.day), B(s.amount));
          assert.equal(r.admitted, s.status === 'ZXV_OK');
          assert.equal(r.left, B(s.left!));
        }
      }
      using c = SpendMeter.create(cap, 10n);
      assert.throws(() => c.admit(1n, 11n), LimitExceededError);
      assert.equal(c.admit(1n, 10n), 0n);
      assert.equal(st(() => SpendMeter.create(cap, 0n)), 'ZXV_E_RANGE');
      assert.equal(st(() => SpendMeter.create(cap, VINO_AMOUNT_MAX + 1n)), 'ZXV_E_RANGE');
    });

    test('a capability without VINO is refused with the cap error', () => {
      using ctx = zxv.createContext();
      const all15 = ctx.rootCapability({ caps: Cap.ALL_1_5 });
      assert.throws(() => VinoPlay.create(all15), CapabilityDeniedError);
      assert.throws(() => SpendMeter.create(all15, 10n), CapabilityDeniedError);
      // a sandbox created with VINO refuses once that capability is revoked
      const vino = ctx.rootCapability({ caps: Cap.VINO });
      const play = VinoPlay.create(vino);
      play.openIssuer(1);
      vino.revoke();
      assert.throws(() => play.openHolder(2), CapabilityDeniedError);
    });

    test('handle lifetimes: use after close is refused; closing the context closes them', () => {
      const ctx = zxv.createContext();
      const cap = ctx.rootCapability({ caps: Cap.VINO });
      const play = VinoPlay.create(cap);
      const meter = SpendMeter.create(cap, 5n);
      play.close();
      play.close(); // idempotent
      assert.equal(play.closed, true);
      assert.throws(() => play.openIssuer(1), ZxvUsageError);
      assert.throws(() => play.vouchers(0), ZxvUsageError);
      ctx.close();
      assert.equal(meter.closed, true);
      assert.throws(() => meter.admit(1n, 1n), ZxvUsageError);
      assert.throws(() => meter.tryAdmit(1n, 1n), ZxvUsageError);
    });

    test('a poisoned context refuses every play and meter call; pure calls keep working', () => {
      const ctx = zxv.createContext();
      const cap = ctx.rootCapability({ caps: Cap.VINO });
      const play = VinoPlay.create(cap);
      const meter = SpendMeter.create(cap, 5n);
      const iss = play.openIssuer(1);
      const h = play.openHolder(2);
      play.back(iss, 100n);
      const id = play.issue(iss, h, 5n, VinoRail.BEARER);
      ctx.halt('test');
      for (const f of [
        () => VinoPlay.create(cap), () => play.openHolder(3), () => play.back(iss, 1n), () => play.unback(iss, 1n),
        () => play.issue(iss, h, 1n, VinoRail.BEARER), () => play.redeem(id, h), () => play.voucher(id), () => play.account(iss),
        () => play.vouchers(iss), () => play.check(), () => meter.admit(1n, 1n), () => meter.tryAdmit(1n, 1n),
      ]) assert.throws(f, ZxvFatalError);
      const r = (X.vino.requests as ReqVec[])[0]!;
      assert.equal(hex(request(r).encode(zxv).bytes), r.bytes);
      ctx.close();
    });
  });
}
