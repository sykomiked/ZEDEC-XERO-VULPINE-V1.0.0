// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// ABI 1.4: hardware emulation (profiles, the matrix) and hardware updates
// (regions) on both backends, mirroring sdk/libzxv/test/test_zxv_abi14.c.
// SLH-DSA signing takes seconds, so the file signs exactly three packages
// (on the first backend) and both backends verify the same bytes.

import assert from 'node:assert/strict';
import { createHash, createHmac } from 'node:crypto';
import { before, describe, test } from 'node:test';

import {
  Cap, CapabilityDeniedError, encodeVectors, HW_HEADER_BYTES, HW_PK_BYTES, HW_ROOT, HW_SIG_BYTES, HwCapKey, HwClass,
  HwEvent, HwFn, HwImageKind, HwKind, HwMatrix, HwPairing, HwPolicy, HwPower, HwProfile, HwProviderFlag, HwRegion,
  HwRegionState, HwReleaseKey, HwResourceState, type RegionEvent, Truth, VerificationFailedError, type Zxv, ZxvError,
  ZxvFatalError, ZxvRefusedError, ZxvUsageError,
} from '../src/index.js';
import { BACKENDS, load } from './helpers.js';

const hex = (b: Uint8Array) => Buffer.from(b).toString('hex');
const ABC = 'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad';
const le64 = (...v: bigint[]) => {
  const b = Buffer.alloc(8 * v.length);
  v.forEach((x, i) => b.writeBigUInt64LE(x, 8 * i));
  return new Uint8Array(b);
};
const macInput = (key: string, msg: string) => {
  const k = Buffer.from(key);
  const b = Buffer.alloc(2 + k.length + Buffer.byteLength(msg));
  b.writeUInt16LE(k.length, 0);
  k.copy(b, 2);
  b.write(msg, 2 + k.length);
  return new Uint8Array(b);
};

function statusOf(f: () => unknown): number {
  try {
    f();
    return 0;
  } catch (e) {
    if (e instanceof ZxvError || e instanceof ZxvFatalError) return e.status;
    throw e;
  }
}

/** The release key and the three signed packages, built once. */
interface Release {
  pk: Uint8Array;
  good: Uint8Array;
  bad: Uint8Array;
  vectors: Uint8Array;
}
let release: Release | undefined;
const INC = Uint8Array.from({ length: 256 }, (_, i) => (i + 1) & 0xff);

function makeRelease(zxv: Zxv): Release {
  if (release) return release;
  using ctx = zxv.createContext();
  const cap = ctx.rootCapability({ caps: Cap.ALL_1_4 });
  using key = HwReleaseKey.generate(cap);
  const wrong = INC.slice();
  wrong[77] = 0;
  release = {
    pk: key.publicKey.slice(),
    good: key.build(cap, HwImageKind.PARTIAL, { targetClass: 0xd0, fnId: 0x200, version: 1, svn: 1, payload: INC }),
    bad: key.build(cap, HwImageKind.PARTIAL, { targetClass: 0xd0, fnId: 0x200, version: 2, svn: 1, payload: wrong }),
    vectors: key.build(cap, HwImageKind.VECTORS, {
      fnId: 0x200, version: 7, svn: 3, payload: encodeVectors([[0, Uint8Array.of(5), Uint8Array.of(6)], [0, Uint8Array.of(250), Uint8Array.of(251)]]),
    }),
  };
  // A payload that is not a vector list is refused before anything is signed.
  assert.equal(statusOf(() => key.build(cap, HwImageKind.VECTORS, { fnId: 0x200, version: 8, svn: 3, payload: Uint8Array.of(0, 1) })), -22);
  assert.equal(key.closed, false);
  key.close();
  assert.ok(key.closed && key.mldsaSecret.closed && key.slhdsaSecret.closed);
  return release;
}

for (const kind of BACKENDS) {
  describe(`hardware on the ${kind} backend`, () => {
    let zxv: Zxv;
    before(async () => {
      zxv = await load(kind);
    });

    test('ABI 1.4: feature, statuses, capability bits', () => {
      assert.ok(zxv.version.minor >= 4 && zxv.has('HW'));
      const names: Record<number, string> = {
        [-25]: 'ZXV_E_NOPROVIDER', [-26]: 'ZXV_E_DISPUTE', [-27]: 'ZXV_E_QUARANTINED', [-28]: 'ZXV_E_ROLLBACK',
        [-29]: 'ZXV_E_BLOCKED', [-30]: 'ZXV_E_TARGET', [-31]: 'ZXV_E_POLICY',
      };
      for (const [c, n] of Object.entries(names)) assert.equal(zxv.statusName(Number(c)), n);
      assert.equal(zxv.statusClass(-26), 2);
      assert.equal(zxv.statusClass(-30), 1);
      assert.equal(zxv.statusClass(-31), 2);
      assert.equal(Cap.ALL, (1n << 10n) - 1n);
      assert.equal(Cap.ALL_1_4, Cap.ALL | Cap.HW_MATRIX | Cap.HW_RECONF);
    });

    test('profiles: R1, the host, presentation, matching', () => {
      using host = HwProfile.probeHost(zxv);
      using r1 = HwProfile.reference(zxv);
      assert.equal(r1.count, 16);
      assert.ok(host.count >= 2);
      const word = host.getCap(2, HwCapKey.WORD_BITS);
      assert.ok(kind === 'native' ? word === 64n : word === 32n || word === 64n, `word bits ${word}`);
      assert.ok((host.getCap(2, HwCapKey.COUNT) ?? 0n) >= 1n);
      const [nat, emu] = HwProfile.match(host, r1);
      assert.equal(nat + emu, 14, `native ${nat} emulated ${emu}`);
      assert.deepEqual([HwProfile.pair(host, r1, 1).needId, HwProfile.pair(host, r1, 1).kind], [2, HwPairing.GROUP]);
      const cpu = HwProfile.pair(host, r1, 2);
      if (kind === 'native') {
        // A 64-bit host runs R1's CPU natively; a wasm32 "host" emulates even that.
        assert.ok(nat >= 1);
        assert.deepEqual([cpu.needId, cpu.kind, cpu.haveId], [3, HwPairing.NATIVE, 2]);
      } else {
        assert.equal(cpu.needId, 3);
      }
      assert.equal(HwProfile.pair(host, r1, 7).kind, HwPairing.EMULATE);
      assert.equal(statusOf(() => HwProfile.pair(host, r1, 99)), -4);
      assert.equal(r1.getCap(6, HwCapKey.CONTRACT_ID), BigInt(HwFn.TENSOR));
      using pres = HwProfile.present(host, r1);
      assert.equal(pres.getCap(3, HwCapKey.KIND), BigInt(cpu.kind === HwPairing.NATIVE ? HwKind.HARD : HwKind.SOFT));
      assert.equal(pres.getCap(8, HwCapKey.KIND), BigInt(HwKind.SOFT));
      assert.equal(pres.getCap(1, HwCapKey.KIND), undefined);
      const nodes = r1.nodes();
      assert.equal(nodes.length, 16);
      assert.deepEqual([nodes[0]!.parent, nodes[0]!.cls], [HW_ROOT, HwClass.MACHINE]);
    });

    test('profiles: canonical encoding, digest, private extension', () => {
      using r1 = HwProfile.reference(zxv);
      const enc = r1.encode();
      using copy = HwProfile.decode(zxv, enc);
      assert.equal(hex(copy.digest()), hex(r1.digest()));
      assert.equal(hex(r1.digest()), createHash('sha256').update(enc).digest('hex'));
      assert.equal(statusOf(() => HwProfile.decode(zxv, enc.subarray(0, enc.length - 1))), -22);
      using mine = HwProfile.empty(zxv);
      mine.addNode(1, HW_ROOT, HwClass.MACHINE);
      mine.addNode(2, 1, HwClass.TERNARY_UNIT);
      mine.setCap(2, HwCapKey.RADIX, 3);
      assert.throws(() => mine.addNode(2, 1, HwClass.CACHE), ZxvUsageError);
      assert.equal(statusOf(() => mine.setCap(9, HwCapKey.RADIX, 3)), -4);
      mine.addNode(3, 1, (0x80000000 | 1) >>> 0);
      const p = HwProfile.pair(mine, r1, 7);
      assert.deepEqual([p.kind, p.haveId], [HwPairing.NATIVE, 2]);
      const d1 = hex(mine.digest());
      assert.equal(mine.getExt(3).length, 0);
      mine.setExt(3, Uint8Array.of(0xa5, 0, 0x5a));
      assert.notEqual(hex(mine.digest()), d1, 'the extension is part of the identity');
      assert.equal(hex(mine.getExt(3)), 'a5005a');
      assert.equal(statusOf(() => mine.setExt(3, new Uint8Array(257))), -8);
      assert.throws(() => mine.setCap(2, HwCapKey.RADIX, 3.5));
    });

    test('capability gating', () => {
      using ctx = zxv.createContext();
      assert.throws(() => HwMatrix.create(ctx.rootCapability()), CapabilityDeniedError);
      using mx = HwMatrix.create(ctx.rootCapability({ caps: Cap.HW_MATRIX }));
      assert.throws(() => mx.trustKey(new Uint8Array(HW_PK_BYTES)), CapabilityDeniedError);
    });

    test('standard engines and discovery on a fresh matrix', () => {
      using ctx = zxv.createContext();
      using mx = HwMatrix.create(ctx.rootCapability({ caps: Cap.ALL_1_4 }));
      assert.equal(hex(mx.call(HwFn.SHA256, new TextEncoder().encode('abc'))), ABC);
      const want = createHmac('sha256', 'Jefe').update('what do ya want for nothing?').digest('hex');
      assert.equal(hex(mx.call(HwFn.MAC, macInput('Jefe', 'what do ya want for nothing?'))), want);
      assert.equal(hex(mx.call(HwFn.GCD, le64(12n, 18n))), hex(le64(6n)));
      assert.equal(hex(mx.call(HwFn.KECCAK, new TextEncoder().encode('abc'))), createHash('sha3-256').update('abc').digest('hex'));
      const infos = mx.providers();
      assert.ok(infos.length === mx.providerCount && infos.length >= 5);
      for (const i of infos) {
        assert.deepEqual([i.kind, i.proof, i.contract], [HwKind.SOFT, Truth.TRUE, 1]);
        assert.equal(i.flags & HwProviderFlag.FAULT, 0);
      }
      const fns = new Set(infos.map((i) => i.fnId));
      for (const f of [HwFn.SHA256, HwFn.KECCAK, HwFn.MAC, HwFn.GCD, HwFn.LOGIC64]) assert.ok(fns.has(f));
      assert.equal(mx.bound(HwFn.SHA256), infos.find((i) => i.fnId === HwFn.SHA256)!.index);
      assert.equal(statusOf(() => mx.providerInfo(infos.length)), -4);
      const fi = mx.functionInfo(HwFn.MAC);
      assert.ok(fi.contract === 1 && (fi.policy & HwPolicy.KEY_BEARING) !== 0 && fi.vectors >= 1);
    });

    test('key-bearing work never goes REMOTE; placement policy', () => {
      using ctx = zxv.createContext();
      using mx = HwMatrix.create(ctx.rootCapability({ caps: Cap.ALL_1_4 }));
      const macPv = mx.providers().find((i) => i.fnId === HwFn.MAC)!.index;
      let remoteHits = 0;
      mx.addResource(7);
      const prem = mx.addProvider(HwFn.MAC, 7, HwKind.REMOTE, 1, () => {
        remoteHits++;
        return -9; // must never serve key-bearing work
      });
      mx.attest(prem); // fails its vectors anyway
      const want = createHmac('sha256', 'Jefe').update('hi').digest('hex');
      assert.equal(hex(mx.call(HwFn.MAC, macInput('Jefe', 'hi'))), want);
      assert.equal(mx.bound(HwFn.MAC), macPv);
      assert.ok(mx.providerInfo(prem).flags & HwProviderFlag.POLICY);
      assert.equal(statusOf(() => mx.setPolicy(HwFn.MAC, { flags: 0 })), -31);
      assert.equal(statusOf(() => mx.memo(HwFn.MAC, 16)), -31);
      assert.equal(statusOf(() => mx.setPolicy(HwFn.MAC, { flags: 0x80 })), -1);
      assert.ok(remoteHits <= 1, 'only its own proof ran it');

      const g = le64(12n, 18n);
      const gcdPv = mx.bound(HwFn.GCD);
      mx.setPolicy(HwFn.GCD, { powerBudgetMw: 500 });
      assert.equal(statusOf(() => mx.call(HwFn.GCD, g)), -25);
      mx.setTraits(gcdPv, 2000, 300, 4);
      assert.equal(statusOf(() => mx.setTraits(gcdPv, 2000, 300, 16)), -8);
      assert.equal(hex(mx.call(HwFn.GCD, g)), hex(le64(6n)));
      const i = mx.providerInfo(gcdPv);
      assert.deepEqual([i.throughputClass, i.powerClass], [4, HwPower.W1]);
      mx.setPolicy(HwFn.GCD, { flags: HwPolicy.LOCAL_ONLY, maxLatencyNs: 1000 });
      assert.equal(statusOf(() => mx.call(HwFn.GCD, g)), -25);
      mx.setPolicy(HwFn.GCD);
    });

    test('JS implementations: proof, dependencies, failover, shadow, memo, contracts', () => {
      using ctx = zxv.createContext();
      using mx = HwMatrix.create(ctx.rootCapability({ caps: Cap.ALL_1_4 }));
      mx.addFunction(0x100, 'add8');
      mx.addFunction(0x101, 'add16');
      assert.equal(statusOf(() => mx.addFunction(0x100, 'again')), -13);
      mx.addResource(1);
      mx.addResource(2);
      const vin = Uint8Array.of(0, 1, 9, 200);
      const ex8 = Uint8Array.of(8, 9, 17, 208);
      const ex16 = Uint8Array.of(16, 17, 25, 216);
      mx.addVector(0x100, 0, vin, ex8);
      mx.addVector(0x101, 0, vin, ex16);
      const st = { hard: 0, soft: 0 };
      const add8 = (which: 'hard' | 'soft') => (_op: number, d: Uint8Array) => {
        if (st[which] === 2) return -23;
        return d.map((b) => (b + 8 + (st[which] && b === 9 ? 1 : 0)) & 0xff);
      };
      const pfx = mx.addProvider(0x100, 2, HwKind.HARD, 1, add8('hard'));
      const psw = mx.addProvider(0x100, 1, HwKind.SOFT, 10, add8('soft'));
      const p16 = mx.addProvider(0x101, 1, HwKind.SOFT, 20, (op, d) => mx.call(0x100, mx.call(0x100, d, { op }), { op }));
      assert.equal(statusOf(() => mx.addProvider(0x100, 1, 9 as HwKind, 1, add8('soft'))), -1);

      assert.equal(statusOf(() => mx.call(0x100, vin)), -25);
      assert.equal(mx.attest(pfx), Truth.TRUE);
      assert.equal(mx.attest(psw), Truth.TRUE);
      assert.equal(mx.attest(p16), Truth.FALSE, 'an undeclared dependency must not prove');
      mx.addDependency(p16, 0x100);
      assert.equal(mx.attest(p16), Truth.TRUE);
      assert.deepEqual(mx.call(0x101, vin), ex16);

      assert.equal(mx.bound(0x100), pfx);
      mx.setResource(2, HwResourceState.FAULTED);
      assert.deepEqual(mx.call(0x100, vin), ex8);
      assert.equal(mx.bound(0x100), psw, 'failover to the emulator');
      mx.setResource(2, HwResourceState.OK);

      st.hard = 2;
      assert.equal(statusOf(() => mx.call(0x100, vin)), -23, "a provider's status comes back as is");
      st.hard = 0;

      const sh = mx.shadow(0x100, vin);
      assert.deepEqual([sh.output, sh.checked], [ex8, true]);
      st.soft = 1;
      assert.equal(statusOf(() => mx.shadow(0x100, vin)), -26);
      assert.equal(statusOf(() => mx.call(0x100, vin)), -25);
      assert.equal(mx.attest(pfx), Truth.TRUE);
      assert.equal(mx.attest(psw), Truth.FALSE);

      assert.equal(statusOf(() => mx.memo(0x100, 6)), -1);
      mx.memo(0x100, 16);
      const calls0 = mx.providerInfo(pfx).calls;
      mx.call(0x100, vin);
      assert.deepEqual(mx.call(0x100, vin), ex8);
      assert.deepEqual(mx.memo(0x100, 16), { hits: 1n, misses: 1n });
      assert.equal(mx.providerInfo(pfx).calls, calls0 + 1n);

      assert.deepEqual(mx.functionInfo(0x100), { contract: 0, policy: 0, vectors: 1 });
      mx.setContract(0x100, 2);
      assert.equal(statusOf(() => mx.setContract(0x100, 2)), -28);
      mx.addVector(0x100, 0, vin, ex8);
      mx.attest(pfx);
      const i = mx.providerInfo(pfx);
      assert.deepEqual([i.proof, i.contract], [Truth.TRUE, 2]);
      assert.ok(mx.providerInfo(psw).flags & HwProviderFlag.UNPROVEN);

      // A JS exception inside an implementation is re-thrown from the SDK call.
      mx.addFunction(0x102);
      mx.addVector(0x102, 0, Uint8Array.of(1), Uint8Array.of(2));
      const pb = mx.addProvider(0x102, 1, HwKind.SOFT, 1, () => {
        throw new RangeError('boom');
      });
      assert.throws(() => mx.attest(pb), RangeError);
    });

    test('signed image: stage, trial, commit, events, audit log, contract package', () => {
      const r = makeRelease(zxv);
      assert.equal(r.good.length, HW_HEADER_BYTES + 256 + HW_SIG_BYTES);
      const ctx = zxv.createContext();
      const cap = ctx.rootCapability({ caps: Cap.ALL_1_4 });
      const oracle = HwMatrix.create(cap);
      const mx = HwMatrix.create(cap);
      mx.addFunction(0x200, 'inc');
      mx.addResource(1);
      mx.addResource(2);
      const vin = Uint8Array.from({ length: 256 }, (_, i) => i);
      mx.addVector(0x200, 0, vin, INC);
      const sim = { slot: [new Uint8Array(0), new Uint8Array(0)], active: -1 };
      const device = {
        load(slot: number, payload: Uint8Array) {
          if (slot > 1 || payload.length > 256) return -8;
          sim.slot[slot] = payload.slice();
          return undefined;
        },
        readback: (slot: number) => oracle.call(HwFn.SHA256, sim.slot[slot]!),
        activate(slot: number) {
          sim.active = slot;
        },
      };
      const psoft = mx.addProvider(0x200, 1, HwKind.SOFT, 50, (_op, d) => d.map((b) => (b + 1) & 0xff));
      const preg = mx.addProvider(0x200, 2, HwKind.FABRIC, 1, (_op, d) =>
        sim.active < 0 ? -9 : d.map((b) => sim.slot[sim.active]![b]!));
      assert.equal(mx.attest(psoft), Truth.TRUE);

      const nohw = cap.attenuate(Cap.ALL);
      assert.throws(() => HwRegion.create(nohw, device, { regionId: 1, targetClass: 0xd0, matrix: mx, provider: preg }), CapabilityDeniedError);
      assert.throws(() => HwRegion.create(cap, device, { regionId: 1, targetClass: 0xd0, matrix: mx, provider: psoft }), ZxvUsageError);
      const rg = HwRegion.create(cap, device, { regionId: 1, targetClass: 0xd0, matrix: mx, provider: preg, logCapacity: 12 });
      const events: RegionEvent[] = [];
      rg.onEvent((e) => events.push(e));
      rg.trustKey(r.pk);

      const wrongHw = r.good.slice();
      new DataView(wrongHw.buffer).setUint32(12, 0xd1, true);
      assert.equal(statusOf(() => rg.stage(wrongHw)), -30);
      const tampered = r.good.slice();
      tampered[HW_HEADER_BYTES + 256 + 100]! ^= 1; // inside the ML-DSA signature
      assert.throws(() => rg.stage(tampered), VerificationFailedError);

      rg.stage(r.good);
      assert.equal(statusOf(() => rg.commit()), -9);
      rg.trial(5);
      assert.equal(mx.bound(0x200), preg, 'the reconfigured region serves');
      rg.commit();
      assert.deepEqual(rg.info(), { state: HwRegionState.ACTIVE, version: 1, floor: 1, quarantined: 1 });
      assert.deepEqual(events.map((e) => e.event), [HwEvent.REJECTED, HwEvent.STAGED, HwEvent.TESTED, HwEvent.ACTIVATED]);
      assert.ok(events.every((e) => e.regionId === 1) && events.at(-1)!.version === 1);
      assert.deepEqual(mx.call(0x200, Uint8Array.of(0, 5)), Uint8Array.of(1, 6));

      assert.equal(statusOf(() => rg.stage(r.good)), -28, 'replay');
      rg.stage(r.bad);
      assert.equal(statusOf(() => rg.trial(5)), -27, 'quarantined, rolled back to v1');
      const info = rg.info();
      assert.deepEqual([info.state, info.version], [HwRegionState.ACTIVE, 1]);
      assert.equal(mx.bound(0x200), preg);
      const blocked = r.bad.slice();
      new DataView(blocked.buffer).setUint32(20, 3, true);
      assert.equal(statusOf(() => rg.stage(blocked)), -29);

      const { head, count } = rg.log();
      assert.equal(count, 8);
      assert.equal(count, events.length);
      const last = rg.logEntry(count - 1);
      assert.deepEqual([last.event, last.id, last.version], [HwEvent.QUARANTINED, 1, 2]);
      assert.equal(hex(last.hash), hex(head));
      assert.ok(last.tag.isForbidden);
      const act = rg.logEntry(3);
      assert.ok(act.event === HwEvent.ACTIVATED && act.version === 1 && !act.tag.isForbidden);
      assert.equal(statusOf(() => rg.logEntry(count)), -4);

      // A contract package: a newer golden-vector set for "inc".
      assert.throws(() => mx.applyContract(r.vectors), VerificationFailedError); // no key trusted yet
      mx.trustKey(r.pk);
      assert.equal(statusOf(() => mx.applyContract(r.good)), -22);
      assert.equal(statusOf(() => rg.stage(r.vectors)), -22);
      assert.equal(mx.applyContract(r.vectors), 2);
      assert.deepEqual([mx.functionInfo(0x200).contract, mx.functionInfo(0x200).vectors], [7, 2]);
      assert.equal(statusOf(() => mx.applyContract(r.vectors)), -28);
      assert.equal(mx.log().count, 3);
      const rec = mx.logEntry(1);
      assert.deepEqual([rec.event, rec.id, rec.version], [HwEvent.CONTRACT, 0x200, 7]);

      // Fill the log: the next recorded refusal halts the region and poisons the context.
      for (let n = rg.log().count; n < 12; n++) assert.throws(() => rg.stage(tampered), VerificationFailedError);
      assert.throws(() => rg.stage(tampered), ZxvFatalError);
      ctx.close();
      assert.ok(rg.closed && mx.closed && oracle.closed);
    });

    test('a device exception surfaces from the region call', () => {
      const r = makeRelease(zxv);
      using ctx = zxv.createContext();
      const cap = ctx.rootCapability({ caps: Cap.ALL_1_4 });
      const rg = HwRegion.create(cap, {
        load() {
          throw new Error('device on fire');
        },
        readback: () => new Uint8Array(32),
        activate: () => undefined,
      }, { regionId: 9, targetClass: 0xd0 });
      rg.trustKey(r.pk);
      assert.throws(() => rg.stage(r.good), /device on fire/);
      assert.equal(rg.info().version, 0);
      assert.ok(rg instanceof HwRegion);
      assert.throws(() => HwRegion.create(cap, {} as never, { regionId: 2, targetClass: 0 }), ZxvUsageError);
      assert.ok(!(new Error() instanceof ZxvRefusedError));
    });
  });
}
