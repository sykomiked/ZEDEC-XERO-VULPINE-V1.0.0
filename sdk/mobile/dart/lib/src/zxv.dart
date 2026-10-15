// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
import 'dart:ffi';

import 'package:ffi/ffi.dart';

import 'bindings.dart';
import 'bindings15.dart';
import 'errors.dart';
import 'native.dart' as n;

/// An ABI version.
final class ZxvVersion {
  const ZxvVersion(this.major, this.minor, this.patch);
  final int major, minor, patch;

  @override
  String toString() => '$major.$minor.$patch';
}

/// A feature group (`ZXV_FEAT_*`).
enum Feature {
  ledger(0x00000001),
  mxPacs008(0x00000002),
  mxCamt053(0x00000004),
  iso4217(0x00000008),
  mxPacs002(0x00000010),
  mxPacs004(0x00000020),
  mxPacs009(0x00000040),
  mxCamt056(0x00000080),
  mxCamt029(0x00000100),
  netting(0x00000200),
  cards(0x00000400),
  cardCheck(0x00000800),
  vss(0x00001000),
  rational(0x00002000),
  lpres(0x00004000),
  caps(0x00008000),
  rmag(0x00010000),
  phase(0x00020000),
  wire(0x00040000),
  swarm(0x00080000),
  hash(0x00100000),
  mlkem1024(0x00200000),
  mldsa87(0x00400000),
  ed25519(0x00800000),
  tensor(0x01000000),
  lora(0x02000000),
  evm(0x04000000),
  hex(0x08000000);

  const Feature(this.bit);
  final int bit;
}

/// An ABI 1.5+ feature group (`ZXV_FEAT2_*`, from `zxv_features2`; [vino] is ABI 1.6).
enum Feature2 {
  /// Treaties: COBOL copybook records and Sutra text ([Treaty]).
  treaty(0x01),

  /// Content-addressed functions and IPFS CIDs ([Cid], [AipiNode]).
  aipi(0x02),

  /// Fibonacci coins, token denominations and octane ([Coins], [Tokens]).
  coin(0x04),

  /// The 0.08889% assurance fee and its split ([AssuranceFee]).
  fee(0x08),

  /// The V-Bill hub: pegs, quotes, swaps, limit rules ([VHub]).
  vhub(0x10),

  /// Erasure-coded freight packets and op streams ([Freight]).
  freight(0x20),

  /// Web 4 agents: manifests, directory, consent, envelopes ([AgentKey]).
  web4(0x40),

  /// The paradox engine ([ParadoxBook]).
  paradox(0x80),

  /// Vino Embed: the play lane, payment requests, receipts and the meter
  /// ([PlaySandbox]; ABI 1.6).
  vino(0x100);

  const Feature2(this.bit);
  final int bit;
}

/// Library entry point: loading, version, features, status names.
///
/// The library is found, on first use, in this order: the path given to
/// [load]; `ZXV_LIBRARY_PATH` (a file or a directory); the copy bundled with
/// the app (Android `lib/<abi>/libzxv.so`; iOS the statically linked
/// xcframework); then the system loader. A library whose ABI is not 1.6 or
/// a later 1.x is refused.
abstract final class Zxv {
  /// Uses the library at `path` (a file or a directory). Call before anything else.
  static void load(String path) {
    setExplicitLibraryPath(path);
    zxvLib;
  }

  /// Where the library was loaded from.
  static String get libraryLocation => zxvLib.location;

  /// The loaded library's ABI version.
  static ZxvVersion version() => using((a) {
    final v = a<Uint32>(3);
    n.check(zxvLib.version(v, v + 1, v + 2));
    return ZxvVersion(v[0], v[1], v[2]);
  });

  /// The feature groups live in this build.
  static Set<Feature> features() {
    final m = _mask();
    return {
      for (final f in Feature.values)
        if (m & f.bit != 0) f,
    };
  }

  static bool hasFeature(Feature f) => _mask() & f.bit != 0;

  /// The ABI 1.5 feature groups live in this build (`zxv_features2`).
  static Set<Feature2> features2() {
    final m = _mask2();
    return {
      for (final f in Feature2.values)
        if (m & f.bit != 0) f,
    };
  }

  static bool hasFeature2(Feature2 f) => _mask2() & f.bit != 0;

  static int _mask2() => using((a) {
    final m = a<Uint32>();
    n.check(zxv15.features2(m));
    return m.value;
  });

  static int _mask() => using((a) {
    final m = a<Uint32>();
    n.check(zxvLib.features(m));
    return m.value;
  });

  /// "ZXV_E_FUNDS"; "ZXV_E_UNKNOWN" for unknown codes.
  static String statusName(int status) => n.statusName(status);

  /// The class of a status; unknown negative codes are SYSTEM.
  static StatusClass statusClass(int status) => n.statusClass(status);
}
