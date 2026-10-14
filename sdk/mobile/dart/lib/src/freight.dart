// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
import 'dart:ffi';
import 'dart:typed_data';

import 'package:ffi/ffi.dart';

import 'bindings15.dart';
import 'errors.dart';
import 'native.dart' as n;

/// A 64-byte freight header: version, k (data rows), flags, payload length,
/// the member's place in a set, the freight id, the op-stream expansion
/// limit and the payload's SHA-256. A value. Pure.
final class FreightHeader {
  FreightHeader._(
    this._b,
    this.version,
    this.k,
    this.flags,
    this.payloadLength,
    this.seq,
    this.seqCount,
    this.freightId,
    this.expandLimit,
    this._sha,
  );

  final Uint8List _b;
  final int version, k, flags, payloadLength, seq, seqCount;

  /// uint64 bits.
  final int freightId;

  /// uint64 bits.
  final int expandLimit;
  final Uint8List _sha;

  /// The header size.
  static const int bytes = 64;

  /// Parses and checks a header.
  factory FreightHeader.parse(List<int> header) {
    n.requireLength(header, bytes, 'header');
    return using((a) {
      final u8 = a<Uint8>(3), u16 = a<Uint16>(3), u64 = a<Uint64>(2), sha = a<Uint8>(32);
      n.check(
        zxv15.freightHeaderParse(n.bytesIn(a, header), u8, u8 + 1, u8 + 2, u16, u16 + 1, u16 + 2, u64, u64 + 1, sha),
      );
      return FreightHeader._(
        Uint8List.fromList(header),
        u8[0],
        u8[1],
        u8[2],
        u16[0],
        u16[1],
        u16[2],
        u64[0],
        u64[1],
        n.bytesOut(sha, 32),
      );
    });
  }

  /// True if `header` is a well-formed header.
  static bool isValid(List<int>? header) =>
      header != null && header.length == bytes && using((a) => zxv15.freightHeaderCheck(n.bytesIn(a, header))) == 0;

  /// The header for a single freight carrying `payload`.
  factory FreightHeader.forPayload(int freightId, int k, int flags, int expandLimit, List<int> payload) {
    n.u8(k, 'k');
    n.u8(flags, 'flags');
    return using((a) {
      final o = a<Uint8>(bytes);
      n.check(zxv15.freightHeaderInit(freightId, k, flags, expandLimit, n.bytesIn(a, payload), payload.length, o));
      return FreightHeader.parse(o.asTypedList(bytes));
    });
  }

  /// A header from its fields.
  factory FreightHeader.of({
    required int k,
    required int flags,
    required int payloadLength,
    required int seq,
    required int seqCount,
    required int freightId,
    required int expandLimit,
    required List<int> payloadSha256,
  }) {
    n.u8(k, 'k');
    n.u8(flags, 'flags');
    n.u16(payloadLength, 'payloadLength');
    n.u16(seq, 'seq');
    n.u16(seqCount, 'seqCount');
    n.requireLength(payloadSha256, 32, 'payloadSha256');
    return using((a) {
      final o = a<Uint8>(bytes);
      n.check(
        zxv15.freightHeaderSerialize(
          k,
          flags,
          payloadLength,
          seq,
          seqCount,
          freightId,
          expandLimit,
          n.bytesIn(a, payloadSha256),
          o,
        ),
      );
      return FreightHeader.parse(o.asTypedList(bytes));
    });
  }

  /// A copy of the 64 bytes.
  Uint8List toBytes() => Uint8List.fromList(_b);

  /// True if the payload is an op stream.
  bool get isOpStream => flags & Freight.fOpStream != 0;

  /// A copy of the payload's SHA-256.
  Uint8List get payloadSha256 => Uint8List.fromList(_sha);

  @override
  bool operator ==(Object other) => other is FreightHeader && n.sameBytes(_b, other._b);

  @override
  int get hashCode => Object.hashAll(_b);

  @override
  String toString() => 'FreightHeader[${n.hex(_b)}]';
}

/// One member of a set: its header and its 3528 bytes of packets.
final class FreightMember {
  FreightMember(this.header, List<int> packets) : _p = Uint8List.fromList(packets);
  final FreightHeader header;
  final Uint8List _p;

  /// A copy of the packets.
  Uint8List get packets => Uint8List.fromList(_p);

  /// The packet of `row`.
  Uint8List packet(int row) => Freight.packet(_p, row);
}

/// Erasure-coded freight: a payload of up to 3360 bytes in 168 rows of
/// 21-byte packets, any `k` distinct rows of which rebuild it; and sets of
/// freights for larger data. Pure.
abstract final class Freight {
  static const int packetBytes = 21;
  static const int rowPayload = 20;
  static const int rows = 168;
  static const int bytes = 3528;
  static const int maxPayload = 3360;
  static const int maxSet = 16384;
  static const int setMaxBytes = 55050240;

  /// The header flag for an op-stream payload.
  static const int fOpStream = 0x01;

  /// The 168 packets of `payload` under `header`, concatenated.
  static Uint8List encode(FreightHeader header, List<int> payload) => using((a) {
    final o = a<Uint8>(bytes);
    n.check(zxv15.freightEncode(n.bytesIn(a, header._b), n.bytesIn(a, payload), payload.length, o));
    return n.bytesOut(o, bytes);
  });

  /// Rebuilds the payload from any `k` distinct packets (concatenated); [ErasureException] if too few.
  static Uint8List decode(FreightHeader header, List<int> packets) => using((a) {
    final h = n.bytesIn(a, header._b);
    final p = n.bytesIn(a, packets);
    return n.bytes((b, c, l) => zxv15.freightDecode(h, p, packets.length, b, c, l), null);
  });

  /// Concatenates packets.
  static Uint8List packets(List<List<int>> packets) {
    final out = Uint8List(packets.length * packetBytes);
    for (var i = 0; i < packets.length; i++) {
      n.requireLength(packets[i], packetBytes, 'packet');
      out.setAll(i * packetBytes, packets[i]);
    }
    return out;
  }

  /// The packet of `row` in an encoded freight.
  static Uint8List packet(List<int> encoded, int row) {
    n.requireLength(encoded, bytes, 'encoded');
    if (row < 0 || row >= rows) throw n.usage(n.eRange, 'row $row');
    return Uint8List.fromList(encoded.sublist(row * packetBytes, (row + 1) * packetBytes));
  }

  /// How many freights a set for `length` bytes at `k` needs.
  static int setCount(int length, int k) {
    n.u8(k, 'k');
    return using((a) {
      final o = a<Uint32>();
      n.check(zxv15.freightSetCount(length, k, o));
      return o.value;
    });
  }

  /// Member `seq` of the set for `data`.
  static FreightMember split(int freightId, int k, int flags, int expandLimit, List<int> data, int seq) {
    n.u8(k, 'k');
    n.u8(flags, 'flags');
    final s = n.index(seq, 'seq');
    return using((a) {
      final h = a<Uint8>(FreightHeader.bytes), p = a<Uint8>(bytes);
      n.check(zxv15.freightSplit(freightId, k, flags, expandLimit, n.bytesIn(a, data), data.length, s, h, p));
      return FreightMember(FreightHeader.parse(h.asTypedList(FreightHeader.bytes)), p.asTypedList(bytes));
    });
  }
}

/// Members received, members needed, whether complete, and the total length.
typedef FreightJoinStatus = ({int received, int count, bool complete, int total});

/// Collects set members in any order and rebuilds the data. Pure.
final class FreightJoin {
  FreightJoin._(Pointer<Void> p) {
    _h = n.NativeHandle('freight join', p, zxv15.freightJoinDestroy, null, null);
    n.handleFinalizer.attach(this, _h, detach: this);
  }

  late final n.NativeHandle _h;

  /// A join for a set of at most `capacity` bytes.
  factory FreightJoin.create(int capacity) => using((a) {
    final o = a<Pointer<Void>>();
    n.check(zxv15.freightJoinCreate(capacity, o));
    return FreightJoin._(o.value);
  });

  /// Adds a member's packets (any `k` distinct packets of it).
  void add(FreightHeader header, List<int> packets) => using((a) {
    n.check(zxv15.freightJoinAdd(_h.ptr, n.bytesIn(a, header._b), n.bytesIn(a, packets), packets.length));
  });

  /// Adds a whole member.
  void addMember(FreightMember member) => add(member.header, member._p);

  /// Progress.
  FreightJoinStatus status() => using((a) {
    final r = a<Uint32>(), c = a<Uint32>(), done = a<Int32>(), t = a<Uint64>();
    n.check(zxv15.freightJoinStatus(_h.ptr, r, c, done, t));
    return (received: r.value, count: c.value, complete: done.value != 0, total: t.value);
  });

  /// The rebuilt data; `ZXV_E_STATE` until complete.
  Uint8List result() {
    final h = _h.ptr;
    return n.bytes((b, c, l) => zxv15.freightJoinResult(h, b, c, l), null);
  }

  bool get isClosed => _h.isClosed;

  /// Frees the join. Idempotent.
  void close() {
    n.handleFinalizer.detach(this);
    _h.close();
  }
}

/// The procedural generators an op stream's SEED op can name.
enum Generator {
  xorshift(1),
  repeat(2),
  fibword(3),
  sierpinski(4);

  const Generator(this.code);
  final int code;

  static Generator fromCode(int code) =>
      values.firstWhere((g) => g.code == code, orElse: () => throw n.usage(n.eArg, 'unknown generator $code'));
}

/// Resolves a CIDREF op: the bytes `cid` names, exactly `length` long, or
/// null if unknown (the expansion then fails with `ZXV_E_NOT_FOUND`).
typedef CidResolver = List<int>? Function(Uint8List cid, int length);

/// The declared total and whether a SHA-256 is carried.
typedef OpStreamPeek = ({int total, bool hasSha});

/// Op streams: a compact description of bytes (literals, back copies, CID
/// references, procedural seeds) that expands under a limit. Pure.
abstract final class OpStream {
  static const int opsMax = 67108864;
  static const int cidMax = 128;
  static const int genParamMax = 32;

  /// Reads an op stream's header.
  static OpStreamPeek peek(List<int> ops) => using((a) {
    final t = a<Uint64>(), s = a<Int32>();
    n.check(zxv15.freightOpsPeek(n.bytesIn(a, ops), ops.length, t, s));
    return (total: t.value, hasSha: s.value != 0);
  });

  /// Expands `ops` to at most `limit` bytes. CIDREF ops go to `resolver`
  /// (none: they fail with `ZXV_E_NOT_FOUND`).
  ///
  /// The resolver runs synchronously, on this isolate's thread, inside the
  /// native call (a `NativeCallable.isolateLocal`), so it must not be async
  /// and must not expand op streams itself. It is asked once per distinct
  /// CID and length. An exception it throws never crosses C: the callback
  /// returns a failure status, the expansion stops, and the exception is
  /// rethrown here as is. An answer of the wrong length is a
  /// [ZxvUsageException].
  static Uint8List expand(List<int> ops, int limit, {CidResolver? resolver}) {
    Object? failure;
    StackTrace? failureTrace;
    final answers = <String, Uint8List?>{};
    int resolve(Pointer<Void> user, Pointer<Uint8> cid, int cidLen, Pointer<Uint8> out, int len) {
      try {
        final id = n.bytesOut(cid, cidLen);
        final key = '${n.hex(id)}/$len';
        final got = answers.containsKey(key) ? answers[key] : (answers[key] = _copy(resolver!(id, len)));
        if (got == null) return n.eNotFound;
        if (got.length != len) {
          throw n.usage(n.eArg, 'CidResolver returned ${got.length} bytes, the op needs $len');
        }
        out.asTypedList(len).setAll(0, got);
        return 0;
      } catch (e, st) {
        failure ??= e;
        failureTrace ??= st;
        return n.eArg;
      }
    }

    final cb = resolver == null
        ? null
        : NativeCallable<FreightResolveC>.isolateLocal(resolve, exceptionalReturn: n.eArg);
    try {
      return using((a) {
        final o = n.bytesIn(a, ops);
        final fn = cb?.nativeFunction ?? nullptr;
        final len = a<Size>();
        var st = zxv15.freightOpsExpand(o, ops.length, limit, fn, nullptr, nullptr, 0, len);
        if (st == 0) return Uint8List(0);
        if (st == n.eBuffer && failure == null) {
          final cap = len.value;
          final buf = a<Uint8>(cap == 0 ? 1 : cap);
          st = zxv15.freightOpsExpand(o, ops.length, limit, fn, nullptr, buf, cap, len);
          if (st == 0) return n.bytesOut(buf, len.value);
        }
        final f = failure;
        if (f != null) Error.throwWithStackTrace(f, failureTrace ?? StackTrace.current);
        throw n.raise(st, n.lastError(), null);
      });
    } finally {
      cb?.close();
    }
  }

  static Uint8List? _copy(List<int>? b) => b == null ? null : Uint8List.fromList(b);

  /// `input` as an op stream (literals and copies), with its SHA-256 if `withSha`.
  static Uint8List encode(List<int> input, {required bool withSha}) => using((a) {
    final i = n.bytesIn(a, input);
    return n.bytes((b, c, l) => zxv15.freightOpsEncode(i, input.length, withSha ? 1 : 0, b, c, l), null);
  });

  /// The largest [encode] output for `count` input bytes.
  static int bound(int count, {required bool withSha}) => using((a) {
    final o = a<Uint64>();
    n.check(zxv15.freightOpsBound(count, withSha ? 1 : 0, o));
    return o.value;
  });

  /// `length` bytes from a generator.
  static Uint8List generate(Generator generator, List<int>? params, int length) {
    n.index(length, 'length');
    return using((a) {
      final o = a<Uint8>(length == 0 ? 1 : length);
      final p = params ?? const <int>[];
      n.check(zxv15.freightGenRun(generator.code, n.bytesIn(a, p), p.length, o, length));
      return n.bytesOut(o, length);
    });
  }
}

/// Writes an op stream op by op. Pure.
final class OpStreamWriter {
  OpStreamWriter._(Pointer<Void> p) {
    _h = n.NativeHandle('op stream writer', p, zxv15.freightOpsWriterDestroy, null, null);
    n.handleFinalizer.attach(this, _h, detach: this);
  }

  late final n.NativeHandle _h;

  /// A writer for `declaredTotal` bytes, with an optional SHA-256 of them, of at most `maxBytes` encoded.
  factory OpStreamWriter.create(int declaredTotal, List<int>? sha256, int maxBytes) {
    if (sha256 != null) n.requireLength(sha256, 32, 'sha256');
    return using((a) {
      final o = a<Pointer<Void>>();
      n.check(zxv15.freightOpsWriterCreate(declaredTotal, n.optBytes(a, sha256), maxBytes, o));
      return OpStreamWriter._(o.value);
    });
  }

  OpStreamWriter _op(int Function(Arena a, Pointer<Void> w) f) {
    using((a) => n.check(f(a, _h.ptr)));
    return this;
  }

  /// A literal.
  OpStreamWriter literal(List<int> bytes) =>
      _op((a, w) => zxv15.freightOpsPutLiteral(w, n.bytesIn(a, bytes), bytes.length));

  /// A copy of `length` bytes from `distance` back.
  OpStreamWriter copy(int distance, int length) => _op((a, w) => zxv15.freightOpsPutCopy(w, distance, length));

  /// A reference to `length` bytes named by `cid`.
  OpStreamWriter cidRef(List<int> cid, int length) =>
      _op((a, w) => zxv15.freightOpsPutCidref(w, n.bytesIn(a, cid), cid.length, length));

  /// `length` bytes from a generator.
  OpStreamWriter seed(Generator generator, List<int>? params, int length) {
    final p = params ?? const <int>[];
    return _op((a, w) => zxv15.freightOpsPutSeed(w, generator.code, n.bytesIn(a, p), p.length, length));
  }

  /// The encoded stream.
  Uint8List finish() {
    final w = _h.ptr;
    return n.bytes((b, c, l) => zxv15.freightOpsWriterFinish(w, b, c, l), null);
  }

  bool get isClosed => _h.isClosed;

  /// Frees the writer. Idempotent.
  void close() {
    n.handleFinalizer.detach(this);
    _h.close();
  }
}
