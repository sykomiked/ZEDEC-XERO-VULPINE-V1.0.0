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

/// Web 4 sizes, and the action digest consent tokens name. Pure.
abstract final class Web4 {
  /// An ML-DSA-65 public key.
  static const int publicKeyBytes = 1952;

  /// An ML-DSA-65 signature.
  static const int signatureBytes = 3309;

  /// An agent, peer or human id.
  static const int idBytes = 32;
  static const int nameMax = 31;
  static const int maxTools = 16;
  static const int maxPayload = 2048;
  static const int cardMax = 7259;
  static const int manifestMax = 4349;
  static const int envelopeMax = 8970;
  static const int directoryMax = 4096;
  static const int consentLogMax = 65536;

  /// The 32-byte digest of one tool call, which a consent token's `action` names.
  static Uint8List actionDigest(String tool, List<int> from, List<int> to, int requestId, List<int>? payload) {
    n.requireLength(from, idBytes, 'from');
    n.requireLength(to, idBytes, 'to');
    final p = payload ?? const <int>[];
    return using((a) {
      final o = a<Uint8>(32);
      n.check(
        zxv15.w4ActionDigest(
          n.cstr(a, tool, 'tool'),
          n.bytesIn(a, from),
          n.bytesIn(a, to),
          requestId,
          n.bytesIn(a, p),
          p.length,
          o,
        ),
      );
      return n.bytesOut(o, 32);
    });
  }
}

/// Consent scopes.
abstract final class ConsentScope {
  static const int money = 0x01;
  static const int pii = 0x02;
}

/// Tool flags.
abstract final class ToolFlag {
  static const int money = 0x01;
  static const int pii = 0x02;
  static const int stream = 0x04;
}

/// One tool in a manifest. Rates and sizes are uint32 values; the price is uint64 bits.
typedef Tool = ({String name, int priceVfv, int ratePerMin, int burst, int flags, int maxInput});

/// What a verified agent card says.
final class VerifiedCard {
  VerifiedCard._(this._a, this._p, this.name, this.boundMs);
  final Uint8List _a, _p;
  final String name;
  final int boundMs;

  Uint8List get agentId => Uint8List.fromList(_a);
  Uint8List get peerId => Uint8List.fromList(_p);

  /// True if the agent bound itself (no node).
  bool get isSelfBound => n.sameBytes(_a, _p);
}

/// An agent card: the agent's public key and name, bound by its node (or itself). A value.
final class AgentCard {
  AgentCard._(this._b);
  final Uint8List _b;

  /// A card from its bytes (checked by [verify]).
  factory AgentCard.of(List<int> bytes) => AgentCard._(Uint8List.fromList(bytes));

  /// A copy of the bytes.
  Uint8List get bytes => Uint8List.fromList(_b);

  /// Checks the binding signature; [VerificationFailedException] if it fails.
  VerifiedCard verify() => using((a) {
    final id = a<Uint8>(32), peer = a<Uint8>(32), name = a<Uint8>(32), bound = a<Uint64>();
    n.check(zxv15.w4CardVerify(n.bytesIn(a, _b), _b.length, id, peer, name.cast(), bound));
    return VerifiedCard._(n.bytesOut(id, 32), n.bytesOut(peer, 32), n.fixedText(name, 32), bound.value);
  });

  @override
  bool operator ==(Object other) => other is AgentCard && n.sameBytes(_b, other._b);

  @override
  int get hashCode => Object.hashAll(_b);

  @override
  String toString() => 'AgentCard[${_b.length} bytes]';
}

/// The kind of an envelope.
enum EnvelopeKind {
  request(1),
  response(2),
  stream(3),
  cancel(4),
  error(5);

  const EnvelopeKind(this.code);
  final int code;

  static EnvelopeKind fromCode(int code) =>
      values.firstWhere((k) => k.code == code, orElse: () => throw n.usage(n.eArg, 'unknown envelope kind $code'));
}

/// A Web 4 agent's key handle (ML-DSA-65 in C memory, never returned in any
/// form), which signs its card, manifests and envelopes. Creating it and
/// anything it signs needs `CRYPTO_SIGN`; issuing consent (as a human's key)
/// needs `ATTEST`. Every signing call passes [capability].
///
/// A seed for a reproducible key is a 32-byte [SecretKey], never a heap
/// list. The handle belongs to the context.
final class AgentKey {
  AgentKey._(this.capability, Pointer<Void> p) {
    _h = n.NativeHandle('web4 agent key', p, zxv15.w4AgentDestroy, capability.context.state, capability.context.handle);
    n.handleFinalizer.attach(this, _h, detach: this);
    using((a) {
      final id = a<Uint8>(32), peer = a<Uint8>(32), pk = a<Uint8>(Web4.publicKeyBytes);
      n.check(zxv15.w4AgentInfo(_h.ptr, id, peer, pk), _st);
      _id = n.bytesOut(id, 32);
      _peer = n.bytesOut(peer, 32);
      _pk = n.bytesOut(pk, Web4.publicKeyBytes);
    });
  }

  /// The capability every signing call passes.
  final Capability capability;
  late final n.NativeHandle _h;
  late final Uint8List _id, _peer, _pk;

  n.ContextState get _st => capability.context.state;

  /// A key: from `seed` (32 bytes) or fresh randomness (null), named `name`
  /// (1..31 of [a-z0-9._-]), bound by `node` (or self-bound) at `nowMs`.
  /// Needs `CRYPTO_SIGN`.
  factory AgentKey.create(Capability cap, String name, int nowMs, {SecretKey? seed, AgentKey? node}) {
    if (seed != null && seed.size != 32) throw n.usage(n.eArg, 'an agent seed is 32 bytes, got ${seed.size}');
    return using((a) {
      final nm = n.cstr(a, name, 'name');
      final o = a<Pointer<Void>>();
      n.check(
        zxv15.w4AgentCreate(
          cap.handle.ptr,
          seed == null ? nullptr : seed.pointer,
          nm,
          node == null ? nullptr : node._h.ptr,
          nowMs,
          o,
        ),
        cap.context.state,
      );
      return AgentKey._(cap, o.value);
    });
  }

  /// A copy of the 32-byte agent id.
  Uint8List get agentId => Uint8List.fromList(_id);

  /// A copy of the 32-byte peer id (the binding node's agent id).
  Uint8List get peerId => Uint8List.fromList(_peer);

  /// A copy of the ML-DSA-65 public key.
  Uint8List get publicKey => Uint8List.fromList(_pk);

  /// The signed agent card.
  AgentCard card() {
    final h = _h.ptr;
    return AgentCard._(n.bytes((b, c, l) => zxv15.w4AgentCard(h, b, c, l), _st));
  }

  /// Seals an envelope to `to`. Needs `CRYPTO_SIGN`.
  Envelope seal(
    EnvelopeKind kind,
    List<int> to,
    int requestId,
    String tool,
    int nowMs, {
    int chunk = 0,
    int flags = 0,
    List<int>? payload,
    ConsentToken? consent,
  }) {
    n.requireLength(to, Web4.idBytes, 'to');
    n.u32(chunk, 'chunk');
    n.u32(flags, 'flags');
    final p = payload ?? const <int>[];
    return using((a) {
      final c = capability.handle.ptr;
      final h = _h.ptr;
      final t = n.cstr(a, tool, 'tool');
      final tp = n.bytesIn(a, to);
      final pp = n.bytesIn(a, p);
      final ct = consent == null ? nullptr : n.bytesIn(a, consent._b);
      return Envelope._(
        n.bytes(
          (b, cap, l) =>
              zxv15.w4EnvSeal(c, h, kind.code, tp, requestId, chunk, t, flags, pp, p.length, ct, nowMs, b, cap, l),
          _st,
        ),
      );
    });
  }

  /// A REQUEST envelope for `tool`.
  Envelope request(List<int> to, int requestId, String tool, List<int>? payload, int nowMs) =>
      seal(EnvelopeKind.request, to, requestId, tool, nowMs, payload: payload);

  /// A reply to `request`. Needs `CRYPTO_SIGN`.
  Envelope reply(Envelope request, EnvelopeKind kind, int nowMs, {int chunk = 0, int flags = 0, List<int>? payload}) {
    n.u32(chunk, 'chunk');
    n.u32(flags, 'flags');
    final p = payload ?? const <int>[];
    return using((a) {
      final c = capability.handle.ptr;
      final h = _h.ptr;
      final r = n.bytesIn(a, request._b);
      final pp = n.bytesIn(a, p);
      return Envelope._(
        n.bytes(
          (b, cap, l) =>
              zxv15.w4EnvReply(c, h, r, request._b.length, kind.code, chunk, flags, pp, p.length, nowMs, b, cap, l),
          _st,
        ),
      );
    });
  }

  /// Signs `manifest` with this key. Needs `CRYPTO_SIGN`.
  void sign(ToolManifest manifest) =>
      n.check(zxv15.w4ManifestSign(capability.handle.ptr, _h.ptr, manifest._h.ptr), _st);

  /// Issues a consent token as this (human's) key for `agentId` to perform
  /// `action` (a [Web4.actionDigest]) in `scope`, up to `maxVfv`, valid in
  /// [issuedMs, expiresMs). `nonce` is 16 bytes or null for random. Needs `ATTEST`.
  ConsentToken issueConsent(
    List<int> agentId,
    List<int> action,
    int scope,
    int maxVfv,
    int issuedMs,
    int expiresMs, {
    List<int>? nonce,
  }) {
    n.requireLength(agentId, 32, 'agentId');
    n.requireLength(action, 32, 'action');
    n.u32(scope, 'scope');
    if (nonce != null) n.requireLength(nonce, 16, 'nonce');
    return using((a) {
      final o = a<Uint8>(ConsentToken.bytesLength);
      n.check(
        zxv15.w4ConsentIssue(
          capability.handle.ptr,
          _h.ptr,
          n.bytesIn(a, agentId),
          n.bytesIn(a, action),
          scope,
          maxVfv,
          issuedMs,
          expiresMs,
          n.optBytes(a, nonce),
          o,
        ),
        _st,
      );
      return ConsentToken.of(o.asTypedList(ConsentToken.bytesLength));
    });
  }

  bool get isClosed => _h.isClosed;

  /// Wipes and frees the key. Idempotent.
  void close() {
    n.handleFinalizer.detach(this);
    _h.close();
  }

  @override
  String toString() => 'AgentKey[${n.hex(_id)}]';
}

/// A directory of verified agent cards, by agent id. Pure.
final class AgentDirectory {
  AgentDirectory._(Pointer<Void> p) {
    _h = n.NativeHandle('agent directory', p, zxv15.w4DirDestroy, null, null);
    n.handleFinalizer.attach(this, _h, detach: this);
  }

  late final n.NativeHandle _h;

  /// A directory of at most `capacity` cards.
  factory AgentDirectory.create(int capacity) {
    n.u32(capacity, 'capacity');
    return using((a) {
      final o = a<Pointer<Void>>();
      n.check(zxv15.w4DirCreate(capacity, o));
      return AgentDirectory._(o.value);
    });
  }

  /// Verifies and adds (or replaces) a card.
  void add(AgentCard card) => using((a) => n.check(zxv15.w4DirAdd(_h.ptr, n.bytesIn(a, card._b), card._b.length)));

  /// The card for `agentId`, or null.
  AgentCard? find(List<int> agentId) {
    n.requireLength(agentId, Web4.idBytes, 'agentId');
    return using((a) {
      final h = _h.ptr;
      final id = n.bytesIn(a, agentId);
      try {
        return AgentCard._(n.bytes((b, c, l) => zxv15.w4DirFind(h, id, b, c, l), null));
      } on ZxvException catch (e) {
        if (e.status == n.eNotFound) return null;
        rethrow;
      }
    });
  }

  /// The number of cards.
  int count() => using((a) {
    final o = a<Uint32>();
    n.check(zxv15.w4DirCount(_h.ptr, o));
    return o.value;
  });

  bool get isClosed => _h.isClosed;

  /// Frees the directory. Idempotent.
  void close() {
    n.handleFinalizer.detach(this);
    _h.close();
  }
}

/// A consent token (3453 bytes), signed by a human's key. A value.
final class ConsentToken {
  ConsentToken._(this._b) {
    using((a) {
      final human = a<Uint8>(32), agent = a<Uint8>(32), act = a<Uint8>(32), nonce = a<Uint8>(16);
      final scope = a<Uint32>(), v = a<Uint64>(3);
      n.check(zxv15.w4ConsentInfo(n.bytesIn(a, _b), human, agent, act, scope, v, v + 1, v + 2, nonce));
      _human = n.bytesOut(human, 32);
      _agent = n.bytesOut(agent, 32);
      _act = n.bytesOut(act, 32);
      _nonce = n.bytesOut(nonce, 16);
      scopeBits = scope.value;
      maxVfv = v[0];
      issuedMs = v[1];
      expiresMs = v[2];
    });
  }

  final Uint8List _b;
  late final Uint8List _human, _agent, _act, _nonce;

  /// The scope bits ([ConsentScope]).
  late final int scopeBits;
  late final int maxVfv, issuedMs, expiresMs;

  /// The token size.
  static const int bytesLength = 3453;

  /// A token from its bytes.
  factory ConsentToken.of(List<int> bytes) {
    n.requireLength(bytes, bytesLength, 'token');
    return ConsentToken._(Uint8List.fromList(bytes));
  }

  Uint8List get bytes => Uint8List.fromList(_b);
  Uint8List get humanId => Uint8List.fromList(_human);
  Uint8List get agentId => Uint8List.fromList(_agent);
  Uint8List get action => Uint8List.fromList(_act);
  Uint8List get nonce => Uint8List.fromList(_nonce);

  @override
  bool operator ==(Object other) => other is ConsentToken && n.sameBytes(_b, other._b);

  @override
  int get hashCode => Object.hashAll(_b);

  @override
  String toString() => 'ConsentToken[scope=$scopeBits, maxVfv=$maxVfv, valid=[$issuedMs, $expiresMs)]';
}

/// A consent log: checks tokens against a human's public key, the agent, the
/// action, the scope needed, the amount and the time, and spends each token
/// once; revoked tokens are refused. Pure (caller state only).
final class ConsentLog {
  ConsentLog._(Pointer<Void> p) {
    _h = n.NativeHandle('consent log', p, zxv15.w4ConsentLogDestroy, null, null);
    n.handleFinalizer.attach(this, _h, detach: this);
  }

  late final n.NativeHandle _h;

  /// A log of at most `capacity` entries.
  factory ConsentLog.create(int capacity) {
    n.u32(capacity, 'capacity');
    return using((a) {
      final o = a<Pointer<Void>>();
      n.check(zxv15.w4ConsentLogCreate(capacity, o));
      return ConsentLog._(o.value);
    });
  }

  /// Checks (and spends) `token`; [ConsentException] if it does not allow this action now.
  void check(
    ConsentToken? token,
    List<int> humanPublicKey,
    List<int> agentId,
    List<int> action,
    int need,
    int amount,
    int nowMs,
  ) {
    n.requireLength(humanPublicKey, Web4.publicKeyBytes, 'humanPublicKey');
    n.requireLength(agentId, Web4.idBytes, 'agentId');
    n.requireLength(action, 32, 'action');
    n.u32(need, 'need');
    using((a) {
      n.check(
        zxv15.w4ConsentCheck(
          _h.ptr,
          token == null ? nullptr : n.bytesIn(a, token._b),
          n.bytesIn(a, humanPublicKey),
          n.bytesIn(a, agentId),
          n.bytesIn(a, action),
          need,
          amount,
          nowMs,
        ),
      );
    });
  }

  /// Revokes `token` from `nowMs`.
  void revoke(ConsentToken token, int nowMs) =>
      using((a) => n.check(zxv15.w4ConsentRevoke(_h.ptr, n.bytesIn(a, token._b), nowMs)));

  /// Drops entries expired by `nowMs`; returns how many remain.
  int prune(int nowMs) => using((a) {
    final o = a<Uint32>();
    n.check(zxv15.w4ConsentLogPrune(_h.ptr, nowMs, o));
    return o.value;
  });

  bool get isClosed => _h.isClosed;

  /// Frees the log. Idempotent.
  void close() {
    n.handleFinalizer.detach(this);
    _h.close();
  }
}

/// A signed envelope. A value; [verify] checks the signature.
final class Envelope {
  Envelope._(this._b) {
    using((a) {
      final kind = a<Int32>(), u32 = a<Uint32>(3), u64 = a<Uint64>(3);
      final f = a<Uint8>(32), t = a<Uint8>(32), tool = a<Uint8>(32);
      n.check(
        zxv15.w4EnvDecode(
          n.bytesIn(a, _b),
          _b.length,
          kind,
          u32,
          f,
          t,
          u64,
          u64 + 1,
          u64 + 2,
          u32 + 1,
          tool.cast(),
          u32 + 2,
        ),
      );
      _kind = kind.value;
      flags = u32[0];
      chunk = u32[1];
      payloadLength = u32[2];
      sequence = u64[0];
      timestampMs = u64[1];
      requestId = u64[2];
      _from = n.bytesOut(f, 32);
      _to = n.bytesOut(t, 32);
      _tool = n.fixedText(tool, 32);
    });
  }

  final Uint8List _b;
  late final int _kind;
  late final Uint8List _from, _to;
  late final String _tool;
  late final int flags, chunk, payloadLength, sequence, timestampMs, requestId;

  static const int flagFinal = 0x01;
  static const int flagConsent = 0x02;

  /// An envelope from its bytes.
  factory Envelope.of(List<int> bytes) => Envelope._(Uint8List.fromList(bytes));

  Uint8List get bytes => Uint8List.fromList(_b);
  EnvelopeKind get kind => EnvelopeKind.fromCode(_kind);
  bool get isFinal => flags & flagFinal != 0;
  Uint8List get from => Uint8List.fromList(_from);
  Uint8List get to => Uint8List.fromList(_to);
  String get tool => _tool;

  /// The payload.
  Uint8List payload() => using((a) {
    final e = n.bytesIn(a, _b);
    return n.bytes((b, c, l) => zxv15.w4EnvPayload(e, _b.length, b, c, l), null);
  });

  /// The consent token it carries, or null.
  ConsentToken? consent() => using((a) {
    final o = a<Uint8>(ConsentToken.bytesLength);
    final st = zxv15.w4EnvConsent(n.bytesIn(a, _b), _b.length, o);
    if (st == n.eNotFound) return null;
    n.check(st);
    return ConsentToken.of(o.asTypedList(ConsentToken.bytesLength));
  });

  /// Returns normally if the sender's signature is valid; [VerificationFailedException] if not.
  void verify(List<int> senderPublicKey) {
    n.requireLength(senderPublicKey, Web4.publicKeyBytes, 'senderPublicKey');
    using((a) => n.check(zxv15.w4EnvVerify(n.bytesIn(a, _b), _b.length, n.bytesIn(a, senderPublicKey))));
  }

  /// [verify] as a bool.
  bool isValid(List<int>? senderPublicKey) =>
      senderPublicKey != null &&
      senderPublicKey.length == Web4.publicKeyBytes &&
      using((a) => zxv15.w4EnvVerify(n.bytesIn(a, _b), _b.length, n.bytesIn(a, senderPublicKey))) == 0;

  @override
  bool operator ==(Object other) => other is Envelope && n.sameBytes(_b, other._b);

  @override
  int get hashCode => Object.hashAll(_b);

  @override
  String toString() => 'Envelope[${kind.name}, tool=$tool, req=$requestId, seq=$sequence]';
}

/// A manifest's ids, version, validity, tool count and whether it is signed.
final class ManifestInfo {
  ManifestInfo._(this._a, this._p, this.version, this.issuedMs, this.expiresMs, this.toolCount, this.signed);
  final Uint8List _a, _p;
  final int version, issuedMs, expiresMs, toolCount;
  final bool signed;

  Uint8List get agentId => Uint8List.fromList(_a);
  Uint8List get peerId => Uint8List.fromList(_p);
}

/// A tool manifest: an agent's priced, rate-limited tools, signed by its
/// key. Pure (signing goes through [AgentKey.sign]).
final class ToolManifest {
  ToolManifest._(Pointer<Void> p) {
    _h = n.NativeHandle('tool manifest', p, zxv15.w4ManifestDestroy, null, null);
    n.handleFinalizer.attach(this, _h, detach: this);
  }

  late final n.NativeHandle _h;

  /// An empty, unsigned manifest.
  factory ToolManifest.create(int version, int issuedMs, int expiresMs) {
    n.u32(version, 'version');
    return using((a) {
      final o = a<Pointer<Void>>();
      n.check(zxv15.w4ManifestCreate(version, issuedMs, expiresMs, o));
      return ToolManifest._(o.value);
    });
  }

  /// A manifest from its encoding.
  factory ToolManifest.decode(List<int> bytes) => using((a) {
    final o = a<Pointer<Void>>();
    n.check(zxv15.w4ManifestDecode(n.bytesIn(a, bytes), bytes.length, o));
    return ToolManifest._(o.value);
  });

  /// Adds a tool.
  ToolManifest addTool(Tool tool) {
    n.u32(tool.ratePerMin, 'ratePerMin');
    n.u32(tool.burst, 'burst');
    n.u32(tool.flags, 'flags');
    n.u32(tool.maxInput, 'maxInput');
    using((a) {
      n.check(
        zxv15.w4ManifestAddTool(
          _h.ptr,
          n.cstr(a, tool.name, 'tool name'),
          tool.priceVfv,
          tool.ratePerMin,
          tool.burst,
          tool.flags,
          tool.maxInput,
        ),
      );
    });
    return this;
  }

  /// Checks the signature against `publicKey` and the validity at `nowMs`.
  void verify(List<int> publicKey, int nowMs) {
    n.requireLength(publicKey, Web4.publicKeyBytes, 'publicKey');
    using((a) => n.check(zxv15.w4ManifestVerify(_h.ptr, n.bytesIn(a, publicKey), nowMs)));
  }

  /// The encoding.
  Uint8List encode() {
    final h = _h.ptr;
    return n.bytes((b, c, l) => zxv15.w4ManifestEncode(h, b, c, l), null);
  }

  /// Canonical JSON.
  String toJson() {
    final h = _h.ptr;
    return n.text((b, c, l) => zxv15.w4ManifestToJson(h, b, c, l), null);
  }

  /// Ids, version, validity, tools, signed.
  ManifestInfo info() => using((a) {
    final id = a<Uint8>(32), peer = a<Uint8>(32);
    final ver = a<Uint32>(), iss = a<Uint64>(), exp = a<Uint64>(), tc = a<Uint32>(), sg = a<Int32>();
    n.check(zxv15.w4ManifestInfo(_h.ptr, id, peer, ver, iss, exp, tc, sg));
    return ManifestInfo._(
      n.bytesOut(id, 32),
      n.bytesOut(peer, 32),
      ver.value,
      iss.value,
      exp.value,
      tc.value,
      sg.value != 0,
    );
  });

  /// Tool `index`.
  Tool tool(int index) {
    final i = n.index(index, 'index');
    return using((a) {
      final name = a<Uint8>(32), price = a<Uint64>(), u = a<Uint32>(4);
      n.check(zxv15.w4ManifestTool(_h.ptr, i, name.cast(), price, u, u + 1, u + 2, u + 3));
      return (
        name: n.fixedText(name, 32),
        priceVfv: price.value,
        ratePerMin: u[0],
        burst: u[1],
        flags: u[2],
        maxInput: u[3],
      );
    });
  }

  /// Every tool.
  List<Tool> tools() => List<Tool>.generate(info().toolCount, tool);

  /// The index of the tool named `name`, or null.
  int? find(String name) => using((a) {
    final o = a<Uint32>();
    final st = zxv15.w4ManifestFind(_h.ptr, n.cstr(a, name, 'name'), o);
    if (st == n.eNotFound) return null;
    n.check(st);
    return o.value;
  });

  bool get isClosed => _h.isClosed;

  /// Frees the manifest. Idempotent.
  void close() {
    n.handleFinalizer.detach(this);
    _h.close();
  }
}
