// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// ABI 1.3: the 64-state hexagram tag (pure TypeScript) against vectors.json
// "hex" and both backends' zxv_hex_* calls; the quarantine log on both.

import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { describe, test } from 'node:test';

import {
  Cap, CapabilityDeniedError, HEX_ACTION_NAMES, HexBase, Hexagram, HexPhase, HexPolarity, QUARANTINE_MAX_RECORDS,
  QuarantineLog, Truth, type Zxv, ZxvError, ZxvFatalError, ZxvRefusedError, ZxvUsageError,
} from '../src/index.js';
import { BACKENDS, load, V } from './helpers.js';

const H = V.hex;
const hex = (b: Uint8Array) => Buffer.from(b).toString('hex');
const unhex = (h: string) => new Uint8Array(Buffer.from(h, 'hex'));
const lower = (o: object) => Object.keys(o).map((k) => k.toLowerCase());

function statusOf(f: () => unknown): number {
  try {
    f();
    return 0;
  } catch (e) {
    if (e instanceof ZxvError) return e.status;
    throw e;
  }
}

describe('Hexagram (pure TypeScript)', () => {
  test('tags, names and actions match the vectors', () => {
    assert.deepEqual(lower(HexBase), H.base_names);
    assert.deepEqual(lower(HexPolarity), H.polarity_names);
    assert.deepEqual(lower(HexPhase), H.phase_names);
    assert.deepEqual([...HEX_ACTION_NAMES], H.action_names);
    for (const v of H.tags) {
      const h = Hexagram.fromTag(v.tag);
      assert.equal(h.name, v.name);
      assert.equal(String(h), v.name);
      assert.deepEqual([h.base, h.polarity, h.phase, h.action], [v.base, v.polarity, v.phase, v.action]);
      assert.equal(Hexagram.of(h.base, h.polarity, h.phase), h, 'singletons');
    }
    for (const p of Object.values(HexPhase)) assert.equal(Hexagram.nextPhase(p), H.phase_next[p]);
  });

  test('and / or / not tables', () => {
    for (const a of Hexagram.all) {
      assert.equal(a.not().tag, H.not[a.tag]);
      for (const b of Hexagram.all) {
        assert.equal(a.and(b).tag, H.and[a.tag]![b.tag]);
        assert.equal(a.or(b).tag, H.or[a.tag]![b.tag]);
      }
    }
  });

  test('step and withState', () => {
    for (const h of Hexagram.all) {
      for (const p of Object.values(HexPhase)) {
        const want = H.step[h.tag]![p]!;
        if (want < 0) {
          let e: unknown;
          try {
            h.step(p);
          } catch (x) {
            e = x;
          }
          assert.ok(e instanceof ZxvRefusedError && e.statusName === 'ZXV_E_STATE');
        } else {
          assert.equal(h.step(p).tag, want);
        }
      }
      for (const b of Object.values(HexBase)) {
        for (const pol of Object.values(HexPolarity)) {
          const want = H.set[h.tag]![b * 4 + pol]!;
          if (want < 0) assert.throws(() => h.withState(b, pol), ZxvRefusedError);
          else assert.equal(h.withState(b, pol).tag, want);
        }
      }
      assert.equal(h.forbid(), Hexagram.of(h.base, HexPolarity.FORBIDDEN, h.phase));
      assert.equal(h.advance().phase, Hexagram.nextPhase(h.phase));
    }
  });

  test('polarity from evidence and the explicit Truth bridge', () => {
    for (const c of H.polarity) {
      assert.equal(Hexagram.polarityFromEvidence(c.pos0, c.neg0, c.pos1, c.neg1, c.stale), c.polarity);
    }
    assert.throws(() => Hexagram.polarityFromEvidence(-1, 0, 0, 0, false), ZxvUsageError);
    for (const t of Truth.values) {
      const h = Hexagram.fromTruth(t, HexPolarity.NEUTRAL, HexPhase.POTENTIAL);
      assert.equal(h.base, H.lp_to_base[t.value]);
      assert.equal(h.toTruth(), t);
    }
    for (const b of Object.values(HexBase)) {
      assert.equal(Hexagram.of(b, HexPolarity.NEUTRAL, HexPhase.POTENTIAL).toTruth().value, H.base_to_lp[b]);
    }
    assert.equal(Truth.TRUE.value, 1);
    assert.equal(HexBase.TRUE, 3, 'the numbering differs on purpose');
  });

  test('refusals and value semantics', () => {
    for (const bad of [64, 100, 255]) assert.equal(statusOf(() => Hexagram.fromTag(bad)), -1);
    assert.throws(() => Hexagram.fromTag(-1), ZxvUsageError);
    assert.throws(() => Hexagram.fromTag(1.5), ZxvUsageError);
    assert.throws(() => Hexagram.of(4 as HexBase, 0, 0), ZxvUsageError);
    const h = Hexagram.of(HexBase.TRUE, HexPolarity.NEGENTROPIC, HexPhase.MANIFEST);
    assert.equal(h.tag, 63);
    assert.equal(JSON.stringify(h), '63');
    assert.throws(() => +(h as unknown as number), TypeError);
    let f = Hexagram.of(HexBase.TRUE, HexPolarity.FORBIDDEN, HexPhase.POTENTIAL);
    for (let i = 0; i < 4; i++) {
      f = f.advance();
      assert.ok(f.isForbidden);
    }
    assert.ok(f.and(h).isForbidden && f.or(h).isForbidden && f.not().isForbidden);
    assert.throws(() => f.withState(HexBase.TRUE, HexPolarity.NEUTRAL), ZxvRefusedError);
  });
});

function link(seq: number, tag: number, reason: number, who: number, subject: Uint8Array, prev: Uint8Array): Uint8Array {
  const b = Buffer.alloc(15 + 8 + 1 + 1 + 4 + 32 + 32);
  b.write('zxv/lpx-qlog/v1', 0, 'ascii');
  b.writeBigUInt64LE(BigInt(seq), 15);
  b[23] = tag;
  b[24] = reason;
  b.writeUInt32LE(who, 25);
  b.set(subject, 29);
  b.set(prev, 61);
  return new Uint8Array(createHash('sha256').update(b).digest());
}

for (const kind of BACKENDS) {
  describe(`hexagram on the ${kind} backend`, () => {
    let zxv: Zxv;

    test('loads ABI 1.3 with HEX', async () => {
      zxv = await load(kind);
      assert.ok(zxv.version.minor >= 3 && zxv.has('HEX'));
    });

    test('every TypeScript result equals zxv_hex_*', () => {
      const b = zxv._core.b;
      const o = new Uint8Array(1);
      const a32 = new Int32Array(1);
      for (const x of Hexagram.all) {
        assert.equal(b.call('zxv_hex_action', x.tag, a32), 0);
        assert.equal(a32[0], x.action);
        assert.equal(b.call('zxv_hex_not', x.tag, o), 0);
        assert.equal(o[0], x.not().tag);
        for (const y of Hexagram.all) {
          b.call('zxv_hex_and', x.tag, y.tag, o);
          assert.equal(o[0], x.and(y).tag);
          b.call('zxv_hex_or', x.tag, y.tag, o);
          assert.equal(o[0], x.or(y).tag);
        }
        for (const p of Object.values(HexPhase)) {
          const s = b.call('zxv_hex_step', x.tag, p, o);
          assert.equal(s === 0 ? o[0] : -1, statusOf(() => x.step(p)) === 0 ? x.step(p).tag : -1);
        }
      }
      assert.equal(b.call('zxv_hex_unpack', 64, null, null, null), -1);
      for (const t of Truth.values) {
        b.call('zxv_hex_from_lp', t.value, o);
        assert.equal(o[0], Hexagram.fromTruth(t, 0, 0).base);
      }
    });

    test('quarantine chain matches the vectors and SHA-256', () => {
      using ctx = zxv.createContext();
      const cap = ctx.rootCapability();
      using log = QuarantineLog.create(cap, 3);
      assert.equal(log.count, 0);
      assert.equal(hex(log.head()), '00'.repeat(32));
      let prev = new Uint8Array(32);
      for (const r of H.qlog.records) {
        const subject = unhex(r.subject);
        const seq = log.append(Hexagram.fromTag(r.tag), r.reason, r.submitter, subject);
        assert.equal(seq, BigInt(r.seq));
        const want = link(r.seq, r.tag, r.reason, r.submitter, subject, prev);
        assert.equal(hex(want), r.hash);
        const rec = log.record(r.seq);
        assert.deepEqual([hex(rec.hash), hex(rec.prev), hex(rec.subject)], [r.hash, hex(prev), r.subject]);
        assert.deepEqual([rec.tag, rec.reason, rec.submitter], [Hexagram.fromTag(r.tag), r.reason, r.submitter]);
        prev = want;
      }
      assert.equal(hex(log.head()), H.qlog.head);
      assert.equal(log.count, 3);
      log.verify();
      let e: unknown;
      try {
        log.append(Hexagram.fromTag(0x3b), 0, 0, new Uint8Array(32));
      } catch (x) {
        e = x;
      }
      assert.ok(e instanceof ZxvRefusedError && e.statusName === 'ZXV_E_CAPACITY');
    });

    test('quarantine refusals and fail-closed', () => {
      const ctx = zxv.createContext();
      const cap = ctx.rootCapability();
      const forb = Hexagram.of(HexBase.BOTH, HexPolarity.FORBIDDEN, HexPhase.EMERGING);
      const all = (1n << 10n) - 1n;
      assert.throws(() => QuarantineLog.create(cap.attenuate(all & ~Cap.ATTEST), 4), CapabilityDeniedError);
      assert.throws(() => QuarantineLog.create(cap, 0), ZxvUsageError);
      assert.throws(() => QuarantineLog.create(cap, QUARANTINE_MAX_RECORDS + 1), ZxvUsageError);
      const log = QuarantineLog.create(cap, 4);
      assert.throws(() => log.append(Hexagram.of(HexBase.TRUE, HexPolarity.NEUTRAL, 0), 1, 1, new Uint8Array(32)), ZxvUsageError);
      assert.throws(() => log.append(forb, 1, 1, new Uint8Array(31)), ZxvUsageError);
      assert.throws(() => log.append(forb, 256, 1, new Uint8Array(32)), ZxvUsageError);
      assert.equal(log.append(forb, 9, 0xffffffff, new Uint8Array(32)), 0n);
      assert.equal(log.record(0).submitter, 0xffffffff);
      ctx.halt('test');
      assert.throws(() => log.count, ZxvFatalError);
      ctx.close();
    });
  });
}
