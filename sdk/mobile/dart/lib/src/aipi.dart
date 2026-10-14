// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
import 'dart:ffi';
import 'dart:typed_data';

import 'package:ffi/ffi.dart';

import 'bindings15.dart';
import 'context.dart';
import 'crypto.dart';
import 'errors.dart';
import 'native.dart' as n;

/// Multicodec and multihash codes the AIPI and CID calls use.
abstract final class Multicodec {
  /// Raw bytes.
  static const int raw = 0x55;

  /// A dag-pb (UnixFS) node.
  static const int dagPb = 0x70;

  /// An AIPI function record (intent FN).
  static const int aipiFn = 0x3A1000;

  /// An AIPI function record, intent TENSOR_INFER.
  static const int aipiTensorInfer = 0x3A1001;

  /// An AIPI function record, intent SETTLE_LEDGER.
  static const int aipiSettleLedger = 0x3A1002;

  /// An AIPI function record, intent SWARM_ROUTE.
  static const int aipiSwarmRoute = 0x3A1003;

  /// An AIPI call record.
  static const int aipiCall = 0x3A10C0;

  /// The identity multihash.
  static const int hashIdentity = 0x00;

  /// The SHA2-256 multihash.
  static const int hashSha2_256 = 0x12;
}

/// The text base of a CID.
enum CidBase {
  /// The canonical base for the version: base58btc for v0, base32 lower for v1.
  defaultBase(0),

  /// base58btc ('z' prefix for v1).
  base58btc(1);

  const CidBase(this.code);
  final int code;
}

/// An IPFS content identifier (v0 or v1), checked canonical on the way in.
/// A value: equal when the bytes are equal. Pure.
final class Cid {
  Cid._(this._b, this.version, this.codec, this.multihash, this._d);

  final Uint8List _b;

  /// 0 or 1.
  final int version;

  /// The content multicodec (uint32).
  final int codec;

  /// The multihash code.
  final int multihash;
  final Uint8List _d;

  /// The longest binary CID.
  static const int maxBytes = 80;

  /// The longest CID text.
  static const int textMax = 139;

  /// The longest multihash digest.
  static const int digestMax = 64;

  /// A CID from its binary form. [CidException] if it is not canonical.
  factory Cid.of(List<int> bytes) => using((a) {
    final ver = a<Uint32>(), codec = a<Uint32>(), hash = a<Uint32>();
    final dig = a<Uint8>(digestMax);
    final dl = a<Size>()..value = digestMax;
    n.check(zxv15.cidInfo(n.bytesIn(a, bytes), bytes.length, ver, codec, hash, dig, dl));
    return Cid._(Uint8List.fromList(bytes), ver.value, codec.value, hash.value, n.bytesOut(dig, dl.value));
  });

  /// A CID from its text form (base58btc v0, or multibase v1).
  factory Cid.parse(String text) => using((a) {
    final t = n.cstr(a, text, 'text');
    return Cid.of(n.cid((c, l) => zxv15.cidFromText(t, c, l), null));
  });

  /// The CIDv1 of `data` under `codec`, SHA2-256.
  factory Cid.compute(int codec, List<int> data) {
    n.u32(codec, 'codec');
    return using((a) {
      final d = n.bytesIn(a, data);
      return Cid.of(n.cid((c, l) => zxv15.cidCompute(codec, d, data.length, c, l), null));
    });
  }

  /// [Cid.compute] with [Multicodec.raw].
  factory Cid.raw(List<int> data) => Cid.compute(Multicodec.raw, data);

  /// The CIDv0 `ipfs add` gives `data` as one UnixFS file (one block, up to 256 KiB).
  factory Cid.unixfs(List<int> data) => using((a) {
    final d = n.bytesIn(a, data);
    return Cid.of(n.cid((c, l) => zxv15.cidUnixfs(d, data.length, c, l), null));
  });

  /// A copy of the binary CID.
  Uint8List get bytes => Uint8List.fromList(_b);

  /// The binary length.
  int get length => _b.length;

  /// A copy of the multihash digest.
  Uint8List get digest => Uint8List.fromList(_d);

  /// The text form in `base`.
  String toText([CidBase base = CidBase.defaultBase]) => using((a) {
    final b = n.bytesIn(a, _b);
    return n.text((buf, cap, len) => zxv15.cidToText(b, _b.length, base.code, buf, cap, len), null);
  });

  /// The same content as a CIDv1 (this one if it already is).
  Cid toV1() => version == 1
      ? this
      : using((a) {
          final b = n.bytesIn(a, _b);
          return Cid.of(n.cid((c, l) => zxv15.cidToV1(b, _b.length, c, l), null));
        });

  /// True if `data` hashes to this CID; other errors still throw.
  bool matches(List<int> data) {
    final st = _verify(data);
    if (st == n.eCid) return false;
    n.check(st);
    return true;
  }

  /// Returns normally if `data` hashes to this CID; [CidException] if not.
  void verify(List<int> data) => n.check(_verify(data));

  int _verify(List<int> data) =>
      using((a) => zxv15.cidVerify(n.bytesIn(a, _b), _b.length, n.bytesIn(a, data), data.length));

  /// The 32-byte DHT provider key (SHA-256 of the multihash).
  Uint8List providerKey() => using((a) {
    final o = a<Uint8>(32);
    n.check(zxv15.aipiProviderKey(n.bytesIn(a, _b), _b.length, o));
    return n.bytesOut(o, 32);
  });

  /// Lower-case hex of the binary CID.
  String toHex() => n.hex(_b);

  @override
  bool operator ==(Object other) => other is Cid && n.sameBytes(_b, other._b);

  @override
  int get hashCode => Object.hashAll(_b);

  @override
  String toString() => toText();
}

/// What a function does (`ZXV_AIPI_INTENT_*`); it picks the record's multicodec.
enum Intent {
  fn(0),
  tensorInfer(1),
  settleLedger(2),
  swarmRoute(3);

  const Intent(this.code);
  final int code;

  /// The multicodec of a function record with this intent.
  int get codec => Multicodec.aipiFn + code;

  static Intent fromCode(int code) => values.firstWhere((v) => v.code == code, orElse: () => n.badCode('Intent', code));
}

/// How a result is verified (`ZXV_AIPI_VERIFY_*`).
enum VerifyMode {
  checker(1),
  cert(2),
  quorum(3),
  local(4);

  const VerifyMode(this.code);
  final int code;

  static VerifyMode fromCode(int code) =>
      values.firstWhere((v) => v.code == code, orElse: () => n.badCode('VerifyMode', code));
}

/// Whether inputs may leave the device (`ZXV_AIPI_PRIVACY_*`).
enum Privacy {
  public(0),
  local(1);

  const Privacy(this.code);
  final int code;

  static Privacy fromCode(int code) =>
      values.firstWhere((v) => v.code == code, orElse: () => n.badCode('Privacy', code));
}

/// Whether a function is deterministic.
enum Determinism {
  nondeterministic(0),
  deterministic(1);

  const Determinism(this.code);
  final int code;

  static Determinism fromCode(int code) =>
      values.firstWhere((v) => v.code == code, orElse: () => n.badCode('Determinism', code));
}

/// Where [AipiNode.plan] would run a call (`ZXV_AIPI_ROUTE_*`).
enum Route {
  cache(1),
  local(2),
  fetchLocal(3),
  mesh(4),
  unavailable(5);

  const Route(this.code);
  final int code;

  static Route fromCode(int code) => values.firstWhere((v) => v.code == code, orElse: () => n.badCode('Route', code));
}

Uint8List _slice(List<int> b, int off, int len) => Uint8List.fromList(b.sublist(off, off + len));

/// A content-addressed function record (at most 373 bytes): what to run
/// (module CID, entry export), how to verify it, its privacy and fuel cap.
/// A value, built with [FunctionRecord.encode] or [FunctionRecord.decode]. Pure.
final class FunctionRecord {
  FunctionRecord._(
    this._rec,
    this.intent,
    this.verify,
    this.privacy,
    this.determinism,
    this.quorumN,
    this.quorumK,
    this.fuelMax,
    this.module,
    this.contract,
    this.entry,
    this.checker,
    this.certifier,
    this.cid,
  );

  final Uint8List _rec;
  final Intent intent;
  final VerifyMode verify;
  final Privacy privacy;
  final Determinism determinism;
  final int quorumN, quorumK;

  /// The fuel cap (uint64 bits).
  final int fuelMax;
  final Cid module;

  /// The contract CID, or null.
  final Cid? contract;
  final String entry, checker, certifier;

  /// The record's own CID.
  final Cid cid;

  /// The longest record.
  static const int maxBytes = 373;

  /// The longest export name.
  static const int nameMax = 63;

  /// The largest quorum.
  static const int quorumMax = 8;

  /// Encodes a record. Defaults: FN, LOCAL verification, PUBLIC,
  /// DETERMINISTIC. CHECKER needs `checker`; CERT needs `checker` and
  /// `certifier`; QUORUM needs `quorumN` and `quorumK`.
  factory FunctionRecord.encode({
    required Cid module,
    required String entry,
    Intent intent = Intent.fn,
    VerifyMode verify = VerifyMode.local,
    Privacy privacy = Privacy.public,
    Determinism determinism = Determinism.deterministic,
    int quorumN = 0,
    int quorumK = 0,
    int fuelMax = 0,
    Cid? contract,
    String? checker,
    String? certifier,
  }) {
    n.u32(quorumN, 'quorumN');
    n.u32(quorumK, 'quorumK');
    return using((a) {
      final e = n.cstr(a, entry, 'entry');
      final ch = checker == null ? nullptr : n.cstr(a, checker, 'checker');
      final ce = certifier == null ? nullptr : n.cstr(a, certifier, 'certifier');
      final m = n.bytesIn(a, module._b);
      final c = contract == null ? nullptr : n.bytesIn(a, contract._b);
      final cl = contract?.length ?? 0;
      final rec = n.bytes(
        (b, cap, len) => zxv15.aipiFnEncode(
          intent.code,
          verify.code,
          privacy.code,
          determinism.code,
          quorumN,
          quorumK,
          fuelMax,
          m,
          module.length,
          c,
          cl,
          e,
          ch,
          ce,
          b,
          cap,
          len,
        ),
        null,
      );
      return FunctionRecord.decode(rec);
    });
  }

  /// Decodes and checks a record.
  factory FunctionRecord.decode(List<int> record) => using((a) {
    final rec = Uint8List.fromList(record);
    final r = n.bytesIn(a, rec);
    final len = rec.length;
    final o = a<Uint32>(6);
    final fuel = a<Uint64>();
    n.check(zxv15.aipiFnDecode(r, len, o, o + 1, o + 2, o + 3, o + 4, o + 5, fuel));
    final module = Cid.of(n.cid((c, l) => zxv15.aipiFnGetCid(r, len, 0, c, l), null));
    final cb = a<Uint8>(Cid.maxBytes);
    final cl = a<Size>();
    final st = zxv15.aipiFnGetCid(r, len, 1, cb, cl);
    Cid? contract;
    if (st != n.eNotFound) {
      n.check(st);
      contract = Cid.of(n.bytesOut(cb, cl.value));
    }
    String name(int field) => n.text((b, cap, l) => zxv15.aipiFnGetName(r, len, field, b, cap, l), null);
    final cid = Cid.of(n.cid((c, l) => zxv15.aipiFnCid(r, len, c, l), null));
    return FunctionRecord._(
      rec,
      Intent.fromCode(o[0]),
      VerifyMode.fromCode(o[1]),
      Privacy.fromCode(o[2]),
      Determinism.fromCode(o[3]),
      o[4],
      o[5],
      fuel.value,
      module,
      contract,
      name(0),
      name(1),
      name(2),
      cid,
    );
  });

  /// A copy of the encoded record.
  Uint8List get bytes => Uint8List.fromList(_rec);

  @override
  bool operator ==(Object other) => other is FunctionRecord && n.sameBytes(_rec, other._rec);

  @override
  int get hashCode => Object.hashAll(_rec);

  @override
  String toString() => 'FunctionRecord[${cid.toText(CidBase.base58btc)} ${intent.name} $entry]';
}

/// A call record: a function CID and its input (at most 1024 bytes). A value. Pure.
final class CallRecord {
  CallRecord._(this._rec, this.function, this._inp, this.cid);

  final Uint8List _rec;
  final Cid function;
  final Uint8List _inp;
  final Cid cid;

  /// The largest input.
  static const int inputMax = 1024;

  /// The longest record.
  static const int maxBytes = 1112;

  /// Encodes a call of `function` with `input`.
  factory CallRecord.encode(Cid function, List<int> input) => using((a) {
    final f = n.bytesIn(a, function._b);
    final i = n.bytesIn(a, input);
    return CallRecord.decode(
      n.bytes((b, c, l) => zxv15.aipiCallEncode(f, function.length, i, input.length, b, c, l), null),
    );
  });

  /// The CID of that call record, without building it.
  static Cid cidOf(Cid function, List<int> input) => using((a) {
    final f = n.bytesIn(a, function._b);
    final i = n.bytesIn(a, input);
    return Cid.of(n.cid((c, l) => zxv15.aipiCallCid(f, function.length, i, input.length, c, l), null));
  });

  /// Decodes and checks a record.
  factory CallRecord.decode(List<int> record) {
    final rec = Uint8List.fromList(record);
    final o = using((a) {
      final o = a<Size>(4);
      n.check(zxv15.aipiCallDecode(n.bytesIn(a, rec), rec.length, o, o + 1, o + 2, o + 3));
      return [o[0], o[1], o[2], o[3]];
    });
    final fn = Cid.of(_slice(rec, o[0], o[1]));
    final input = _slice(rec, o[2], o[3]);
    return CallRecord._(rec, fn, input, cidOf(fn, input));
  }

  /// A copy of the encoded record.
  Uint8List get bytes => Uint8List.fromList(_rec);

  /// A copy of the input.
  Uint8List get input => Uint8List.fromList(_inp);

  @override
  bool operator ==(Object other) => other is CallRecord && n.sameBytes(_rec, other._rec);

  @override
  int get hashCode => Object.hashAll(_rec);

  @override
  String toString() => 'CallRecord[$function, ${_inp.length} input bytes]';
}

/// A claim record: a call CID, the 32-byte executor id, the output and a certificate. A value. Pure.
final class ClaimRecord {
  ClaimRecord._(this._rec, this.call, this._ex, this._out, this._cert);

  final Uint8List _rec;
  final Cid call;
  final Uint8List _ex, _out, _cert;

  /// The largest output.
  static const int outputMax = 1024;

  /// The largest certificate.
  static const int certMax = 1024;

  /// The longest record.
  static const int maxBytes = 2170;

  /// Encodes a claim.
  factory ClaimRecord.encode(Cid call, List<int> executor, List<int> output, List<int> certificate) {
    n.requireLength(executor, 32, 'executor');
    return using((a) {
      final c = n.bytesIn(a, call._b);
      final ex = n.bytesIn(a, executor);
      final out = n.bytesIn(a, output);
      final cert = n.bytesIn(a, certificate);
      return ClaimRecord.decode(
        n.bytes(
          (b, cap, l) =>
              zxv15.aipiClaimEncode(c, call.length, ex, out, output.length, cert, certificate.length, b, cap, l),
          null,
        ),
      );
    });
  }

  /// Decodes and checks a record.
  factory ClaimRecord.decode(List<int> record) {
    final rec = Uint8List.fromList(record);
    final o = using((a) {
      final o = a<Size>(7);
      n.check(zxv15.aipiClaimDecode(n.bytesIn(a, rec), rec.length, o, o + 1, o + 2, o + 3, o + 4, o + 5, o + 6));
      return [for (var i = 0; i < 7; i++) o[i]];
    });
    return ClaimRecord._(
      rec,
      Cid.of(_slice(rec, o[0], o[1])),
      _slice(rec, o[2], 32),
      _slice(rec, o[3], o[4]),
      _slice(rec, o[5], o[6]),
    );
  }

  /// A copy of the encoded record.
  Uint8List get bytes => Uint8List.fromList(_rec);

  /// A copy of the executor id.
  Uint8List get executor => Uint8List.fromList(_ex);

  /// A copy of the output.
  Uint8List get output => Uint8List.fromList(_out);

  /// A copy of the certificate.
  Uint8List get certificate => Uint8List.fromList(_cert);

  @override
  bool operator ==(Object other) => other is ClaimRecord && n.sameBytes(_rec, other._rec);

  @override
  int get hashCode => Object.hashAll(_rec);

  @override
  String toString() => 'ClaimRecord[$call, ${_out.length} output bytes]';
}

/// Block count, store bytes used, verified gets and forbidden (private) refusals.
typedef AipiInfo = ({int blocks, int storeUsed, int getsVerified, int forbidden});

/// An AIPI node: a bounded content-addressed block store that checks every
/// block against its CID on the way out, publishes function records, and
/// plans where a call would run. Putting and publishing need `AIPI` and pass
/// [capability]; reads need none but fail closed once the context halted.
///
/// The node belongs to the context (closed with it, ABI R6). Its optional
/// 32-byte node key is a [SecretKey], so it never sits on the Dart heap.
final class AipiNode {
  AipiNode._(this.capability, Pointer<Void> p) {
    _h = n.NativeHandle('AIPI node', p, zxv15.aipiDestroy, capability.context.state, capability.context.handle);
    n.handleFinalizer.attach(this, _h, detach: this);
  }

  /// The capability puts and publishes pass.
  final Capability capability;
  late final n.NativeHandle _h;

  n.ContextState get _st => capability.context.state;

  /// The smallest store.
  static const int storeMin = 4096;

  /// The largest store.
  static const int storeMax = 268435456;

  /// The most blocks.
  static const int blocksMax = 65536;

  /// A node with `storeBytes` of block store and at most `maxBlocks` blocks.
  /// `nodeKey` (32 bytes, or null) keys private blocks. Needs `AIPI`.
  factory AipiNode.create(Capability cap, int storeBytes, int maxBlocks, {SecretKey? nodeKey}) {
    if (storeBytes < 0) throw n.usage(n.eArg, 'storeBytes is negative');
    n.u32(maxBlocks, 'maxBlocks');
    if (nodeKey != null && nodeKey.size != 32) throw n.usage(n.eArg, 'a node key is 32 bytes, got ${nodeKey.size}');
    return using((a) {
      final o = a<Pointer<Void>>();
      n.check(
        zxv15.aipiCreate(cap.handle.ptr, storeBytes, maxBlocks, nodeKey == null ? nullptr : nodeKey.pointer, o),
        cap.context.state,
      );
      return AipiNode._(cap, o.value);
    });
  }

  /// Stores a public block; returns its CID.
  Cid put(List<int> data) => _put(data, false);

  /// Stores a private block (never served to the mesh); returns its CID.
  Cid putPrivate(List<int> data) => _put(data, true);

  Cid _put(List<int> data, bool priv) => using((a) {
    final c = capability.handle.ptr;
    final h = _h.ptr;
    final d = n.bytesIn(a, data);
    return Cid.of(n.cid((b, l) => zxv15.aipiPut(c, h, d, data.length, priv ? 1 : 0, b, l), _st));
  });

  /// The block for `cid`, checked against it.
  Uint8List get(Cid cid) => using((a) {
    final h = _h.ptr;
    final c = n.bytesIn(a, cid._b);
    return n.bytes((b, cap, l) => zxv15.aipiGet(h, c, cid.length, b, cap, l), _st);
  });

  /// Publishes a function record; returns its CID.
  Cid publish(FunctionRecord record) => using((a) {
    final c = capability.handle.ptr;
    final h = _h.ptr;
    final r = n.bytesIn(a, record._rec);
    return Cid.of(n.cid((b, l) => zxv15.aipiPublishFn(c, h, r, record._rec.length, b, l), _st));
  });

  /// The published function record for `fn`.
  FunctionRecord loadFunction(Cid fn) => using((a) {
    final h = _h.ptr;
    final c = n.bytesIn(a, fn._b);
    return FunctionRecord.decode(n.bytes((b, cap, l) => zxv15.aipiLoadFn(h, c, fn.length, b, cap, l), _st));
  });

  /// Where a call of `fn` with `input` (or none) would run.
  Route plan(Cid fn, List<int>? input, {bool inputPrivate = false}) => using((a) {
    final o = a<Int32>();
    n.check(
      zxv15.aipiPlan(
        _h.ptr,
        n.bytesIn(a, fn._b),
        fn.length,
        n.optBytes(a, input),
        input?.length ?? 0,
        inputPrivate ? 1 : 0,
        o,
      ),
      _st,
    );
    return Route.fromCode(o.value);
  });

  /// Counters.
  AipiInfo info() => using((a) {
    final blocks = a<Uint32>(), used = a<Uint64>(), gets = a<Uint32>(), forb = a<Uint32>();
    n.check(zxv15.aipiInfo(_h.ptr, blocks, used, gets, forb), _st);
    return (blocks: blocks.value, storeUsed: used.value, getsVerified: gets.value, forbidden: forb.value);
  });

  bool get isClosed => _h.isClosed;

  /// Frees the node. Idempotent.
  void close() {
    n.handleFinalizer.detach(this);
    _h.close();
  }
}
