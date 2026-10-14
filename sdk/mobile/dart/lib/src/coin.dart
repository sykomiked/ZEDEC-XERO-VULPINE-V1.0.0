// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
import 'dart:ffi';
import 'dart:typed_data';

import 'package:ffi/ffi.dart';

import 'bindings15.dart';
import 'context.dart';
import 'errors.dart';
import 'native.dart' as n;

/// Fibonacci coins: 45 rungs, faces 1, 2, 3, 5, 8, ... 1,836,311,903. Every
/// amount splits into the fewest coins (Zeckendorf). Amounts and faces are
/// uint64 values carried in an `int` (bits kept). Pure.
abstract final class Coins {
  /// The number of rungs.
  static const int rungs = 45;

  /// The face of the top rung.
  static const int topFace = 1836311903;

  /// The longest list [split] returns.
  static const int splitMax = 1 << 28;

  /// The face of rung `rung` (0..44).
  static int face(int rung) {
    final r = n.index(rung, 'rung');
    return using((a) {
      final o = a<Uint64>();
      n.check(zxv15.coinFace(r, o));
      return o.value;
    });
  }

  /// The rung of a face; [NotADenominationException] if it is not a face.
  static int rung(int face) => using((a) {
    final o = a<Uint32>();
    n.check(zxv15.coinRung(face, o));
    return o.value;
  });

  /// How many coins [split] would give (counted without building the list).
  static int coinCount(int amount) => using((a) {
    final o = a<Size>();
    final st = zxv15.coinSplit(amount, nullptr, 0, o);
    if (st != n.eBuffer) n.check(st);
    return o.value;
  });

  /// The rungs of the fewest coins making `amount`, highest first.
  static List<int> split(int amount) {
    final count = coinCount(amount);
    if (count > splitMax) {
      throw n.usage(n.eRange, 'split of $amount has $count coins, too many for a list; use coinCount');
    }
    if (count == 0) return const [];
    return using((a) {
      final r = a<Uint32>(count);
      final o = a<Size>();
      n.check(zxv15.coinSplit(amount, r, count, o));
      return List<int>.unmodifiable(r.asTypedList(o.value));
    });
  }
}

/// Token octane levels: how far a holding is compressed into super tiers.
abstract final class Octane {
  static const int plain = 0;
  static const int regular = 4;
  static const int midgrade = 9;
  static const int premium = 14;
  static const int racing = 23;
  static const int max = 63;
}

/// Token denominations: 64 tiers of Fibonacci faces; tiers from [superTier]
/// up are super tokens. Pure.
abstract final class Tokens {
  /// The number of tiers.
  static const int tiers = 64;

  /// The first super tier.
  static const int superTier = 10;

  /// The face of `tier` (uint64 bits).
  static int face(int tier) => _faceInfo(tier).$1;

  /// True if `tier` is a super tier.
  static bool isSuper(int tier) => _faceInfo(tier).$2;

  static (int, bool) _faceInfo(int tier) {
    final t = n.index(tier, 'tier');
    return using((a) {
      final f = a<Uint64>(), s = a<Int32>();
      n.check(zxv15.tokenFace(t, f, s));
      return (f.value, s.value != 0);
    });
  }

  /// The tier of a face; [NotADenominationException] if it is not one.
  static int tier(int face) => using((a) {
    final o = a<Uint32>();
    n.check(zxv15.tokenTier(face, o));
    return o.value;
  });
}

/// Encoded bytes and how many bits are used.
typedef FibonacciEncoded = ({Uint8List bytes, int bits});

/// A decoded value and the bit after it.
typedef FibonacciDecoded = ({int value, int nextBit});

/// Fibonacci (Zeckendorf) universal codes: self-delimiting bit strings for uint64 values. Pure.
abstract final class FibonacciCode {
  /// The longest code.
  static const int maxBits = 93;

  /// The longest buffer.
  static const int maxBytes = 536870911;

  /// The code length of `value` in bits (`value` >= 1).
  static int bits(int value) => using((a) {
    final o = a<Uint32>();
    n.check(zxv15.tokenCodeBits(value, o));
    return o.value;
  });

  /// The codes of `values`, back to back.
  static FibonacciEncoded encode(List<int> values) {
    var total = 0;
    for (final v in values) {
      total += bits(v);
    }
    if ((total + 7) ~/ 8 > maxBytes) throw n.usage(-12, 'codes too long');
    final buf = Uint8List((total + 7) ~/ 8);
    var bit = 0;
    for (final v in values) {
      bit = put(buf, bit, v);
    }
    return (bytes: buf, bits: bit);
  }

  /// Writes the code of `value` into `buf` at `bit` (in place); returns the bit after it.
  static int put(Uint8List buf, int bit, int value) {
    n.u32(bit, 'bit');
    return using((a) {
      final b = n.bytesIn(a, buf);
      final pos = a<Uint32>()..value = bit;
      n.check(zxv15.tokenCodePut(b, buf.length, pos, value));
      buf.setAll(0, b.asTypedList(buf.length));
      return pos.value;
    });
  }

  /// Reads one code from `buf` at `bit`.
  static FibonacciDecoded get(List<int> buf, int bit) {
    n.u32(bit, 'bit');
    return using((a) {
      final pos = a<Uint32>()..value = bit;
      final v = a<Uint64>();
      n.check(zxv15.tokenCodeGet(n.bytesIn(a, buf), buf.length, pos, v));
      return (value: v.value, nextBit: pos.value);
    });
  }

  /// Reads `count` codes from the start of `buf`.
  static List<int> decode(List<int> buf, int count) {
    n.index(count, 'count');
    final out = <int>[];
    var bit = 0;
    for (var i = 0; i < count; i++) {
      final d = get(buf, bit);
      out.add(d.value);
      bit = d.nextBit;
    }
    return out;
  }
}

/// A token holding: a count (uint64 bits) for each of the 64 tiers. An
/// immutable value; the operations return a new holding. Pure.
final class TokenHolding {
  TokenHolding._(this._c);
  final Uint64List _c;

  /// No tokens.
  factory TokenHolding.empty() => TokenHolding._(Uint64List(Tokens.tiers));

  /// A holding from 64 counts.
  factory TokenHolding.of(List<int> counts) {
    if (counts.length != Tokens.tiers) throw n.usage(n.eArg, 'counts must have 64 entries');
    return TokenHolding._(Uint64List.fromList(counts));
  }

  /// The canonical holding of `value` at `octane`.
  factory TokenHolding.canonical(int value, int octane) {
    final o = n.index(octane, 'octane');
    return using((a) {
      final out = a<Uint64>(Tokens.tiers);
      n.check(zxv15.tokenCanonical(value, o, out));
      return TokenHolding._(Uint64List.fromList(out.asTypedList(Tokens.tiers)));
    });
  }

  /// The count at `tier`.
  int count(int tier) => _c[tier];

  /// A copy of the 64 counts.
  Uint64List get counts => Uint64List.fromList(_c);

  /// This holding with `count` at `tier`.
  TokenHolding withCount(int tier, int count) {
    if (tier < 0 || tier >= Tokens.tiers) throw n.usage(n.eRange, 'tier $tier');
    return TokenHolding._(Uint64List.fromList(_c)..[tier] = count);
  }

  /// The total value.
  int value() => _valueAndPieces().$1;

  /// The number of pieces (tokens).
  int pieces() => _valueAndPieces().$2;

  (int, int) _valueAndPieces() => using((a) {
    final v = a<Uint64>(), p = a<Uint64>();
    n.check(zxv15.tokenValue(_in(a), v, p));
    return (v.value, p.value);
  });

  Pointer<Uint64> _in(Arena a) {
    final p = a<Uint64>(Tokens.tiers);
    p.asTypedList(Tokens.tiers).setAll(0, _c);
    return p;
  }

  /// True if this is the canonical holding of its value at `octane`.
  bool isCanonical(int octane) {
    final o = n.index(octane, 'octane');
    return using((a) {
      final r = a<Int32>();
      n.check(zxv15.tokenIsCanonical(_in(a), o, r));
      return r.value != 0;
    });
  }

  /// Compressed to canonical form at `octane`.
  TokenHolding compress(int octane) {
    final o = n.index(octane, 'octane');
    return _mutate((p) => zxv15.tokenCompress(p, o));
  }

  /// With tokens at `tier` fused upward (at `octane`).
  TokenHolding fuse(int octane, int tier) {
    final o = n.index(octane, 'octane');
    final t = n.index(tier, 'tier');
    return _mutate((p) => zxv15.tokenFuse(p, o, t));
  }

  /// With one token at `tier` split into the two below it.
  TokenHolding split(int tier) {
    final t = n.index(tier, 'tier');
    return _mutate((p) => zxv15.tokenSplit(p, t));
  }

  TokenHolding _mutate(int Function(Pointer<Uint64>) f) => using((a) {
    final p = _in(a);
    n.check(f(p));
    return TokenHolding._(Uint64List.fromList(p.asTypedList(Tokens.tiers)));
  });

  @override
  bool operator ==(Object other) => other is TokenHolding && n.sameBytes(_c, other._c);

  @override
  int get hashCode => Object.hashAll(_c);

  @override
  String toString() =>
      'TokenHolding[${[for (var i = 0; i < _c.length; i++)
        if (_c[i] != 0) '$i:${BigInt.from(_c[i]).toUnsigned(64)}'].join(', ')}]';
}

/// Model id (uint32), octane, value and pieces.
typedef TokenWalletInfo = ({int modelId, int octane, int value, int pieces});

/// A token wallet for one model: a holding kept canonical at its octane.
/// Every change (mint, spend, transfer, octane) needs `MINT` and passes
/// [capability]. Reads need none but fail closed once the context halted.
/// The wallet belongs to the context.
final class TokenWallet {
  TokenWallet._(this.capability, Pointer<Void> p) {
    _h = n.NativeHandle(
      'token wallet',
      p,
      zxv15.tokenWalletDestroy,
      capability.context.state,
      capability.context.handle,
    );
    n.handleFinalizer.attach(this, _h, detach: this);
  }

  /// The capability gated calls pass.
  final Capability capability;
  late final n.NativeHandle _h;

  n.ContextState get _st => capability.context.state;

  /// An empty wallet for `modelId` (uint32) at `octane`.
  factory TokenWallet.create(Capability cap, int modelId, int octane) {
    n.u32(modelId, 'modelId');
    final o = n.index(octane, 'octane');
    return using((a) {
      final out = a<Pointer<Void>>();
      n.check(zxv15.tokenWalletCreate(cap.handle.ptr, modelId, o, out), cap.context.state);
      return TokenWallet._(cap, out.value);
    });
  }

  /// Adds `amount` of value. Needs `MINT`.
  void mint(int amount) => n.check(zxv15.tokenWalletMint(capability.handle.ptr, _h.ptr, amount), _st);

  /// Removes `amount` of value. Needs `MINT`.
  void spend(int amount) => n.check(zxv15.tokenWalletSpend(capability.handle.ptr, _h.ptr, amount), _st);

  /// Re-canonicalizes at a new octane. Needs `MINT`.
  void setOctane(int octane) {
    final o = n.index(octane, 'octane');
    n.check(zxv15.tokenWalletSetOctane(capability.handle.ptr, _h.ptr, o), _st);
  }

  /// Moves `amount` of value to `to`. Needs `MINT`.
  void transfer(TokenWallet to, int amount) =>
      n.check(zxv15.tokenWalletTransfer(capability.handle.ptr, _h.ptr, to._h.ptr, amount), _st);

  /// Model, octane, value, pieces.
  TokenWalletInfo info() => using((a) {
    final m = a<Uint32>(), o = a<Uint32>(), v = a<Uint64>(), p = a<Uint64>();
    n.check(zxv15.tokenWalletInfo(_h.ptr, m, o, v, p), _st);
    return (modelId: m.value, octane: o.value, value: v.value, pieces: p.value);
  });

  /// The value held.
  int value() => info().value;

  /// The holding.
  TokenHolding holding() => using((a) {
    final o = a<Uint64>(Tokens.tiers);
    n.check(zxv15.tokenWalletCounts(_h.ptr, o), _st);
    return TokenHolding._(Uint64List.fromList(o.asTypedList(Tokens.tiers)));
  });

  bool get isClosed => _h.isClosed;

  /// Frees the wallet. Idempotent.
  void close() {
    n.handleFinalizer.detach(this);
    _h.close();
  }
}
