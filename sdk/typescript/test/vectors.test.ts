// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// sdk/conformance/vectors.json, checked on both backends and, for the
// pure-TypeScript parts (Rational, Truth, Ubh168Frame), without any.

import assert from 'node:assert/strict';
import { describe, test } from 'node:test';

import {
  Cap, Ed25519, FloatRejectedError, MlDsa87, MlKem1024, Rational, StateChannel, Swarm, Truth, Ubh168Frame, type Zxv,
  ZxvError, ZxvUsageError,
} from '../src/index.js';
import { BACKENDS, big, load, V } from './helpers.js';

const hex = (b: Uint8Array) => Buffer.from(b).toString('hex');
const unhex = (h: string) => new Uint8Array(Buffer.from(h, 'hex'));
const R = (r: [number | string, number | string]) => Rational.of(...big(r));

/** The status a thrown SDK error carries (0 if none was thrown). */
function statusOf(f: () => unknown): number {
  try {
    f();
    return 0;
  } catch (e) {
    if (e instanceof ZxvError) return e.status;
    throw e;
  }
}

describe('pure TypeScript (no backend)', () => {
  test('rational_ops', () => {
    for (const v of V.rational_ops) {
      const a = R(v.a);
      const b = R(v.b);
      assert.ok(a.add(b).equals(R(v.add)), `add ${v.a} ${v.b}`);
      assert.ok(a.sub(b).equals(R(v.sub)));
      assert.ok(a.mul(b).equals(R(v.mul)));
      assert.ok(a.div(b).equals(R(v.div)));
      assert.equal(a.cmp(b), v.cmp);
      assert.deepEqual([a.add(b).num, a.add(b).den], big(v.add), 'reduced form');
    }
  });

  test('rational_parse and format', () => {
    for (const v of V.rational_parse) {
      assert.equal(statusOf(() => Rational.parse(v.text)), v.status, v.text);
      if (v.status === 0) {
        const r = Rational.parse(v.text);
        assert.deepEqual([r.num, r.den], big(v.value!), v.text);
        assert.equal(r.toString(), v.format);
      }
    }
  });

  test('ieee754: the float boundary', () => {
    for (const v of V.ieee754) {
      const bits = BigInt('0x' + v.bits);
      assert.equal(statusOf(() => Rational.fromIeee754Bits(bits)), v.status, v.bits);
      if (v.status === 0) assert.deepEqual([Rational.fromIeee754Bits(bits).num, Rational.fromIeee754Bits(bits).den], big(v.value!));
    }
    assert.equal(Rational.fromNumberExact(0.1).toFraction(), '3602879701896397/36028797018963968');
    assert.throws(() => Rational.fromNumberExact(Number.NaN), FloatRejectedError);
    assert.throws(() => Rational.fromNumberExact(Infinity), FloatRejectedError);
    assert.throws(() => Rational.of(1.5), FloatRejectedError);
  });

  test('q16', () => {
    for (const v of V.q16) {
      assert.equal(statusOf(() => R(v.value).toQ16()), v.status);
      if (v.status === 0) assert.equal(R(v.value).toQ16(), v.q16);
    }
  });

  test('lpres tables', () => {
    for (const t of Truth.values) {
      assert.equal(t.name, V.lpres.names[t.value]);
      assert.equal(t.not().value, V.lpres.not[t.value]);
      for (const u of Truth.values) {
        assert.equal(t.and(u).value, V.lpres.and[t.value]![u.value]);
        assert.equal(t.or(u).value, V.lpres.or[t.value]![u.value]);
      }
    }
  });

  test('ubh168 frames byte for byte', () => {
    for (const v of V.ubh168) {
      assert.equal(Ubh168Frame.tag(v.trunk, v.shell, v.sync), v.tag);
      const f = Ubh168Frame.pack(v.tag, v.s_plus, v.s_minus);
      assert.equal(f.toHex(), v.frame);
      const u = Ubh168Frame.unpack(unhex(v.frame));
      assert.deepEqual([u.tag, [...u.sPlus], [...u.sMinus]], [v.tag, v.s_plus, v.s_minus]);
    }
    V.ubh168_idle.forEach((h, trunk) => assert.equal(Ubh168Frame.idle(trunk).toHex(), h));
  });
});

for (const kind of BACKENDS) {
  describe(`${kind} backend`, () => {
    let zxv: Zxv;

    test('loads with ABI 1.2+', async () => {
      zxv = await load(kind);
      assert.equal(zxv.backend, kind);
      assert.deepEqual([zxv.version.major, zxv.version.minor, zxv.version.patch], V.abi);
      assert.ok(zxv.has('RATIONAL') && zxv.has('TENSOR') && zxv.has('MLDSA87'));
      assert.ok(!zxv.has('LORA'), 'LoRA is honestly reported as not built');
    });

    test('status names and classes', () => {
      for (const s of V.status) {
        assert.equal(zxv.statusName(s.code), s.name);
        assert.equal(zxv.statusClass(s.code), s.class);
      }
    });

    test('kernel rationals match the vectors and the TypeScript Rational', () => {
      const k = zxv.kernel;
      for (const v of V.rational_ops) {
        const a = R(v.a);
        const b = R(v.b);
        assert.ok(k.add(a, b).equals(R(v.add)));
        assert.ok(k.sub(a, b).equals(R(v.sub)));
        assert.ok(k.mul(a, b).equals(R(v.mul)));
        assert.ok(k.div(a, b).equals(R(v.div)));
        assert.equal(k.cmp(a, b), v.cmp);
      }
      for (const v of V.rational_parse) {
        assert.equal(statusOf(() => k.parse(v.text)), v.status, v.text);
        if (v.status === 0) assert.equal(k.format(k.parse(v.text)), v.format);
      }
      for (const v of V.ieee754) {
        const bits = BigInt('0x' + v.bits);
        assert.equal(statusOf(() => k.fromIeee754Bits(bits)), v.status);
        if (v.status === 0) assert.ok(k.fromIeee754Bits(bits).equals(R(v.value!)));
      }
      for (const v of V.q16) {
        assert.equal(statusOf(() => k.toQ16(R(v.value))), v.status);
        if (v.status === 0) assert.equal(k.toQ16(R(v.value)), v.q16);
      }
      assert.equal(statusOf(() => k.mul(Rational.of((1n << 62n) + 1n), Rational.of(4))), -8, 'overflow refused, not wrapped');
    });

    test('TypeScript Rational agrees with the kernel on random inputs (incl. overflow)', () => {
      let seed = 0x2545f491n;
      const next = () => {
        seed = (seed * 6364136223846793005n + 1442695040888963407n) & ((1n << 64n) - 1n);
        const bits = Number(seed >> 58n); // 0..63 bits of magnitude
        const mag = (seed >> 3n) & ((1n << BigInt(bits)) - 1n);
        return seed & 1n ? -mag : mag;
      };
      const k = zxv.kernel;
      for (let i = 0; i < 400; i++) {
        const an = next();
        const ad = next() || 1n;
        const bn = next() || 1n;
        const bd = next() || 1n;
        const a = Rational.of(an, ad);
        const b = Rational.of(bn, bd);
        for (const op of ['add', 'sub', 'mul', 'div'] as const) {
          let ts: string;
          let c: string;
          try { ts = a[op](b).toFraction(); } catch (e) { ts = `E${(e as ZxvError).status}`; }
          try { c = k[op](a, b).toFraction(); } catch (e) { c = `E${(e as ZxvError).status}`; }
          assert.equal(ts, c, `${op} ${a.toFraction()} ${b.toFraction()}`);
        }
        assert.equal(a.toString(), k.format(a));
        assert.equal(a.cmp(b), k.cmp(a, b));
      }
    });

    test('kernel lpres tables', () => {
      for (const t of Truth.values) {
        assert.equal(zxv.kernel.truthName(t), V.lpres.names[t.value]);
        assert.equal(zxv.kernel.not(t), t.not());
        for (const u of Truth.values) {
          assert.equal(zxv.kernel.and(t, u), t.and(u));
          assert.equal(zxv.kernel.or(t, u), t.or(u));
        }
      }
      assert.equal(zxv.kernel.fromEvidence(2, 1), Truth.BOTH);
    });

    test('ubh168 through C: same octets, same truth', () => {
      for (const v of V.ubh168) {
        assert.equal(zxv.wire.tag(v.trunk, v.shell, v.sync), v.tag);
        const c = zxv.wire.pack(v.tag, v.s_plus, v.s_minus);
        assert.equal(c.toHex(), v.frame);
        assert.ok(c.equals(Ubh168Frame.pack(v.tag, v.s_plus, v.s_minus)));
        assert.deepEqual(zxv.wire.unpack(unhex(v.frame)), { tag: v.tag, sPlus: v.s_plus, sMinus: v.s_minus });
        assert.equal(zxv.wire.resolve(Ubh168Frame.unpack(unhex(v.frame))), v.truth);
      }
      V.ubh168_idle.forEach((h, trunk) => assert.equal(zxv.wire.idle(trunk).toHex(), h));
    });

    test('hashes', () => {
      for (const v of V.hash) {
        assert.equal(hex(zxv.hashes.keccak256(v.utf8)), v.keccak256);
        assert.equal(hex(zxv.hashes.sha3_256(v.utf8)), v.sha3_256);
        assert.equal(hex(zxv.hashes.shake256(new TextEncoder().encode(v.utf8), 64)), v.shake256_64);
      }
    });

    test('ed25519', () => {
      using ctx = zxv.createContext();
      const cap = ctx.rootCapability();
      using sk = zxv.secretKey(unhex(V.ed25519.sk));
      assert.equal(hex(Ed25519.publicKey(cap, sk)), V.ed25519.pk);
      const sig = Ed25519.sign(cap, sk, V.ed25519.msg_utf8);
      assert.equal(hex(sig), V.ed25519.sig);
      assert.equal(Ed25519.verify(cap, unhex(V.ed25519.pk), V.ed25519.msg_utf8, sig), true);
      sig[0]! ^= 1;
      assert.equal(Ed25519.verify(cap, unhex(V.ed25519.pk), V.ed25519.msg_utf8, sig), false);
    });

    test('ML-DSA-87 digests', () => {
      using ctx = zxv.createContext();
      const cap = ctx.rootCapability();
      const kp = MlDsa87.fromSeed(cap, unhex(V.mldsa87.seed));
      using sk = kp.secretKey;
      assert.equal(hex(zxv.hashes.sha3_256(kp.publicKey)), V.mldsa87.pk_sha3_256);
      const skCopy = sk.exportBytesUnsafe();
      assert.equal(hex(zxv.hashes.sha3_256(skCopy)), V.mldsa87.sk_sha3_256);
      skCopy.fill(0);
      const sig = MlDsa87.signDeterministic(cap, sk, V.mldsa87.msg_utf8, { context: V.mldsa87.ctx_utf8 });
      assert.equal(hex(zxv.hashes.sha3_256(sig)), V.mldsa87.sig_deterministic_sha3_256);
      assert.equal(MlDsa87.verify(cap, kp.publicKey, V.mldsa87.msg_utf8, sig, { context: V.mldsa87.ctx_utf8 }), true);
      assert.equal(MlDsa87.verify(cap, kp.publicKey, V.mldsa87.msg_utf8, sig, { context: 'zxw' }), false);
      const hedged = MlDsa87.sign(cap, sk, 'agent message');
      assert.equal(MlDsa87.verify(cap, kp.publicKey, 'agent message', hedged), true);
    });

    test('ML-KEM-1024 digests and round trip', () => {
      using ctx = zxv.createContext();
      const cap = ctx.rootCapability();
      const kp = MlKem1024.fromSeeds(cap, unhex(V.mlkem1024.d), unhex(V.mlkem1024.z));
      using dk = kp.secretKey;
      assert.equal(hex(zxv.hashes.sha3_256(kp.publicKey)), V.mlkem1024.ek_sha3_256);
      const enc = MlKem1024.encapsulateDerand(cap, kp.publicKey, unhex(V.mlkem1024.m));
      using ss = enc.sharedSecret;
      assert.equal(hex(zxv.hashes.sha3_256(enc.ciphertext)), V.mlkem1024.ct_sha3_256);
      assert.equal(hex(ss.exportBytesUnsafe()), V.mlkem1024.ss);
      using ss2 = MlKem1024.decapsulate(cap, dk, enc.ciphertext);
      assert.ok(ss2.equals(ss));
      const fresh = MlKem1024.generate(cap);
      const e2 = MlKem1024.encapsulate(cap, fresh.publicKey);
      using back = MlKem1024.decapsulate(cap, fresh.secretKey, e2.ciphertext);
      assert.ok(back.equals(e2.sharedSecret));
      fresh.secretKey.close();
      e2.sharedSecret.close();
      const bad = fresh.publicKey.slice();
      bad.fill(0xff, 0, 32);
      assert.equal(statusOf(() => MlKem1024.encapsulate(cap, bad)), -22, 'malformed ek is FORMAT');
    });

    test('swarm allotments', () => {
      const v = V.swarm;
      using ctx = zxv.createContext();
      const cap = ctx.rootCapability();
      using sw = Swarm.create(cap, { levels: v.levels, tokensPerCycle: v.tokens_per_cycle });
      for (const [m, lvl] of v.models) sw.register(m, lvl);
      sw.setMood(v.mood[0], v.mood[1]);
      for (const [m, cp, i] of v.feelings) sw.setFeeling(m, cp, i);
      sw.beginCycle();
      for (const [m, allotted, imaginary] of v.allotments) {
        const a = sw.allotment(m);
        assert.deepEqual([a.allotted, a.imaginary], [BigInt(allotted), BigInt(imaginary)], `model ${m}`);
      }
      v.level_capacity.forEach((c, lvl) => assert.equal(zxv.swarm.levelCapacity(lvl), c));
      const first = v.allotments[0]!;
      assert.deepEqual(sw.consume(first[0], 10), { granted: 10n, exhausted: false });
      assert.deepEqual(sw.consume(first[0], first[1]), { granted: BigInt(first[1] - 10), exhausted: true });
      assert.equal(sw.endCycle() >= 0n, true);
      assert.ok(zxv.swarm.emotionProfile(0x1f914).temperatureMilli > 0);
    });

    test('evm_state encoding and digest', () => {
      const v = V.evm_state;
      using ctx = zxv.createContext();
      const ch = new StateChannel(ctx.rootCapability({ caps: Cap.EVM }), unhex(v.channel_id));
      const st = ch.encodeState(v.nonce, v.addresses.map((a, i) => ({ address: unhex(a), balance: BigInt(v.balances[i]!) })));
      assert.equal(hex(st.encoding), v.encoding);
      assert.equal(hex(st.digest), v.digest);
      assert.equal(hex(zxv.hashes.keccak256(st.encoding)), v.digest);
    });

    test('usage errors are typed', () => {
      assert.throws(() => zxv.kernel.parse('1e5'), ZxvUsageError);
      assert.throws(() => zxv.random(1.5), FloatRejectedError);
      assert.notDeepEqual(zxv.random(32), zxv.random(32));
    });
  });
}
