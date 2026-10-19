// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// ABI 1.5 on both backends against vectors.json (status_abi15, treaty, cid,
// aipi, coin, fee, vhub, freight, web4, paradox), plus handle lifetimes,
// the two-call buffer path, error mapping, capability refusals and a
// poisoned context.

import assert from 'node:assert/strict';
import { describe, test } from 'node:test';

import { errorFor } from '../src/errors.js';
import {
  AipiNode, Cap, CapabilityDeniedError, CidCodec, CidMismatchError, ConsentDeniedError, DenominationError, ErasureError,
  FreightFlag, LimitExceededError, MlDsa87, OwnerApprovalRequiredError, ParadoxBook, ParadoxGraph, ParadoxHold, ParadoxLog,
  ParadoxOutcome, ParadoxPolicy, ParadoxStepLimitError, PegError, RepairProof, TokenWallet, TreatyError, VerificationFailedError,
  VHub, W4Agent, W4ConsentScope, W4MessageFlag, W4MessageKind, type Zxv, ZxvError, ZxvFatalError, ZxvRefusedError,
  ZxvUsageError, type Capability, type FreightHeader,
} from '../src/index.js';
import { BACKENDS, load, V } from './helpers.js';

// eslint-disable-next-line @typescript-eslint/no-explicit-any
type Any = any;
const X = V as unknown as Any;
const hex = (b: Uint8Array): string => Buffer.from(b).toString('hex');
const unhex = (h: string): Uint8Array => new Uint8Array(Buffer.from(h, 'hex'));
const utf8 = (s: string): Uint8Array => new TextEncoder().encode(s);
const B = (x: string | number): bigint => BigInt(x);
const ramp = (n: number): Uint8Array => Uint8Array.from({ length: n }, (_, i) => i & 255);
const fill = (n: number, b: number): Uint8Array => new Uint8Array(n).fill(b);

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

/** The value `f` returns, or the status name of what it threw. */
function val<T>(f: () => T): T | string {
  try {
    return f();
  } catch (e) {
    if (e instanceof ZxvError) return e.statusName;
    throw e;
  }
}

/** A holding from the vectors' sparse [[tier, count], ...] form. */
const sparse = (zxv: Zxv, c: [number, string][]) => zxv.tokens.holding(c.map(([t, n]) => [t, B(n)] as const));
const toSparse = (h: readonly bigint[]): [number, string][] =>
  h.flatMap((n, t) => (n === 0n ? [] : [[t, n.toString()] as [number, string]]));

describe('ABI 1.5 status table (pure)', () => {
  test('errorFor maps every new status to its class', () => {
    const want: Record<string, new (...a: Any[]) => Error> = {
      ZXV_E_DENOM: DenominationError, ZXV_E_TREATY: TreatyError, ZXV_E_CID: CidMismatchError, ZXV_E_CONSENT: ConsentDeniedError,
      ZXV_E_ERASURE: ErasureError, ZXV_E_PEG: PegError, ZXV_E_LIMIT: LimitExceededError, ZXV_E_PARADOX_STEP: ParadoxStepLimitError,
      ZXV_E_PARADOX_OWNER: OwnerApprovalRequiredError,
    };
    for (const s of X.status_abi15 as { code: number; name: string; class: number }[]) {
      const e = errorFor(s.code, s.class, s.name, 'detail');
      const cls = want[s.name];
      assert.ok(cls, s.name);
      assert.ok(e instanceof cls, s.name);
      assert.ok(s.class === 1 ? e instanceof ZxvUsageError : e instanceof ZxvRefusedError, s.name);
      assert.equal((e as ZxvError).status, s.code);
    }
  });
});

for (const kind of BACKENDS) {
  describe(`ABI 1.5 on the ${kind} backend`, () => {
    let zxv: Zxv;

    test('loads ABI 1.5 with every FEAT2 group', async () => {
      zxv = await load(kind);
      assert.ok(zxv.version.minor >= 5);
      // ABI 1.6 adds VINO (0x100); every 1.5 group is still there.
      assert.deepEqual(zxv.features2().slice(0, 8), ['TREATY', 'AIPI', 'COIN', 'FEE', 'VHUB', 'FREIGHT', 'WEB4', 'PARADOX']);
      assert.equal(zxv.featureMask2 & 0xff, 0xff);
      assert.ok(zxv.has('PARADOX') && zxv.has('HEX'));
      for (const s of X.status_abi15 as { code: number; name: string; class: number }[]) {
        assert.equal(zxv.statusName(s.code), s.name);
        assert.equal(zxv.statusClass(s.code), s.class);
      }
    });

    test('treaty: Sutra, records, rejects, PIC arithmetic, runs', () => {
      const T = X.treaty;
      for (const s of T.sutra) {
        assert.equal(st(() => zxv.treaty.parseSutra(s.sutra)), s.status);
        using t = zxv.treaty.parseSutra(s.sutra);
        assert.equal(hex(t.toRecord()), s.record);
        assert.equal(t.toRecord().length, T.rec_len);
        using r = zxv.treaty.parseRecord(unhex(s.record));
        assert.equal(r.equals(t), s.record_parses_equal);
        assert.equal(t.toSutra(), s.reemit);
        assert.deepEqual([t.name, t.asset, t.op, t.partyCount, t.obligationCount], [s.name, s.asset, s.op, s.party_count, s.owe_count]);
        assert.deepEqual(t.parties().map((p) => [p.id, p.deposit, p.claim, p.weightNum, p.weightDen]),
          s.parties.map((p: Any) => [p.id, B(p.deposit), B(p.claim), p.weight[0], p.weight[1]]));
        assert.deepEqual(t.obligations().map((o) => [o.from, o.to, o.amount]), s.owes.map((o: Any) => [o.from, o.to, B(o.amount)]));
      }
      for (const r of T.reject) {
        let e: unknown;
        try {
          zxv.treaty.parseSutra(r.sutra);
        } catch (x) {
          e = x;
        }
        assert.ok(e instanceof TreatyError, r.sutra);
        assert.equal(e.statusName, r.status);
        assert.deepEqual([e.fault, e.offset], [r.fault, r.offset], r.sutra);
      }
      for (const r of T.reject_record) {
        const rec = unhex(T.sutra[0].record);
        rec[r.patch_offset] = Number.parseInt(r.patch_byte, 16);
        let e: unknown;
        try {
          zxv.treaty.parseRecord(rec);
        } catch (x) {
          e = x;
        }
        assert.ok(e instanceof TreatyError);
        assert.deepEqual([e.statusName, e.fault, e.offset], [r.status, r.fault, r.offset]);
      }
      for (const p of T.pic) {
        const a = B(p.a);
        const b = B(p.b);
        const f = (): bigint | { quotient: bigint; remainder: bigint } =>
          p.op === 'add' ? zxv.treaty.picAdd(a, b) : p.op === 'sub' ? zxv.treaty.picSub(a, b)
            : p.op === 'mul' ? zxv.treaty.picMul(a, b) : zxv.treaty.picDiv(a, b);
        assert.equal(st(f), p.status, `${p.op} ${p.a} ${p.b}`);
        if (p.status !== 'ZXV_OK') continue;
        const out = f();
        if (typeof out === 'bigint') assert.equal(out, B(p.out));
        else assert.deepEqual([out.quotient, out.remainder], [B(p.out), B(p.rem)]);
      }
      for (const r of T.run) {
        using t = zxv.treaty.parseSutra(r.sutra);
        assert.equal(st(() => t.run().close()), r.status);
        if (r.status !== 'ZXV_OK') continue;
        using res = t.run();
        const s = res.summary();
        assert.deepEqual([s.parties, s.legs, s.fees], [r.parties, r.legs, B(r.fees)]);
        assert.deepEqual(res.parties().map((p) => [p.net, p.balance, p.dividend]),
          r.party.map((p: Any) => [B(p.net), B(p.balance), B(p.dividend)]));
        assert.deepEqual(res.legs().map((l) => [l.from, l.to, l.amount]), r.leg.map((l: Any) => [l.from, l.to, B(l.amount)]));
        assert.equal(hex(res.toRecord()), r.result_record);
        assert.equal(res.toRecord().length, T.result_len);
      }
    });

    test('cid: compute, unixfs, text, rejects, verify', () => {
      const C = X.cid;
      for (const c of C.compute) {
        const cid = zxv.cid.compute(utf8(c.utf8), c.codec);
        assert.equal(hex(cid.toBytes()), c.cid);
        assert.equal(cid.toString(), c.text_default);
        assert.equal(val(() => cid.toString(1)), c.text_58btc);
        assert.deepEqual([cid.version, cid.codec, cid.hash, hex(cid.digest)], [c.version, c.codec, c.hash, c.digest]);
      }
      for (const u of C.unixfs) {
        const data = u.ramp ? ramp(u.len) : utf8(u.utf8);
        const cid = zxv.cid.unixfs(data);
        assert.equal(hex(cid.toBytes()), u.cid);
        assert.equal(String(cid), u.text_default);
        if (u.codec !== undefined) assert.equal(cid.codec, u.codec);
      }
      for (const t of C.text) {
        const cid = zxv.cid.parse(t.text);
        assert.equal(hex(cid.toBytes()), t.cid);
        assert.deepEqual([cid.toString(), val(() => cid.toString(1)), cid.version, cid.codec, cid.hash, hex(cid.digest)],
          [t.text_default, t.text_58btc, t.version, t.codec, t.hash, t.digest]);
        assert.equal(hex(cid.toV1().toBytes()), t.v1);
        assert.equal(cid.toV1().toString(), t.v1_text);
        assert.equal(hex(cid.providerKey()), t.provider_key);
        assert.ok(zxv.cid.fromBytes(cid.toBytes()).equals(cid));
      }
      for (const t of C.text_reject) assert.equal(st(() => zxv.cid.parse(t.text)), t.status, t.text);
      assert.throws(() => zxv.cid.parse('z'), CidMismatchError);
      for (const v of C.verify) {
        assert.equal(st(() => zxv.cid.fromBytes(unhex(v.cid)).verify(utf8(v.utf8))), v.status);
      }
      const abc = zxv.cid.compute(utf8('abc'));
      assert.ok(abc.matches(utf8('abc')) && !abc.matches(utf8('abd')));
      assert.throws(() => abc.verify(utf8('abd')), CidMismatchError);
      assert.throws(() => +abc, TypeError, 'no implicit number');
      assert.equal(zxv.cid.compute(utf8('abc'), CidCodec.RAW).toString(), abc.toString());
    });

    test('aipi: function, call and claim records', () => {
      const A = X.aipi;
      const mod = zxv.cid.compute(utf8(A.module_utf8));
      const con = zxv.cid.compute(utf8(A.contract_utf8));
      assert.equal(hex(mod.toBytes()), A.module_cid);
      assert.equal(hex(con.toBytes()), A.contract_cid);
      const fnCids: string[] = [];
      for (const f of A.fn) {
        const spec = {
          intent: f.intent, verify: f.verify, privacy: f.privacy, determinism: f.determinism, quorumN: f.quorum_n, quorumK: f.quorum_k,
          fuelMax: B(f.fuel_max), module: mod, entry: f.entry as string,
          ...(f.contract ? { contract: con } : {}),
          ...(f.checker !== null ? { checker: f.checker as string } : {}),
          ...(f.certifier !== null ? { certifier: f.certifier as string } : {}),
        };
        assert.equal(st(() => zxv.aipi.encodeFunction(spec)), f.status, f.entry);
        if (f.status !== 'ZXV_OK') continue;
        const rec = zxv.aipi.encodeFunction(spec);
        assert.equal(hex(rec), f.record);
        const cid = zxv.aipi.functionCid(rec);
        assert.equal(hex(cid.toBytes()), f.fn_cid);
        assert.equal(cid.toString(1), f.fn_cid_text);
        fnCids.push(f.fn_cid);
        const d = zxv.aipi.decodeFunction(rec);
        assert.deepEqual([d.intent, d.verify, d.fuelMax, d.entry, d.checker, d.certifier, d.contract !== undefined],
          [f.intent, f.verify, B(f.fuel_max), f.entry, f.checker ?? '', f.certifier ?? '', f.contract]);
        assert.ok(d.module.equals(mod));
      }
      const ref = (s: string, list: Any[], key: string): string => {
        const m = /^\w+\[(\d+)\]\.(\w+)$/.exec(s);
        return m ? list[Number(m[1])][key] : A[s];
      };
      for (const c of A.call) {
        const fn = zxv.cid.fromBytes(unhex(ref(c.fn, A.fn, 'fn_cid')));
        if (c.status !== undefined && c.status !== 'ZXV_OK') {
          assert.equal(st(() => zxv.aipi.encodeCall(fn, utf8(c.input_utf8))), c.status);
          assert.throws(() => zxv.aipi.callCid(fn, utf8(c.input_utf8)), CidMismatchError);
          continue;
        }
        const rec = zxv.aipi.encodeCall(fn, utf8(c.input_utf8));
        assert.equal(hex(rec), c.record);
        const cid = zxv.aipi.callCid(fn, utf8(c.input_utf8));
        assert.equal(hex(cid.toBytes()), c.call_cid);
        assert.equal(cid.toString(1), c.call_cid_text);
        const d = zxv.aipi.decodeCall(rec);
        assert.ok(d.fn.equals(fn));
        assert.equal(Buffer.from(d.input).toString(), c.input_utf8);
      }
      for (const c of A.claim) {
        const call = zxv.cid.fromBytes(unhex(ref(c.call, A.call, 'call_cid')));
        const rec = zxv.aipi.encodeClaim(call, unhex(c.executor), utf8(c.output_utf8), utf8(c.cert_utf8));
        assert.equal(hex(rec), c.record);
        const d = zxv.aipi.decodeClaim(rec);
        assert.ok(d.call.equals(call));
        assert.deepEqual([hex(d.executor), Buffer.from(d.output).toString(), Buffer.from(d.cert).toString()],
          [c.executor, c.output_utf8, c.cert_utf8]);
      }
    });

    test('aipi node: put, get, publish (needs AIPI)', () => {
      using ctx = zxv.createContext();
      assert.throws(() => AipiNode.create(ctx.rootCapability()), CapabilityDeniedError);
      using node = AipiNode.create(ctx.rootCapability({ caps: Cap.ALL_1_5 }));
      const cid = node.put(utf8('hello world'));
      assert.equal(cid.toString(), zxv.cid.compute(utf8('hello world')).toString());
      assert.equal(Buffer.from(node.get(cid)).toString(), 'hello world');
      const rec = zxv.aipi.encodeFunction({ intent: 0, verify: 4, fuelMax: 7n, module: node.put(utf8(X.aipi.module_utf8)), entry: 'run' });
      const fn = node.publishFunction(rec);
      assert.ok(fn.equals(zxv.aipi.functionCid(rec)));
      assert.equal(hex(node.loadFunction(fn)), hex(rec));
      assert.ok(node.info().blocks >= 2);
    });

    test('coin: ladder faces, rungs, splits', () => {
      const K = X.coin;
      assert.equal(zxv.coins.face(K.rungs), B(K.top_face));
      for (const f of K.face) {
        assert.equal(st(() => zxv.coins.face(f.rung)), f.status);
        if (f.status === 'ZXV_OK') assert.equal(zxv.coins.face(f.rung), B(f.face));
      }
      for (const r of K.rung_of) {
        assert.equal(st(() => zxv.coins.rung(B(r.face))), r.status, r.face);
        if (r.status === 'ZXV_OK') assert.equal(zxv.coins.rung(B(r.face)), r.rung);
      }
      assert.throws(() => zxv.coins.rung(4n), DenominationError);
      for (const s of K.split) {
        assert.equal(st(() => zxv.coins.splitCount(B(s.amount))), s.status);
        if (s.status !== 'ZXV_OK') continue;
        assert.equal(zxv.coins.splitCount(B(s.amount)), B(s.count));
        if (s.rungs) assert.deepEqual(zxv.coins.split(B(s.amount)), s.rungs);
      }
    });

    test('tokens: faces, tiers, values, canonical forms, codes', () => {
      const K = X.coin;
      const tk = zxv.tokens;
      for (const f of K.token_face) {
        assert.equal(st(() => tk.face(f.tier)), f.status);
        if (f.status === 'ZXV_OK') assert.deepEqual(tk.face(f.tier), { face: B(f.face), isSuper: f.super });
      }
      for (const f of K.token_tier) {
        assert.equal(st(() => tk.tier(B(f.face))), f.status);
        if (f.status === 'ZXV_OK') assert.equal(tk.tier(B(f.face)), f.tier);
      }
      for (const v of K.token_value) {
        const h = sparse(zxv, v.counts);
        assert.equal(st(() => tk.value(h)), v.status);
        if (v.status === 'ZXV_OK') assert.deepEqual(tk.value(h), { value: B(v.value), pieces: B(v.pieces) });
      }
      for (const c of K.token_canonical) {
        assert.equal(st(() => tk.canonical(B(c.value), c.octane)), c.status);
        if (c.status !== 'ZXV_OK') continue;
        const h = tk.canonical(B(c.value), c.octane);
        assert.deepEqual(toSparse(h), c.counts);
        assert.equal(tk.value(h).pieces, B(c.pieces));
      }
      for (const c of K.token_is_canonical) assert.equal(tk.isCanonical(sparse(zxv, c.counts), c.octane), c.canonical);
      for (const c of K.token_compress) {
        assert.equal(st(() => tk.compress(sparse(zxv, c.counts), c.octane)), c.status);
        if (c.status === 'ZXV_OK') assert.deepEqual(toSparse(tk.compress(sparse(zxv, c.counts), c.octane)), c.out);
      }
      for (const c of K.token_fuse_split) {
        const h = sparse(zxv, c.counts);
        const f = () => (c.op === 'fuse' ? tk.fuse(h, c.octane, c.tier) : tk.split(h, c.tier));
        assert.equal(st(f), c.status, JSON.stringify(c));
        if (c.status === 'ZXV_OK') assert.deepEqual(toSparse(f()), c.out);
        assert.deepEqual(toSparse(h), c.counts, 'the input holding is never changed');
      }
      for (const c of K.token_code) {
        assert.equal(st(() => tk.codeBits(B(c.value))), c.status);
        if (c.status !== 'ZXV_OK') continue;
        assert.equal(tk.codeBits(B(c.value)), c.bits);
        const s = tk.encodeCodes([B(c.value)]);
        assert.deepEqual([hex(s.bytes), s.bits], [c.bytes, c.bits]);
        assert.deepEqual(tk.decodeCodes(s.bytes), [B(c.value)]);
      }
      const cs = K.token_code_stream;
      const s = tk.encodeCodes(cs.values.map(B));
      assert.deepEqual([hex(s.bytes), s.bits], [cs.bytes, cs.bits]);
      assert.deepEqual(tk.decodeCodes(s.bytes, cs.values.length), cs.values.map(B));
      assert.deepEqual(tk.decodeCodes(s.bytes, undefined, s.bits), cs.values.map(B));
      for (const r of K.token_code_get_reject) assert.equal(st(() => tk.readCode(unhex(r.bytes), r.bit)), r.status);
    });

    test('token wallet: needs MINT; mint, spend, transfer, octane', () => {
      using ctx = zxv.createContext();
      assert.throws(() => TokenWallet.create(ctx.rootCapability()), CapabilityDeniedError);
      const cap = ctx.rootCapability({ caps: Cap.ALL_1_5 });
      using a = TokenWallet.create(cap, { octane: 4, modelId: 7 });
      using b = TokenWallet.create(cap, { octane: 0 });
      a.mint(100n);
      assert.deepEqual(a.info(), { modelId: 7, octane: 4, value: 100n, pieces: zxv.tokens.value(a.counts()).pieces });
      assert.ok(zxv.tokens.isCanonical(a.counts(), 4));
      a.spend(30n);
      a.transferTo(b, 20n);
      assert.equal(a.info().value, 50n);
      assert.deepEqual(toSparse(b.counts()), [[0, '20']]);
      b.setOctane(9);
      assert.equal(b.info().value, 20n);
      assert.equal(st(() => a.spend(1000n)), 'ZXV_E_FUNDS');
    });

    test('fee: assure, carry, split, default policy', () => {
      const F = X.fee;
      for (const a of F.assure) assert.equal(zxv.fees.assure(B(a.gross)), B(a.fee));
      for (const c of F.carry) {
        let rem = B(c.rem_in);
        for (const s of c.steps) {
          assert.equal(st(() => zxv.fees.assureWithCarry(rem, B(s.gross))), s.status);
          const r = zxv.fees.assureWithCarry(rem, B(s.gross));
          assert.deepEqual([r.fee, r.remainder], [B(s.fee), B(s.rem)]);
          rem = r.remainder;
        }
      }
      for (const r of F.carry_reject) assert.equal(st(() => zxv.fees.assureWithCarry(B(r.rem_in), B(r.gross))), r.status);
      for (const s of F.split) assert.deepEqual(zxv.fees.split(B(s.fee)).parts, s.parts.map(B));
      const p = zxv.fees.policyDefault();
      const d = F.policy_default;
      assert.deepEqual(p.percent, d.pct);
      assert.equal(p.allowBelow, d.allow_below !== 0);
      assert.deepEqual(p.creditRates.map((r) => [r.num, r.den]), d.rate.map((r: string[]) => r.map(B)));
      assert.deepEqual([p.creditMultiplier.num, p.creditMultiplier.den], d.multiplier.map(B));
      assert.deepEqual([p.commonsCap.num, p.commonsCap.den], d.commons_cap.map(B));
    });

    test('vhub: the conformance scenario', () => {
      const S = X.vhub.scenario;
      for (const m of X.vhub.peg_msg) assert.equal(hex(VHub.pegMessage(zxv, m.slot, B(m.num), B(m.den), B(m.epoch))), m.msg);
      using ctx = zxv.createContext();
      const cap = ctx.rootCapability();
      const keys = S.keys.map((k: Any) => {
        const kp = MlDsa87.fromSeed(cap, unhex(k.seed));
        assert.equal(hex(zxv.hashes.sha3_256(kp.publicKey)), k.pk_sha3_256);
        return kp;
      });
      using hub = VHub.create(cap, { seed: unhex(S.hub_seed) });
      for (const c of S.currencies) {
        const add = () => hub.addCurrency(c.slot, { code: c.code, minorUnits: c.minor_units ?? 2, issuerPublicKey: keys[c.issuer_key ?? 0].publicKey,
          poolFunding: B(c.pool_funding ?? 0), vbillBacking: B(c.vbill_backing ?? 0) });
        assert.equal(st(add), c.status, c.code);
        if (c.status !== 'ZXV_OK') continue;
        const i = hub.currency(c.slot);
        assert.deepEqual([i.issuerAccount, i.poolAccount, i.vbillFloatAccount, i.peg], [c.issuer_account, c.pool_account, c.vfloat_account, undefined]);
      }
      for (const p of S.pegs) {
        const sig = MlDsa87.signDeterministic(cap, keys[p.signer].secretKey, hub.pegMessage(p.slot, B(p.num), B(p.den), B(p.epoch)));
        assert.equal(hex(zxv.hashes.sha3_256(sig)), p.sig_sha3_256);
        assert.equal(st(() => hub.setPeg(p.slot, B(p.num), B(p.den), B(p.epoch), sig)), p.status);
      }
      assert.deepEqual(hub.currency(0).peg, { num: 350n, den: 1n, epoch: 1n });
      for (const a of S.accounts) {
        assert.equal(hub.openAccount(a.slot, a.owner), a.account);
      }
      for (const a of S.accounts) if (a.issued) hub.issue(a.account, B(a.issued));
      for (const q of S.quotes) {
        assert.equal(st(() => hub.quote(q.from, q.to, B(q.want))), q.status);
        if (q.status === 'ZXV_OK') assert.deepEqual(hub.quote(q.from, q.to, B(q.want)), { amount: B(q.amount), vbills: B(q.vbills), received: B(q.received) });
      }
      assert.throws(() => hub.quote(0, 3, 1n), PegError);
      for (const s of S.swaps) {
        if (s.add_limit) hub.addLimit(s.add_limit.slot, B(s.add_limit.limit));
        let r: Any;
        const status = st(() => {
          r = hub.swap({ from: s.from, to: s.to, sender: s.sender, receiver: s.receiver, amount: B(s.amount), tick: B(s.tick) });
        });
        assert.equal(status, s.status, JSON.stringify(s));
        if (s.status === 'ZXV_OK') assert.deepEqual(r, { vbills: B(s.vbills), received: B(s.received) });
      }
      assert.equal(st(() => hub.check()), S.check);
      assert.equal(hub.check(), B(S.postings));
      for (const b of S.balances) {
        assert.deepEqual(hub.balance(b.account), { debit: B(b.debit), credit: B(b.credit), equity: B(b.equity) });
      }
      assert.equal(st(() => hub.balance(S.balances.length)), 'ZXV_E_NOT_FOUND');
      // a limit refusal maps to LimitExceededError
      assert.throws(() => hub.swap({ from: 0, to: 1, sender: 13, receiver: 14, amount: 105000n, tick: 99n }), LimitExceededError);
    });

    test('freight: headers, codec, erasure, sets, op streams, generators', () => {
      const F = X.freight;
      const fz = zxv.freight;
      for (const h of F.header_serialize) {
        const f = () => fz.serializeHeader({ k: h.k, flags: h.flags, payloadLength: h.payload_len, seq: h.seq, seqCount: h.seq_count,
          freightId: B(h.freight_id), expandLimit: B(h.expand_limit), payloadSha256: unhex(h.payload_sha256) });
        assert.equal(st(f), h.status);
        if (h.status !== 'ZXV_OK') continue;
        const hd = f();
        assert.equal(hex(hd.toBytes()), h.header);
        const p = fz.parseHeader(unhex(h.header));
        assert.deepEqual([p.k, p.flags, p.payloadLength, p.seq, p.seqCount, p.freightId, p.expandLimit],
          [h.k, h.flags, h.payload_len, h.seq, h.seq_count, B(h.freight_id), B(h.expand_limit)]);
        assert.ok(fz.isValidHeader(unhex(h.header)));
      }
      assert.ok(!fz.isValidHeader(new Uint8Array(64)));
      for (const c of F.codec) {
        const payload = unhex(c.payload);
        const hd = fz.header(payload, { freightId: B(c.freight_id), k: c.k });
        assert.equal(hex(hd.toBytes()), c.header);
        const packets = fz.encode(hd, payload);
        assert.equal(hex(packets), c.packets);
        for (const d of c.decode) {
          const sub = new Uint8Array(21 * d.rows.length);
          d.rows.forEach((r: number, i: number) => sub.set(packets.subarray(21 * r, 21 * r + 21), 21 * i));
          if (d.flip) sub[21 * d.flip.index_in_rows + d.flip.byte]! ^= Number.parseInt(d.flip.xor, 16);
          assert.equal(st(() => fz.decode(hd, sub)), d.status, JSON.stringify(d.rows));
          if (d.status === 'ZXV_OK') assert.equal(hex(fz.decode(hd, sub)), c.payload);
        }
      }
      assert.throws(() => {
        const c = F.codec[0];
        const hd = fz.parseHeader(unhex(c.header));
        fz.decode(hd, unhex(c.packets).subarray(0, 21));
      }, ErasureError);
      const rej = F.header_init_reject;
      assert.equal(st(() => fz.header(ramp(201), { freightId: 1n, k: 10 })), rej[0].status);
      assert.equal(st(() => fz.header(ramp(1), { freightId: 1n, k: 0 })), rej[1].status);
      assert.equal(st(() => fz.header(ramp(1), { freightId: 1n, k: 10, expandLimit: 5n })), rej[2].status);
      for (const s of F.set_count) {
        assert.equal(st(() => fz.setCount(B(s.len), s.k)), s.status);
        if (s.status === 'ZXV_OK') assert.equal(fz.setCount(B(s.len), s.k), s.count);
      }
      const sp = F.split;
      const data = ramp(sp.len);
      const opts = { freightId: B(sp.freight_id), k: sp.k };
      for (const m of sp.members) {
        assert.equal(st(() => fz.split(data, opts, m.seq)), m.status);
        if (m.status !== 'ZXV_OK') continue;
        const mem = fz.split(data, opts, m.seq);
        assert.equal(hex(mem.header.toBytes()), m.header);
        assert.equal(hex(zxv.hashes.sha3_256(mem.packets)), m.packets_sha3_256);
      }
      using join = fz.join(BigInt(sp.len));
      const members = [...fz.members(data, opts)].reverse();
      for (const m of members) join.add(m.header, m.packets);
      assert.deepEqual(join.status(), { received: members.length, count: members.length, complete: true, total: BigInt(sp.len) });
      assert.equal(hex(join.result()), hex(data));
      for (const o of F.ops_encode) {
        const d = utf8(o.utf8);
        assert.equal(hex(fz.encodeOps(d, { withSha256: o.with_sha })), o.ops);
        assert.equal(fz.opsBound(BigInt(d.length), { withSha256: o.with_sha }), B(o.bound));
        assert.equal(st(() => fz.expand(unhex(o.ops), { limit: 256n })), o.expand_status);
        assert.equal(Buffer.from(fz.expand(unhex(o.ops), { limit: 256n })).toString(), o.utf8);
      }
      const w0 = F.ops_writer[0];
      using w = fz.opsWriter(B(w0.declared_total));
      for (const op of w0.ops_list) {
        if (op.op === 'literal') w.literal(utf8(op.utf8));
        else if (op.op === 'copy') w.copy(B(op.distance), B(op.len));
        else w.seed(op.gen, unhex(op.params), B(op.len));
      }
      const ops = w.finish();
      assert.equal(hex(ops), w0.ops);
      assert.equal(hex(fz.expand(ops, { limit: 1024n })), w0.expanded);
      assert.deepEqual(fz.peek(ops), { total: B(w0.peek_total), hasSha256: w0.peek_has_sha });
      assert.equal(st(() => fz.expand(ops, { limit: B(w0.declared_total) - 1n })), w0.expand_limit_too_small);
      assert.equal(st(() => fz.expand(ops.subarray(0, ops.length - 1), { limit: 1024n })), w0.expand_truncated);
      const w1 = F.ops_writer[1];
      using wc = fz.opsWriter(B(w1.declared_total));
      const target = w1.ops_list[1];
      for (const op of w1.ops_list) {
        if (op.op === 'literal') wc.literal(utf8(op.utf8));
        else wc.cidref(unhex(op.cid), B(op.len));
      }
      const cops = wc.finish();
      assert.equal(hex(cops), w1.ops);
      assert.equal(st(() => fz.expand(cops, { limit: 1024n })), w1.expand_without_resolver);
      const seen: string[] = [];
      const out = fz.expand(cops, {
        limit: 1024n,
        resolve: (cid, len) => {
          seen.push(hex(cid));
          return hex(cid) === target.cid ? utf8(target.resolves_to_utf8).subarray(0, len) : undefined;
        },
      });
      assert.equal(Buffer.from(out).toString(), w1.expanded_with_resolver_utf8);
      assert.deepEqual(seen, [target.cid]);
      // a resolver exception is caught at the C boundary and re-thrown here, after the call
      const boom = new Error('resolver failed');
      assert.throws(() => fz.expand(cops, { limit: 1024n, resolve: () => { throw boom; } }), (e) => e === boom);
      assert.equal(st(() => fz.expand(cops, { limit: 1024n, resolve: () => undefined })), 'ZXV_E_NOT_FOUND');
      assert.throws(() => fz.expand(cops, { limit: 1024n, resolve: () => utf8('ab') }), RangeError);
      assert.equal(Buffer.from(fz.expand(cops, { limit: 1024n, resolve: (_c, n) => utf8('abc').subarray(0, n) })).toString(), '<abc>');
      for (const g of F.gen_run) {
        assert.equal(st(() => fz.generate(g.gen, unhex(g.params), g.len)), g.status);
        if (g.status === 'ZXV_OK') assert.equal(hex(fz.generate(g.gen, unhex(g.params), g.len)), g.out);
      }
      assert.equal(FreightFlag.OPSTREAM, 1);
    });

    test('web4: agents, cards, digests, manifest, consent, envelopes', () => {
      const W = X.web4;
      using ctx = zxv.createContext();
      const cap = ctx.rootCapability();
      const agents = new Map<string, W4Agent>();
      try {
        for (const a of W.agents) {
          const node = a.node === null ? undefined : agents.get(a.node);
          const ag = W4Agent.create(cap, { seed: fill(32, Number.parseInt(a.seed_byte, 16)), name: a.name, nowMs: B(a.now_ms),
            ...(node ? { node } : {}) });
          agents.set(a.name, ag);
          assert.deepEqual([hex(ag.agentId), hex(ag.peerId), hex(zxv.hashes.sha3_256(ag.publicKey))], [a.agent_id, a.peer_id, a.pk_sha3_256]);
          const card = ag.card();
          assert.equal(card.length, a.card_len);
          assert.equal(hex(zxv.hashes.sha3_256(card.subarray(0, card.length - W.sig_bytes))), a.card_signed_body_sha3_256);
          assert.equal(st(() => zxv.web4.verifyCard(card)), a.card_verify);
          const info = zxv.web4.verifyCard(card);
          assert.deepEqual([hex(info.agentId), hex(info.peerId), info.name, info.boundMs], [a.agent_id, a.peer_id, a.name, B(a.now_ms)]);
          const bad = card.slice();
          bad[bad.length - 1]! ^= 1;
          assert.equal(st(() => zxv.web4.verifyCard(bad)), a.card_verify_flipped_last_byte);
          assert.equal(String(ag), 'W4Agent(live)');
          assert.ok(!JSON.stringify(ag).includes('secret'));
        }
        const alice = agents.get('alice')!;
        const bob = agents.get('bob')!;
        const human = agents.get('human')!;
        // directory
        using dir = zxv.web4.directory(4);
        for (const ag of agents.values()) dir.add(ag.card());
        assert.equal(dir.size, 4);
        assert.equal(hex(dir.find(alice.agentId)!), hex(alice.card()));
        assert.equal(dir.find(new Uint8Array(32)), undefined);
        // action digests
        const AD = W.action_digest;
        for (const c of AD.cases) {
          const p = c.payload_utf8 !== undefined ? utf8(c.payload_utf8) : ramp(c.payload_ramp_len);
          const f = () => zxv.web4.actionDigest(c.tool, unhex(AD.from), unhex(AD.to), B(c.req_id), p);
          assert.equal(st(f), c.status, c.tool);
          if (c.status === 'ZXV_OK') assert.equal(hex(f()), c.digest);
        }
        // manifest
        const M = W.manifest;
        using m = zxv.web4.manifest(M.version, B(M.issued_ms), B(M.expires_ms));
        for (const t of M.tools) {
          assert.equal(st(() => m.addTool({ name: t.name, priceVfv: B(t.price_vfv), ratePerMin: t.rate_per_min, burst: t.burst, flags: t.flags,
            maxInput: t.max_input })), t.status);
        }
        assert.equal(st(() => m.addTool({ name: 'pay', priceVfv: 1n, ratePerMin: 1, burst: 1, flags: 0, maxInput: 0 })), M.add_duplicate);
        assert.equal(st(() => m.encode()), M.encode_unsigned);
        assert.equal(m.info().signed, false);
        m.sign(alice);
        const enc = m.encode();
        assert.equal(enc.length, M.encoding_len);
        assert.equal(hex(enc.subarray(0, enc.length - W.sig_bytes)), M.encoding_signed_body);
        assert.equal(m.toJson(), M.json);
        assert.equal(st(() => zxv.web4.decodeManifest(enc).close()), M.decode);
        using d = zxv.web4.decodeManifest(enc);
        const pk = (n: string) => agents.get(n)!.publicKey;
        for (const v of M.verify) assert.equal(st(() => d.verify(pk(v.key), B(v.now_ms))), v.status);
        assert.throws(() => d.verify(pk('bob'), B(M.issued_ms)), VerificationFailedError);
        const di = d.info();
        assert.deepEqual([hex(di.agentId), di.version, di.issuedMs, di.expiresMs, di.toolCount, di.signed],
          [W.agents[0].agent_id, M.version, B(M.issued_ms), B(M.expires_ms), M.tools.length, true]);
        assert.deepEqual(d.tools().map((t) => [t.name, t.priceVfv, t.ratePerMin, t.burst, t.flags, t.maxInput]),
          M.tools.map((t: Any) => [t.name, B(t.price_vfv), t.rate_per_min, t.burst, t.flags, t.max_input]));
        assert.deepEqual([d.find('profile.read'), d.find('nope')], [1, undefined]);
        // consent
        const CS = W.consent;
        const action = unhex(AD.cases[1].digest);
        const req = { agentId: alice.agentId, action, scope: CS.scope, maxVfv: B(CS.max_vfv), issuedMs: B(CS.issued_ms),
          expiresMs: B(CS.expires_ms), nonce: unhex(CS.nonce) };
        assert.throws(() => human.issueConsent(req, cap.attenuate(Cap.CRYPTO_SIGN)), CapabilityDeniedError);
        const tok = human.issueConsent(req);
        assert.equal(tok.length, CS.token_len);
        assert.equal(hex(tok.subarray(0, tok.length - W.sig_bytes)), CS.token_signed_body);
        const ti = zxv.web4.consentInfo(tok);
        assert.deepEqual([hex(ti.humanId), hex(ti.agentId), hex(ti.action), ti.scope, ti.maxVfv, hex(ti.nonce)],
          [hex(human.agentId), hex(alice.agentId), AD.cases[1].digest, CS.scope, B(CS.max_vfv), CS.nonce]);
        const nonce2 = unhex(CS.nonce);
        nonce2[0]! ^= 0xff;
        const tok2 = human.issueConsent({ ...req, nonce: nonce2 });
        using log = zxv.web4.consentLog(16);
        for (const c of CS.check) {
          const t = c.what.startsWith('second') ? tok2 : tok;
          if (t === tok2) log.revoke(tok2, B(c.now_ms));
          assert.equal(st(() => log.check(t, human.publicKey, agents.get(c.agent)!.agentId, action, c.need, B(c.amount), B(c.now_ms))),
            c.status, c.what);
        }
        assert.throws(() => log.check(tok, human.publicKey, alice.agentId, action, W4ConsentScope.MONEY, 1n, B(CS.issued_ms) + 5n),
          ConsentDeniedError);
        assert.equal(log.prune(B(CS.expires_ms)), 0);
        // envelopes
        const [e0, e1] = W.envelopes;
        const env = alice.seal({ kind: e0.kind, to: bob.agentId, reqId: B(e0.req_id), chunk: e0.chunk, tool: e0.tool, flags: e0.flags,
          payload: utf8(e0.payload_utf8), nowMs: B(e0.now_ms) });
        assert.equal(env.length, e0.env_len);
        assert.equal(hex(env.subarray(0, env.length - W.sig_bytes)), e0.env_signed_body);
        assert.equal(st(() => zxv.web4.verifyEnvelope(env, alice.publicKey)), e0.verify_sender);
        assert.equal(st(() => zxv.web4.verifyEnvelope(env, bob.publicKey)), e0.verify_other);
        const de = zxv.web4.decodeEnvelope(env);
        assert.deepEqual([de.kind, de.flags, hex(de.from), hex(de.to), de.seq, de.tsMs, de.reqId, de.chunk, de.tool, de.payloadLength],
          [W4MessageKind.REQUEST, 0, hex(alice.agentId), hex(bob.agentId), 1n, B(e0.now_ms), 7n, 0, 'pay', 10]);
        assert.equal(Buffer.from(zxv.web4.envelopePayload(env)).toString(), e0.payload_utf8);
        assert.equal(zxv.web4.envelopeConsent(env), undefined);
        const rsp = bob.reply(env, { kind: e1.kind, chunk: e1.chunk, flags: e1.flags, payload: utf8(e1.payload_utf8), nowMs: B(e1.now_ms) });
        assert.equal(rsp.length, e1.env_len);
        assert.equal(hex(rsp.subarray(0, rsp.length - W.sig_bytes)), e1.env_signed_body);
        assert.equal(st(() => zxv.web4.verifyEnvelope(rsp, bob.publicKey)), e1.verify_sender);
        assert.equal(zxv.web4.decodeEnvelope(rsp).flags, W4MessageFlag.FINAL);
        // a consent token rides in an envelope
        const withTok = alice.seal({ kind: W4MessageKind.REQUEST, to: bob.agentId, reqId: 8n, tool: 'pay', consent: tok, nowMs: B(e0.now_ms) });
        assert.equal(hex(zxv.web4.envelopeConsent(withTok)!), hex(tok));
        assert.equal(zxv.web4.decodeEnvelope(withTok).flags & W4MessageFlag.CONSENT, W4MessageFlag.CONSENT);
      } finally {
        for (const a of [...agents.values()].reverse()) a.close();
      }
    });

    test('paradox: the conformance run on one book', () => {
      const P = X.paradox;
      const px = zxv.paradox;
      for (const s of P.signature) assert.equal(hex(px.signature(utf8(s.utf8))), s.sig);
      P.kind_names.forEach((n: string, k: number) => assert.equal(px.kindName(k), n));
      P.outcome_names.forEach((n: string, k: number) => assert.equal(px.outcomeName(k), n));
      P.node_state_names.forEach((n: string, k: number) => assert.equal(px.nodeStateName(k), n));
      assert.equal(px.kindName(99), 'INVALID');
      for (let t = 0; t < 64; t++) assert.equal(px.kindOfTag(t), P.kind_of_tag[t]);
      for (let k = 0; k < 5; k++) {
        for (let t = 0; t < 64; t++) {
          const want = P.action[k][t];
          if (want < 0) assert.equal(st(() => px.action(k, t)), 'ZXV_E_ARG');
          else assert.equal(px.action(k, t), want);
        }
      }
      P.outcome_action.forEach((a: number, o: number) => assert.equal(px.outcomeAction(o), a));

      using ctx = zxv.createContext();
      const cap = ctx.rootCapability({ caps: Cap.ALL_1_5 });
      using book = ParadoxBook.create(cap, { logCapacity: 64, registryCapacity: 16 });
      const heads = (h: Any) => {
        const names = ['ops', 'frontier', 'quarantine'];
        for (let w = 0; w < 3; w++) {
          assert.equal(book.logCount(w), h[names[w]!].count);
          assert.equal(hex(book.logHead(w)), h[names[w]!].head);
        }
      };
      heads(P.heads_fresh);
      const sigL = px.signature(utf8('loop:A->B->A'));
      for (const c of P.classify) {
        if (c.registry_add) {
          const r = c.registry_add;
          assert.equal(book.registryAdd(sigL, r.kind, { reason: r.reason, flags: r.flags, ownerOk: r.owner_ok }), r.index);
          continue;
        }
        const r = book.classify(c.tag, { ...(c.evidence ? { evidence: c.evidence } : {}), stale: c.stale, ...(c.sig ? { signature: sigL } : {}) });
        assert.deepEqual([r.kind, r.tag, r.registryIndex], [c.kind, c.out_tag, c.reg_index], JSON.stringify(c));
      }
      const sigM = px.signature(utf8('money:drain'));
      for (const r of P.registry) {
        assert.equal(st(() => book.registryAdd(sigM, r.kind, { flags: r.flags, ownerOk: r.owner_ok })), r.status);
      }
      assert.equal(book.registryFind(sigM), 1);
      assert.equal(book.registryFind(new Uint8Array(32)), undefined);
      assert.equal(book.registryAt(1).source, 0xffffffffffffffffn);
      heads(P.heads_after_classify);
      // constructive
      const C = P.constructive;
      using plain = ParadoxPolicy.create(cap, { lo: C.lo.map(B), hi: C.hi.map(B), initial: C.initial.map(B) });
      using money = ParadoxPolicy.create(cap, { lo: C.lo.map(B), hi: C.hi.map(B), initial: C.initial.map(B), flags: 1 });
      for (const s of C.steps) {
        const pol = s.policy === 'money' ? money : plain;
        const f = () => book.constructive(pol, { kind: s.kind, tag: s.tag }, s.synth ? s.synth.map(B) : null, s.tension);
        let out = -1;
        assert.equal(st(() => {
          out = f();
        }), s.status, JSON.stringify(s));
        if (s.status === 'ZXV_OK') assert.equal(out, s.outcome);
        const v = pol.values();
        assert.deepEqual([v.values, v.previous, v.armed, v.tensionAt], [s.values.map(B), s.prev.map(B), s.armed, s.tension_at]);
        if (s.proposal) assert.equal(hex(pol.proposal(s.synth.map(B))), s.proposal);
      }
      assert.deepEqual(money.bounds(), { lo: C.lo.map(B), hi: C.hi.map(B), flags: 1 });
      // destructive
      for (const d of P.destructive) {
        const state = utf8(d.state_utf8);
        const r = book.destructive({ kind: d.kind, tag: d.tag }, {
          steps: d.steps, stepLimit: d.step_limit, spent: d.spent, budget: d.budget, trace: d.trace.map(B), invariantOk: d.invariant_ok,
          state, checkpoint: utf8(d.checkpoint_utf8), pattern: utf8(d.pattern_utf8), submitter: d.submitter,
        });
        assert.equal(r.outcome, d.outcome);
        assert.equal(hex(state), d.state_after, 'the state is restored in place');
        if (!d.repair_proof) {
          assert.equal(r.proof, undefined);
          continue;
        }
        assert.equal(hex(r.proof!.bytes), d.repair_proof);
        assert.equal(st(() => px.verifyRepair(r.proof!)), d.repair_verify);
        const bad = r.proof!.bytes.slice();
        bad[0]! ^= 1;
        assert.equal(st(() => px.verifyRepair(bad)), d.repair_verify_flipped_byte0);
        assert.throws(() => px.verifyRepair(new RepairProof(bad)), VerificationFailedError);
        assert.deepEqual([r.proof!.steps, r.proof!.submitter, hex(r.proof!.signature)], [d.steps, d.submitter, hex(sigL)]);
      }
      // neutral
      const N = P.neutral;
      using hold = ParadoxHold.create(cap, { capacity: N.capacity, decisive: N.decisive });
      const [b0, b1] = N.branches.map(unhex);
      hold.add(b0).add(b1).evidence(b0, true).evidence(b1, true);
      for (const s of N.steps.slice(1)) {
        for (let i = 0; i < s.add_for_branch0; i++) hold.evidence(b0, true);
        const r = book.neutral(hold, { kind: s.kind, tag: s.tag }, s.owner_pick);
        assert.deepEqual([r.outcome, r.settled], [s.outcome, s.settled], JSON.stringify(s));
      }
      assert.deepEqual(hold.info(), { count: 2, settled: 0, decisive: 0 });
      assert.deepEqual(hold.branch(0), { id: b0, positive: 3, negative: 0 });
      // special, drafts, review
      const S = P.special;
      const sg = px.signature(utf8(S.sig_utf8));
      assert.equal(hex(sg), S.sig);
      const sr = book.special({ kind: S.kind, tag: S.tag }, sg, unhex(S.subject));
      assert.deepEqual(sr, { outcome: S.outcome, record: S.rec });
      const draft = book.draft(S.rec, S.draft.kind, { reason: S.draft.reason, flags: S.draft.flags });
      assert.equal(hex(draft), S.draft.bytes);
      const draft2 = book.draft(S.rec, S.draft_money.kind, { reason: S.draft_money.reason, flags: S.draft_money.flags });
      assert.equal(hex(draft2), S.draft_money.bytes);
      for (const rv of S.review) {
        const o = book.review(rv.draft === 'draft' ? draft : draft2, { ownerOk: rv.owner_ok, consensusOk: rv.consensus_ok });
        assert.equal(o, rv.outcome);
      }
      assert.equal(book.registryCount(), S.registry_count_after);
      // graphs
      for (const g of P.graphs) {
        using graph = ParadoxGraph.create(cap, g.step_limit);
        const id = (c: string) => fill(32, c.charCodeAt(0));
        for (const n of g.nodes) graph.addNode(id(n.id), { kind: n.kind, tag: n.tag }, n.mag, n.dir);
        for (const d of g.deps) graph.addDependency(id(d.id), id(d.dep));
        for (const p of g.passes) {
          let out = -1;
          let err: unknown;
          try {
            out = book.graphPass(graph);
          } catch (e) {
            err = e;
          }
          if (p.status === 'ZXV_OK') assert.equal(out, p.outcome);
          else assert.ok(err instanceof ParadoxStepLimitError && err.statusName === p.status);
          const info = graph.info();
          assert.deepEqual([info.passes, info.totalMagnitude, info.totalDirection, info.escalated, hex(info.chain)],
            [p.pass, p.total_mag, p.total_dir, p.escalated, p.chain]);
          for (const [n, s] of Object.entries(p.states)) assert.equal(graph.state(id(n)), s);
        }
        assert.equal(graph.nodeAt(0).id[0], g.nodes.map((n: Any) => n.id).sort()[0].charCodeAt(0));
      }
      heads(P.heads_final);
      const logs = P.logs;
      const names = ['ops', 'frontier', 'quarantine'];
      for (let w = 0; w < 3; w++) {
        const recs = logs[names[w]!];
        recs.forEach((r: string, i: number) => assert.equal(hex(book.logRecord(w, i)), r));
        assert.equal(st(() => book.verifyLog(w)), P.log_verify[w]);
      }
      assert.equal(book.logRecord(ParadoxLog.QUARANTINE, 0).length, 110);
      assert.equal(ParadoxOutcome.ESCALATE_TO_OWNER, 11);
    });

    test('paradox refusals: owner approval and capability', () => {
      using ctx = zxv.createContext();
      assert.throws(() => ParadoxBook.create(ctx.rootCapability(), { logCapacity: 4, registryCapacity: 4 }), CapabilityDeniedError);
      const cap = ctx.rootCapability({ caps: Cap.ALL_1_5 });
      using book = ParadoxBook.create(cap, { logCapacity: 4, registryCapacity: 4 });
      assert.throws(() => book.registryAdd(new Uint8Array(32).fill(1), 2, { flags: 1 }), OwnerApprovalRequiredError);
      assert.throws(() => book.registryAdd(new Uint8Array(32).fill(1), 2, {}, ctx.rootCapability()), CapabilityDeniedError);
      assert.throws(() => ParadoxHold.create(ctx.rootCapability(), { capacity: 2 }), CapabilityDeniedError);
    });

    test('handle lifetimes: use after release is refused', () => {
      const t = zxv.treaty.parseSutra(X.treaty.sutra[0].sutra);
      t.close();
      t.close();
      assert.throws(() => t.name, ZxvUsageError);
      const m = zxv.web4.manifest(1, 0n, 1n);
      m.close();
      assert.throws(() => m.info(), ZxvUsageError);
      const j = zxv.freight.join(10n);
      j.close();
      assert.throws(() => j.status(), ZxvUsageError);
      const ctx = zxv.createContext();
      const cap = ctx.rootCapability({ caps: Cap.ALL_1_5 });
      const agent = W4Agent.create(cap, { name: 'x', nowMs: 1n });
      const book = ParadoxBook.create(cap, { logCapacity: 4, registryCapacity: 4 });
      const wallet = TokenWallet.create(cap);
      agent.close();
      assert.throws(() => agent.card(), ZxvUsageError);
      assert.equal(String(agent), 'W4Agent(closed)');
      ctx.close();
      assert.ok(book.closed && wallet.closed, 'closing a context releases its handles first');
      assert.throws(() => book.logCount(0), ZxvUsageError);
      assert.throws(() => wallet.info(), ZxvUsageError);
    });

    test('the two-call buffer path grows to the exact size', () => {
      using ctx = zxv.createContext();
      const cap = ctx.rootCapability();
      using agent = W4Agent.create(cap, { seed: fill(32, 0x11), name: 'alice', nowMs: 1700000000000n });
      const b = zxv._core.b;
      const len = b.sizeSlot();
      assert.equal(b.call('zxv_w4_agent_card', agent._p(), null, 0, len), -2);
      const need = b.readSize(len);
      assert.equal(need, X.web4.agents[0].card_len);
      const small = new Uint8Array(need - 1);
      assert.equal(b.call('zxv_w4_agent_card', agent._p(), small, small.length, len), -2, 'one byte short');
      assert.ok(small.every((x) => x === 0), 'nothing written on ZXV_E_BUFFER');
      const card = agent.card();
      assert.equal(card.length, need);
      // large R4 results on both backends: a 1 MB unixfs block set, a manifest's JSON, a decoded freight
      assert.equal(zxv.cid.unixfs(ramp(1_000_000)).toString(), X.cid.unixfs[4].text_default);
      using m = zxv.web4.manifest(1, 0n, 10n);
      for (let i = 0; i < 16; i++) m.addTool({ name: `tool.${i}`, priceVfv: BigInt(i), ratePerMin: 1, burst: 1, flags: 0, maxInput: 0 });
      m.sign(agent);
      assert.ok(m.toJson().length > 1000 && JSON.parse(m.toJson()).tools.length === 16);
      const big = ramp(3360);
      const hd: FreightHeader = zxv.freight.header(big, { freightId: 1n, k: 168 });
      assert.equal(hex(zxv.freight.decode(hd, zxv.freight.encode(hd, big))), hex(big));
    });

    test('a poisoned context refuses every handle call', () => {
      const ctx = zxv.createContext();
      const cap: Capability = ctx.rootCapability({ caps: Cap.ALL_1_5 });
      const wallet = TokenWallet.create(cap);
      const agent = W4Agent.create(cap, { name: 'p', nowMs: 1n });
      const book = ParadoxBook.create(cap, { logCapacity: 4, registryCapacity: 4 });
      wallet.mint(5n);
      ctx.halt('test');
      assert.throws(() => wallet.info(), ZxvFatalError);
      assert.throws(() => wallet.mint(1n), ZxvFatalError);
      assert.throws(() => agent.card(), ZxvFatalError);
      assert.throws(() => book.logCount(0), ZxvFatalError);
      assert.throws(() => TokenWallet.create(cap), ZxvFatalError);
      // pure groups keep working
      assert.equal(zxv.fees.assure(1125n), 1n);
      ctx.close();
    });

    test('FEAT2 helpers and flag constants', () => {
      assert.equal(zxv.has('TREATY'), true);
      assert.equal(FreightFlag.OPSTREAM, 0x01);
      assert.equal(W4ConsentScope.PII, 0x02);
    });
  });
}
