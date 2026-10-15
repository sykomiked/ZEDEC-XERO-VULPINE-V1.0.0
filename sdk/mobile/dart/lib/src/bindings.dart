// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

// Hand-written dart:ffi declarations of the libzxv functions this package
// uses, with the exact C types of sdk/libzxv/include/zxv_api.h. Internal.
//
// Narrow unsigned parameters are declared Uint8/Uint32 so the ABI's
// zero-extension is done by the FFI. Dart TRUNCATES an int passed as Uint8
// (256 becomes 0), so every caller range-checks first (native.dart u8, u32).
import 'dart:ffi';
import 'dart:io';

import 'package:ffi/ffi.dart';

typedef _I32 = Int32;

final class ZxvLib {
  ZxvLib(this.lib, this.location)
    : version = lib
          .lookupFunction<
            _I32 Function(Pointer<Uint32>, Pointer<Uint32>, Pointer<Uint32>),
            int Function(Pointer<Uint32>, Pointer<Uint32>, Pointer<Uint32>)
          >('zxv_version'),
      features = lib.lookupFunction<_I32 Function(Pointer<Uint32>), int Function(Pointer<Uint32>)>('zxv_features'),
      statusName = lib.lookupFunction<Pointer<Utf8> Function(_I32), Pointer<Utf8> Function(int)>('zxv_status_name'),
      statusClass = lib.lookupFunction<_I32 Function(_I32), int Function(int)>('zxv_status_class'),
      lastError = lib
          .lookupFunction<
            _I32 Function(Pointer<Utf8>, Size, Pointer<Size>),
            int Function(Pointer<Utf8>, int, Pointer<Size>)
          >('zxv_last_error'),
      ctxCreate = lib.lookupFunction<_I32 Function(Pointer<Pointer<Void>>), int Function(Pointer<Pointer<Void>>)>(
        'zxv_ctx_create',
      ),
      ctxDestroy = lib.lookupFunction<Void Function(Pointer<Void>), void Function(Pointer<Void>)>('zxv_ctx_destroy'),
      ctxIsPoisoned = lib
          .lookupFunction<_I32 Function(Pointer<Void>, Pointer<Int32>), int Function(Pointer<Void>, Pointer<Int32>)>(
            'zxv_ctx_is_poisoned',
          ),
      ctxHalt = lib
          .lookupFunction<_I32 Function(Pointer<Void>, Pointer<Utf8>), int Function(Pointer<Void>, Pointer<Utf8>)>(
            'zxv_ctx_halt',
          ),
      capCreate = lib
          .lookupFunction<
            _I32 Function(Pointer<Void>, Uint32, Pointer<Uint8>, Uint64, Pointer<Pointer<Void>>),
            int Function(Pointer<Void>, int, Pointer<Uint8>, int, Pointer<Pointer<Void>>)
          >('zxv_cap_create'),
      capAttenuate = lib
          .lookupFunction<
            _I32 Function(Pointer<Void>, Uint64, Pointer<Pointer<Void>>),
            int Function(Pointer<Void>, int, Pointer<Pointer<Void>>)
          >('zxv_cap_attenuate'),
      capRevoke = lib.lookupFunction<_I32 Function(Pointer<Void>), int Function(Pointer<Void>)>('zxv_cap_revoke'),
      capSetAttestation = lib.lookupFunction<_I32 Function(Pointer<Void>, _I32), int Function(Pointer<Void>, int)>(
        'zxv_cap_set_attestation',
      ),
      capInfo = lib
          .lookupFunction<
            _I32 Function(Pointer<Void>, Pointer<Uint64>, Pointer<Uint32>, Pointer<Int32>, Pointer<Int32>),
            int Function(Pointer<Void>, Pointer<Uint64>, Pointer<Uint32>, Pointer<Int32>, Pointer<Int32>)
          >('zxv_cap_info'),
      capCheck = lib.lookupFunction<_I32 Function(Pointer<Void>, Uint64), int Function(Pointer<Void>, int)>(
        'zxv_cap_check',
      ),
      capDestroy = lib.lookupFunction<Void Function(Pointer<Void>), void Function(Pointer<Void>)>('zxv_cap_destroy'),
      ccyEnableCap = lib
          .lookupFunction<_I32 Function(Pointer<Void>, Pointer<Utf8>), int Function(Pointer<Void>, Pointer<Utf8>)>(
            'zxv_ccy_enable_cap',
          ),
      ccyEnableWithMinorCap = lib
          .lookupFunction<
            _I32 Function(Pointer<Void>, Pointer<Utf8>, Uint8),
            int Function(Pointer<Void>, Pointer<Utf8>, int)
          >('zxv_ccy_enable_with_minor_cap'),
      ccyRegisterPrivateCap = lib
          .lookupFunction<
            _I32 Function(Pointer<Void>, Pointer<Utf8>, Uint8),
            int Function(Pointer<Void>, Pointer<Utf8>, int)
          >('zxv_ccy_register_private_cap'),
      ledgerOpenAccountCap = lib
          .lookupFunction<
            _I32 Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>, _I32, Pointer<Uint32>),
            int Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>, int, Pointer<Uint32>)
          >('zxv_ledger_open_account_cap'),
      ledgerPostCap = lib
          .lookupFunction<
            _I32 Function(Pointer<Void>, Uint32, Uint32, Int64, Pointer<Utf8>, Pointer<Utf8>, Pointer<Uint64>),
            int Function(Pointer<Void>, int, int, int, Pointer<Utf8>, Pointer<Utf8>, Pointer<Uint64>)
          >('zxv_ledger_post_cap'),
      ledgerBalanceCap = lib
          .lookupFunction<
            _I32 Function(Pointer<Void>, Uint32, Pointer<Int64>, Pointer<Int64>, Pointer<Int64>),
            int Function(Pointer<Void>, int, Pointer<Int64>, Pointer<Int64>, Pointer<Int64>)
          >('zxv_ledger_balance_cap'),
      ledgerCheckCap = lib.lookupFunction<_I32 Function(Pointer<Void>), int Function(Pointer<Void>)>(
        'zxv_ledger_check_cap',
      ),
      ledgerAccountCountCap = lib
          .lookupFunction<_I32 Function(Pointer<Void>, Pointer<Uint32>), int Function(Pointer<Void>, Pointer<Uint32>)>(
            'zxv_ledger_account_count_cap',
          ),
      ledgerAccountNameCap = lib
          .lookupFunction<
            _I32 Function(Pointer<Void>, Uint32, Pointer<Utf8>, Size, Pointer<Size>),
            int Function(Pointer<Void>, int, Pointer<Utf8>, int, Pointer<Size>)
          >('zxv_ledger_account_name_cap'),
      ledgerAccountCcyCap = lib
          .lookupFunction<
            _I32 Function(Pointer<Void>, Uint32, Pointer<Utf8>, Size, Pointer<Size>),
            int Function(Pointer<Void>, int, Pointer<Utf8>, int, Pointer<Size>)
          >('zxv_ledger_account_ccy_cap'),
      ledgerAccountKindCap = lib
          .lookupFunction<
            _I32 Function(Pointer<Void>, Uint32, Pointer<Int32>),
            int Function(Pointer<Void>, int, Pointer<Int32>)
          >('zxv_ledger_account_kind_cap'),
      ledgerEntryCountCap = lib
          .lookupFunction<
            _I32 Function(Pointer<Void>, Uint32, Pointer<Uint32>),
            int Function(Pointer<Void>, int, Pointer<Uint32>)
          >('zxv_ledger_entry_count_cap'),
      ledgerEntryAtCap = lib
          .lookupFunction<
            _I32 Function(
              Pointer<Void>,
              Uint32,
              Uint32,
              Pointer<Uint64>,
              Pointer<Int64>,
              Pointer<Uint32>,
              Pointer<Utf8>,
              Size,
              Pointer<Size>,
            ),
            int Function(
              Pointer<Void>,
              int,
              int,
              Pointer<Uint64>,
              Pointer<Int64>,
              Pointer<Uint32>,
              Pointer<Utf8>,
              int,
              Pointer<Size>,
            )
          >('zxv_ledger_entry_at_cap'),
      ratNormalize = lib
          .lookupFunction<
            _I32 Function(Int64, Int64, Pointer<Int64>, Pointer<Int64>),
            int Function(int, int, Pointer<Int64>, Pointer<Int64>)
          >('zxv_rat_normalize'),
      ratAdd = lib.lookupFunction<RatBinC, RatBinFn>('zxv_rat_add'),
      ratSub = lib.lookupFunction<RatBinC, RatBinFn>('zxv_rat_sub'),
      ratMul = lib.lookupFunction<RatBinC, RatBinFn>('zxv_rat_mul'),
      ratDiv = lib.lookupFunction<RatBinC, RatBinFn>('zxv_rat_div'),
      ratCmp = lib
          .lookupFunction<
            _I32 Function(Int64, Int64, Int64, Int64, Pointer<Int32>),
            int Function(int, int, int, int, Pointer<Int32>)
          >('zxv_rat_cmp'),
      ratParse = lib
          .lookupFunction<
            _I32 Function(Pointer<Utf8>, Pointer<Int64>, Pointer<Int64>),
            int Function(Pointer<Utf8>, Pointer<Int64>, Pointer<Int64>)
          >('zxv_rat_parse'),
      ratFormat = lib
          .lookupFunction<
            _I32 Function(Int64, Int64, Pointer<Utf8>, Size, Pointer<Size>),
            int Function(int, int, Pointer<Utf8>, int, Pointer<Size>)
          >('zxv_rat_format'),
      ratFromIeee754 = lib
          .lookupFunction<
            _I32 Function(Uint64, Pointer<Int64>, Pointer<Int64>),
            int Function(int, Pointer<Int64>, Pointer<Int64>)
          >('zxv_rat_from_ieee754'),
      ratToQ16 = lib
          .lookupFunction<_I32 Function(Int64, Int64, Pointer<Int32>), int Function(int, int, Pointer<Int32>)>(
            'zxv_rat_to_q16',
          ),
      lpNot = lib.lookupFunction<_I32 Function(_I32, Pointer<Int32>), int Function(int, Pointer<Int32>)>('zxv_lp_not'),
      lpAnd = lib.lookupFunction<_I32 Function(_I32, _I32, Pointer<Int32>), int Function(int, int, Pointer<Int32>)>(
        'zxv_lp_and',
      ),
      lpOr = lib.lookupFunction<_I32 Function(_I32, _I32, Pointer<Int32>), int Function(int, int, Pointer<Int32>)>(
        'zxv_lp_or',
      ),
      hexPack = lib
          .lookupFunction<
            _I32 Function(Uint8, Uint8, Uint8, Pointer<Uint8>),
            int Function(int, int, int, Pointer<Uint8>)
          >('zxv_hex_pack'),
      hexUnpack = lib
          .lookupFunction<
            _I32 Function(Uint8, Pointer<Uint8>, Pointer<Uint8>, Pointer<Uint8>),
            int Function(int, Pointer<Uint8>, Pointer<Uint8>, Pointer<Uint8>)
          >('zxv_hex_unpack'),
      hexAction = lib.lookupFunction<_I32 Function(Uint8, Pointer<Int32>), int Function(int, Pointer<Int32>)>(
        'zxv_hex_action',
      ),
      hexPhaseNext = lib.lookupFunction<_I32 Function(Uint8, Pointer<Uint8>), int Function(int, Pointer<Uint8>)>(
        'zxv_hex_phase_next',
      ),
      hexStep = lib.lookupFunction<_I32 Function(Uint8, Uint8, Pointer<Uint8>), int Function(int, int, Pointer<Uint8>)>(
        'zxv_hex_step',
      ),
      hexSet = lib
          .lookupFunction<
            _I32 Function(Uint8, Uint8, Uint8, Pointer<Uint8>),
            int Function(int, int, int, Pointer<Uint8>)
          >('zxv_hex_set'),
      hexForbid = lib.lookupFunction<_I32 Function(Uint8, Pointer<Uint8>), int Function(int, Pointer<Uint8>)>(
        'zxv_hex_forbid',
      ),
      hexAnd = lib.lookupFunction<_I32 Function(Uint8, Uint8, Pointer<Uint8>), int Function(int, int, Pointer<Uint8>)>(
        'zxv_hex_and',
      ),
      hexOr = lib.lookupFunction<_I32 Function(Uint8, Uint8, Pointer<Uint8>), int Function(int, int, Pointer<Uint8>)>(
        'zxv_hex_or',
      ),
      hexNot = lib.lookupFunction<_I32 Function(Uint8, Pointer<Uint8>), int Function(int, Pointer<Uint8>)>(
        'zxv_hex_not',
      ),
      hexPolarity = lib
          .lookupFunction<
            _I32 Function(Uint32, Uint32, Uint32, Uint32, _I32, Pointer<Uint8>),
            int Function(int, int, int, int, int, Pointer<Uint8>)
          >('zxv_hex_polarity'),
      hexFromLp = lib.lookupFunction<_I32 Function(_I32, Pointer<Uint8>), int Function(int, Pointer<Uint8>)>(
        'zxv_hex_from_lp',
      ),
      hexToLp = lib.lookupFunction<_I32 Function(Uint8, Pointer<Int32>), int Function(int, Pointer<Int32>)>(
        'zxv_hex_to_lp',
      ),
      hexName = lib.lookupFunction<Pointer<Utf8> Function(Uint8), Pointer<Utf8> Function(int)>('zxv_hex_name'),
      hexActionName = lib.lookupFunction<Pointer<Utf8> Function(_I32), Pointer<Utf8> Function(int)>(
        'zxv_hex_action_name',
      ),
      hexlogCreate = lib
          .lookupFunction<
            _I32 Function(Pointer<Void>, Uint32, Pointer<Pointer<Void>>),
            int Function(Pointer<Void>, int, Pointer<Pointer<Void>>)
          >('zxv_hexlog_create'),
      hexlogDestroy = lib.lookupFunction<Void Function(Pointer<Void>), void Function(Pointer<Void>)>(
        'zxv_hexlog_destroy',
      ),
      hexlogAppend = lib
          .lookupFunction<
            _I32 Function(Pointer<Void>, Pointer<Void>, Uint8, Uint8, Uint32, Pointer<Uint8>, Pointer<Uint64>),
            int Function(Pointer<Void>, Pointer<Void>, int, int, int, Pointer<Uint8>, Pointer<Uint64>)
          >('zxv_hexlog_append'),
      hexlogCount = lib
          .lookupFunction<_I32 Function(Pointer<Void>, Pointer<Uint32>), int Function(Pointer<Void>, Pointer<Uint32>)>(
            'zxv_hexlog_count',
          ),
      hexlogHead = lib
          .lookupFunction<_I32 Function(Pointer<Void>, Pointer<Uint8>), int Function(Pointer<Void>, Pointer<Uint8>)>(
            'zxv_hexlog_head',
          ),
      hexlogAt = lib
          .lookupFunction<
            _I32 Function(
              Pointer<Void>,
              Uint32,
              Pointer<Uint64>,
              Pointer<Uint8>,
              Pointer<Uint8>,
              Pointer<Uint32>,
              Pointer<Uint8>,
              Pointer<Uint8>,
              Pointer<Uint8>,
            ),
            int Function(
              Pointer<Void>,
              int,
              Pointer<Uint64>,
              Pointer<Uint8>,
              Pointer<Uint8>,
              Pointer<Uint32>,
              Pointer<Uint8>,
              Pointer<Uint8>,
              Pointer<Uint8>,
            )
          >('zxv_hexlog_at'),
      hexlogVerify = lib.lookupFunction<_I32 Function(Pointer<Void>), int Function(Pointer<Void>)>('zxv_hexlog_verify'),
      rmagCreateCap = lib
          .lookupFunction<
            _I32 Function(Pointer<Void>, Pointer<Pointer<Void>>),
            int Function(Pointer<Void>, Pointer<Pointer<Void>>)
          >('zxv_rmag_create_cap'),
      rmagDestroy = lib.lookupFunction<Void Function(Pointer<Void>), void Function(Pointer<Void>)>('zxv_rmag_destroy'),
      rmagBudget = lib
          .lookupFunction<
            _I32 Function(Pointer<Void>, Pointer<Void>, Pointer<Utf8>, _I32, Int64, Int64, _I32, Pointer<Uint32>),
            int Function(Pointer<Void>, Pointer<Void>, Pointer<Utf8>, int, int, int, int, Pointer<Uint32>)
          >('zxv_rmag_budget'),
      rmagConsume = lib.lookupFunction<RmagC, RmagFn>('zxv_rmag_consume'),
      rmagRelease = lib.lookupFunction<RmagC, RmagFn>('zxv_rmag_release'),
      rmagSetLimit = lib.lookupFunction<RmagC, RmagFn>('zxv_rmag_set_limit'),
      rmagStatus = lib
          .lookupFunction<
            _I32 Function(
              Pointer<Void>,
              Uint32,
              Pointer<Int64>,
              Pointer<Int64>,
              Pointer<Int64>,
              Pointer<Int64>,
              Pointer<Uint64>,
            ),
            int Function(
              Pointer<Void>,
              int,
              Pointer<Int64>,
              Pointer<Int64>,
              Pointer<Int64>,
              Pointer<Int64>,
              Pointer<Uint64>,
            )
          >('zxv_rmag_status'),
      sha3_256 = lib.lookupFunction<HashC, HashFn>('zxv_sha3_256'),
      keccak256 = lib.lookupFunction<HashC, HashFn>('zxv_keccak256'),
      shake256 = lib
          .lookupFunction<
            _I32 Function(Pointer<Uint8>, Size, Pointer<Uint8>, Size),
            int Function(Pointer<Uint8>, int, Pointer<Uint8>, int)
          >('zxv_shake256'),
      secureZero = lib.lookupFunction<_I32 Function(Pointer<Void>, Size), int Function(Pointer<Void>, int)>(
        'zxv_secure_zero',
      ),
      ed25519Keygen = lib
          .lookupFunction<
            _I32 Function(Pointer<Void>, Pointer<Uint8>, Pointer<Uint8>),
            int Function(Pointer<Void>, Pointer<Uint8>, Pointer<Uint8>)
          >('zxv_ed25519_keygen'),
      ed25519Public = lib
          .lookupFunction<
            _I32 Function(Pointer<Void>, Pointer<Uint8>, Pointer<Uint8>),
            int Function(Pointer<Void>, Pointer<Uint8>, Pointer<Uint8>)
          >('zxv_ed25519_public'),
      ed25519Sign = lib
          .lookupFunction<
            _I32 Function(Pointer<Void>, Pointer<Uint8>, Pointer<Uint8>, Size, Pointer<Uint8>),
            int Function(Pointer<Void>, Pointer<Uint8>, Pointer<Uint8>, int, Pointer<Uint8>)
          >('zxv_ed25519_sign'),
      ed25519Verify = lib
          .lookupFunction<
            _I32 Function(Pointer<Void>, Pointer<Uint8>, Pointer<Uint8>, Size, Pointer<Uint8>),
            int Function(Pointer<Void>, Pointer<Uint8>, Pointer<Uint8>, int, Pointer<Uint8>)
          >('zxv_ed25519_verify');

  final DynamicLibrary lib;
  final String location;

  final int Function(Pointer<Uint32>, Pointer<Uint32>, Pointer<Uint32>) version;
  final int Function(Pointer<Uint32>) features;
  final Pointer<Utf8> Function(int) statusName;
  final int Function(int) statusClass;
  final int Function(Pointer<Utf8>, int, Pointer<Size>) lastError;
  final int Function(Pointer<Pointer<Void>>) ctxCreate;
  final void Function(Pointer<Void>) ctxDestroy;
  final int Function(Pointer<Void>, Pointer<Int32>) ctxIsPoisoned;
  final int Function(Pointer<Void>, Pointer<Utf8>) ctxHalt;
  final int Function(Pointer<Void>, int, Pointer<Uint8>, int, Pointer<Pointer<Void>>) capCreate;
  final int Function(Pointer<Void>, int, Pointer<Pointer<Void>>) capAttenuate;
  final int Function(Pointer<Void>) capRevoke;
  final int Function(Pointer<Void>, int) capSetAttestation;
  final int Function(Pointer<Void>, Pointer<Uint64>, Pointer<Uint32>, Pointer<Int32>, Pointer<Int32>) capInfo;
  final int Function(Pointer<Void>, int) capCheck;
  final void Function(Pointer<Void>) capDestroy;
  final int Function(Pointer<Void>, Pointer<Utf8>) ccyEnableCap;
  final int Function(Pointer<Void>, Pointer<Utf8>, int) ccyEnableWithMinorCap;
  final int Function(Pointer<Void>, Pointer<Utf8>, int) ccyRegisterPrivateCap;
  final int Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>, int, Pointer<Uint32>) ledgerOpenAccountCap;
  final int Function(Pointer<Void>, int, int, int, Pointer<Utf8>, Pointer<Utf8>, Pointer<Uint64>) ledgerPostCap;
  final int Function(Pointer<Void>, int, Pointer<Int64>, Pointer<Int64>, Pointer<Int64>) ledgerBalanceCap;
  final int Function(Pointer<Void>) ledgerCheckCap;
  final int Function(Pointer<Void>, Pointer<Uint32>) ledgerAccountCountCap;
  final int Function(Pointer<Void>, int, Pointer<Utf8>, int, Pointer<Size>) ledgerAccountNameCap;
  final int Function(Pointer<Void>, int, Pointer<Utf8>, int, Pointer<Size>) ledgerAccountCcyCap;
  final int Function(Pointer<Void>, int, Pointer<Int32>) ledgerAccountKindCap;
  final int Function(Pointer<Void>, int, Pointer<Uint32>) ledgerEntryCountCap;
  final int Function(
    Pointer<Void>,
    int,
    int,
    Pointer<Uint64>,
    Pointer<Int64>,
    Pointer<Uint32>,
    Pointer<Utf8>,
    int,
    Pointer<Size>,
  )
  ledgerEntryAtCap;
  final int Function(int, int, Pointer<Int64>, Pointer<Int64>) ratNormalize;
  final RatBinFn ratAdd, ratSub, ratMul, ratDiv;
  final int Function(int, int, int, int, Pointer<Int32>) ratCmp;
  final int Function(Pointer<Utf8>, Pointer<Int64>, Pointer<Int64>) ratParse;
  final int Function(int, int, Pointer<Utf8>, int, Pointer<Size>) ratFormat;
  final int Function(int, Pointer<Int64>, Pointer<Int64>) ratFromIeee754;
  final int Function(int, int, Pointer<Int32>) ratToQ16;
  final int Function(int, Pointer<Int32>) lpNot;
  final int Function(int, int, Pointer<Int32>) lpAnd, lpOr;
  final int Function(int, int, int, Pointer<Uint8>) hexPack;
  final int Function(int, Pointer<Uint8>, Pointer<Uint8>, Pointer<Uint8>) hexUnpack;
  final int Function(int, Pointer<Int32>) hexAction;
  final int Function(int, Pointer<Uint8>) hexPhaseNext;
  final int Function(int, int, Pointer<Uint8>) hexStep;
  final int Function(int, int, int, Pointer<Uint8>) hexSet;
  final int Function(int, Pointer<Uint8>) hexForbid;
  final int Function(int, int, Pointer<Uint8>) hexAnd, hexOr;
  final int Function(int, Pointer<Uint8>) hexNot;
  final int Function(int, int, int, int, int, Pointer<Uint8>) hexPolarity;
  final int Function(int, Pointer<Uint8>) hexFromLp;
  final int Function(int, Pointer<Int32>) hexToLp;
  final Pointer<Utf8> Function(int) hexName;
  final Pointer<Utf8> Function(int) hexActionName;
  final int Function(Pointer<Void>, int, Pointer<Pointer<Void>>) hexlogCreate;
  final void Function(Pointer<Void>) hexlogDestroy;
  final int Function(Pointer<Void>, Pointer<Void>, int, int, int, Pointer<Uint8>, Pointer<Uint64>) hexlogAppend;
  final int Function(Pointer<Void>, Pointer<Uint32>) hexlogCount;
  final int Function(Pointer<Void>, Pointer<Uint8>) hexlogHead;
  final int Function(
    Pointer<Void>,
    int,
    Pointer<Uint64>,
    Pointer<Uint8>,
    Pointer<Uint8>,
    Pointer<Uint32>,
    Pointer<Uint8>,
    Pointer<Uint8>,
    Pointer<Uint8>,
  )
  hexlogAt;
  final int Function(Pointer<Void>) hexlogVerify;
  final int Function(Pointer<Void>, Pointer<Pointer<Void>>) rmagCreateCap;
  final void Function(Pointer<Void>) rmagDestroy;
  final int Function(Pointer<Void>, Pointer<Void>, Pointer<Utf8>, int, int, int, int, Pointer<Uint32>) rmagBudget;
  final RmagFn rmagConsume, rmagRelease, rmagSetLimit;
  final int Function(
    Pointer<Void>,
    int,
    Pointer<Int64>,
    Pointer<Int64>,
    Pointer<Int64>,
    Pointer<Int64>,
    Pointer<Uint64>,
  )
  rmagStatus;
  final HashFn sha3_256, keccak256;
  final int Function(Pointer<Uint8>, int, Pointer<Uint8>, int) shake256;
  final int Function(Pointer<Void>, int) secureZero;
  final int Function(Pointer<Void>, Pointer<Uint8>, Pointer<Uint8>) ed25519Keygen;
  final int Function(Pointer<Void>, Pointer<Uint8>, Pointer<Uint8>) ed25519Public;
  final int Function(Pointer<Void>, Pointer<Uint8>, Pointer<Uint8>, int, Pointer<Uint8>) ed25519Sign;
  final int Function(Pointer<Void>, Pointer<Uint8>, Pointer<Uint8>, int, Pointer<Uint8>) ed25519Verify;
}

typedef RatBinC = Int32 Function(Int64, Int64, Int64, Int64, Pointer<Int64>, Pointer<Int64>);
typedef RatBinFn = int Function(int, int, int, int, Pointer<Int64>, Pointer<Int64>);
typedef RmagC = Int32 Function(Pointer<Void>, Pointer<Void>, Uint32, Int64, Int64);
typedef RmagFn = int Function(Pointer<Void>, Pointer<Void>, int, int, int);
typedef HashC = Int32 Function(Pointer<Uint8>, Size, Pointer<Uint8>);
typedef HashFn = int Function(Pointer<Uint8>, int, Pointer<Uint8>);

String? _explicitPath;
ZxvLib? _lib;

/// Sets the library path before first use.
void setExplicitLibraryPath(String path) {
  if (_lib != null) throw StateError('libzxv is already loaded from ${_lib!.location}');
  _explicitPath = path;
}

/// The loaded library (loads and version-checks it on first use).
ZxvLib get zxvLib => _lib ??= _load();

ZxvLib _load() {
  final (DynamicLibrary lib, String where) = _open();
  final z = ZxvLib(lib, where);
  final v = calloc<Uint32>(3);
  try {
    final rc = z.version(v, v + 1, v + 2);
    if (rc != 0) throw StateError('zxv_version failed with status $rc');
    if (v[0] != 1 || v[1] < 6) {
      throw UnsupportedError('need libzxv ABI 1.6 or a later 1.x, found ${v[0]}.${v[1]}.${v[2]} at $where');
    }
  } finally {
    calloc.free(v);
  }
  return z;
}

/// The SDK_DESIGN search order: explicit path, ZXV_LIBRARY_PATH, the copy
/// bundled with the app, then the system loader.
(DynamicLibrary, String) _open() {
  final name = Platform.isMacOS || Platform.isIOS
      ? 'libzxv.dylib'
      : Platform.isWindows
      ? 'zxv.dll'
      : 'libzxv.so';
  String? path = _explicitPath ?? Platform.environment['ZXV_LIBRARY_PATH'];
  if (path != null && path.isNotEmpty) {
    if (FileSystemEntity.isDirectorySync(path)) path = '$path/$name';
    return (DynamicLibrary.open(path), path);
  }
  if (Platform.isIOS) {
    // The xcframework is linked statically into the app binary.
    return (DynamicLibrary.process(), 'the app binary (static xcframework)');
  }
  if (Platform.isAndroid) {
    // jniLibs/<abi>/libzxv.so, packaged by the app's Gradle build.
    return (DynamicLibrary.open('libzxv.so'), 'the APK (lib/<abi>/libzxv.so)');
  }
  return (DynamicLibrary.open(name), 'the system loader ($name)');
}
