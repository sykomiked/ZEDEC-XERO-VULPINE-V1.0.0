// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

// dart:ffi declarations of the ABI 1.5 groups (treaty, aipi, coin, fee,
// vhub, freight, web4, paradox), zxv_features2 and ML-DSA-87, with the exact
// C types of sdk/libzxv/include/zxv_api.h. Internal.
//
// Generated from the header, then checked against it by
// test/abi_drift_test.dart like bindings.dart. Each lookup is lazy (`late
// final`), so only the functions a program calls are resolved; the loader
// already refuses a library older than ABI 1.5. Narrow unsigned parameters
// are declared Uint8/Uint16/Uint32 and every caller range-checks first
// (native.dart u8, u32): Dart truncates an out-of-range int.
import 'dart:ffi';

import 'package:ffi/ffi.dart';

import 'bindings.dart';

/// `zxv_freight_resolve_fn`: (user, cid, cid_len, out, len) -> status.
typedef FreightResolveC = Int32 Function(Pointer<Void>, Pointer<Uint8>, Size, Pointer<Uint8>, Uint64);

/// The 1.5 entry points of the loaded library.
final class Zxv15Lib {
  Zxv15Lib(this._l);
  final DynamicLibrary _l;

  late final mldsa87Keygen = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Uint8>, Pointer<Uint8>),
        int Function(Pointer<Void>, Pointer<Uint8>, Pointer<Uint8>)
      >('zxv_mldsa87_keygen');
  late final mldsa87KeygenSeed = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Uint8>, Pointer<Uint8>, Pointer<Uint8>),
        int Function(Pointer<Void>, Pointer<Uint8>, Pointer<Uint8>, Pointer<Uint8>)
      >('zxv_mldsa87_keygen_seed');
  late final mldsa87Sign = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Uint8>, Pointer<Uint8>, Size, Pointer<Uint8>, Size, Pointer<Uint8>),
        int Function(Pointer<Void>, Pointer<Uint8>, Pointer<Uint8>, int, Pointer<Uint8>, int, Pointer<Uint8>)
      >('zxv_mldsa87_sign');
  late final mldsa87SignDeterministic = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Uint8>, Pointer<Uint8>, Size, Pointer<Uint8>, Size, Pointer<Uint8>),
        int Function(Pointer<Void>, Pointer<Uint8>, Pointer<Uint8>, int, Pointer<Uint8>, int, Pointer<Uint8>)
      >('zxv_mldsa87_sign_deterministic');
  late final mldsa87Verify = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Uint8>, Pointer<Uint8>, Size, Pointer<Uint8>, Size, Pointer<Uint8>),
        int Function(Pointer<Void>, Pointer<Uint8>, Pointer<Uint8>, int, Pointer<Uint8>, int, Pointer<Uint8>)
      >('zxv_mldsa87_verify');
  late final features2 = _l.lookupFunction<Int32 Function(Pointer<Uint32>), int Function(Pointer<Uint32>)>(
    'zxv_features2',
  );
  late final treatyCreate = _l
      .lookupFunction<Int32 Function(Pointer<Pointer<Void>>), int Function(Pointer<Pointer<Void>>)>(
        'zxv_treaty_create',
      );
  late final treatyDestroy = _l.lookupFunction<Void Function(Pointer<Void>), void Function(Pointer<Void>)>(
    'zxv_treaty_destroy',
  );
  late final treatyClear = _l.lookupFunction<Int32 Function(Pointer<Void>), int Function(Pointer<Void>)>(
    'zxv_treaty_clear',
  );
  late final treatyParseSutra = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Utf8>, Pointer<Uint32>, Pointer<Int32>),
        int Function(Pointer<Void>, Pointer<Utf8>, Pointer<Uint32>, Pointer<Int32>)
      >('zxv_treaty_parse_sutra');
  late final treatyParseRecord = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Uint8>, Size, Pointer<Uint32>, Pointer<Int32>),
        int Function(Pointer<Void>, Pointer<Uint8>, int, Pointer<Uint32>, Pointer<Int32>)
      >('zxv_treaty_parse_record');
  late final treatyEmitSutra = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Utf8>, Size, Pointer<Size>),
        int Function(Pointer<Void>, Pointer<Utf8>, int, Pointer<Size>)
      >('zxv_treaty_emit_sutra');
  late final treatyEmitRecord = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Uint8>, Size, Pointer<Size>),
        int Function(Pointer<Void>, Pointer<Uint8>, int, Pointer<Size>)
      >('zxv_treaty_emit_record');
  late final treatyValidate = _l
      .lookupFunction<Int32 Function(Pointer<Void>, Pointer<Int32>), int Function(Pointer<Void>, Pointer<Int32>)>(
        'zxv_treaty_validate',
      );
  late final treatyEqual = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Void>, Pointer<Int32>),
        int Function(Pointer<Void>, Pointer<Void>, Pointer<Int32>)
      >('zxv_treaty_equal');
  late final treatyName = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Utf8>, Size, Pointer<Size>),
        int Function(Pointer<Void>, Pointer<Utf8>, int, Pointer<Size>)
      >('zxv_treaty_name');
  late final treatyAsset = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Utf8>, Size, Pointer<Size>),
        int Function(Pointer<Void>, Pointer<Utf8>, int, Pointer<Size>)
      >('zxv_treaty_asset');
  late final treatyHeader = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Int32>, Pointer<Uint32>, Pointer<Uint32>),
        int Function(Pointer<Void>, Pointer<Int32>, Pointer<Uint32>, Pointer<Uint32>)
      >('zxv_treaty_header');
  late final treatyPartyId = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Uint32, Pointer<Utf8>, Size, Pointer<Size>),
        int Function(Pointer<Void>, int, Pointer<Utf8>, int, Pointer<Size>)
      >('zxv_treaty_party_id');
  late final treatyParty = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Uint32, Pointer<Int64>, Pointer<Int64>, Pointer<Uint32>, Pointer<Uint32>),
        int Function(Pointer<Void>, int, Pointer<Int64>, Pointer<Int64>, Pointer<Uint32>, Pointer<Uint32>)
      >('zxv_treaty_party');
  late final treatyOwe = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Uint32, Pointer<Uint32>, Pointer<Uint32>, Pointer<Int64>),
        int Function(Pointer<Void>, int, Pointer<Uint32>, Pointer<Uint32>, Pointer<Int64>)
      >('zxv_treaty_owe');
  late final treatyPicAdd = _l
      .lookupFunction<Int32 Function(Int64, Int64, Pointer<Int64>), int Function(int, int, Pointer<Int64>)>(
        'zxv_treaty_pic_add',
      );
  late final treatyPicSub = _l
      .lookupFunction<Int32 Function(Int64, Int64, Pointer<Int64>), int Function(int, int, Pointer<Int64>)>(
        'zxv_treaty_pic_sub',
      );
  late final treatyPicMul = _l
      .lookupFunction<Int32 Function(Int64, Int64, Pointer<Int64>), int Function(int, int, Pointer<Int64>)>(
        'zxv_treaty_pic_mul',
      );
  late final treatyPicDiv = _l
      .lookupFunction<
        Int32 Function(Int64, Int64, Pointer<Int64>, Pointer<Int64>),
        int Function(int, int, Pointer<Int64>, Pointer<Int64>)
      >('zxv_treaty_pic_div');
  late final treatyRun = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Pointer<Void>>),
        int Function(Pointer<Void>, Pointer<Pointer<Void>>)
      >('zxv_treaty_run');
  late final treatyResultDestroy = _l.lookupFunction<Void Function(Pointer<Void>), void Function(Pointer<Void>)>(
    'zxv_treaty_result_destroy',
  );
  late final treatyResultSummary = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Uint32>, Pointer<Uint32>, Pointer<Int64>),
        int Function(Pointer<Void>, Pointer<Uint32>, Pointer<Uint32>, Pointer<Int64>)
      >('zxv_treaty_result_summary');
  late final treatyResultParty = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Uint32, Pointer<Int64>, Pointer<Int64>, Pointer<Int64>),
        int Function(Pointer<Void>, int, Pointer<Int64>, Pointer<Int64>, Pointer<Int64>)
      >('zxv_treaty_result_party');
  late final treatyResultLeg = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Uint32, Pointer<Uint32>, Pointer<Uint32>, Pointer<Int64>),
        int Function(Pointer<Void>, int, Pointer<Uint32>, Pointer<Uint32>, Pointer<Int64>)
      >('zxv_treaty_result_leg');
  late final treatyResultRecord = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Uint8>, Size, Pointer<Size>),
        int Function(Pointer<Void>, Pointer<Uint8>, int, Pointer<Size>)
      >('zxv_treaty_result_record');
  late final cidCompute = _l
      .lookupFunction<
        Int32 Function(Uint32, Pointer<Uint8>, Size, Pointer<Uint8>, Pointer<Size>),
        int Function(int, Pointer<Uint8>, int, Pointer<Uint8>, Pointer<Size>)
      >('zxv_cid_compute');
  late final cidUnixfs = _l
      .lookupFunction<
        Int32 Function(Pointer<Uint8>, Size, Pointer<Uint8>, Pointer<Size>),
        int Function(Pointer<Uint8>, int, Pointer<Uint8>, Pointer<Size>)
      >('zxv_cid_unixfs');
  late final cidVerify = _l
      .lookupFunction<
        Int32 Function(Pointer<Uint8>, Size, Pointer<Uint8>, Size),
        int Function(Pointer<Uint8>, int, Pointer<Uint8>, int)
      >('zxv_cid_verify');
  late final cidToText = _l
      .lookupFunction<
        Int32 Function(Pointer<Uint8>, Size, Uint32, Pointer<Utf8>, Size, Pointer<Size>),
        int Function(Pointer<Uint8>, int, int, Pointer<Utf8>, int, Pointer<Size>)
      >('zxv_cid_to_text');
  late final cidFromText = _l
      .lookupFunction<
        Int32 Function(Pointer<Utf8>, Pointer<Uint8>, Pointer<Size>),
        int Function(Pointer<Utf8>, Pointer<Uint8>, Pointer<Size>)
      >('zxv_cid_from_text');
  late final cidInfo = _l
      .lookupFunction<
        Int32 Function(
          Pointer<Uint8>,
          Size,
          Pointer<Uint32>,
          Pointer<Uint32>,
          Pointer<Uint32>,
          Pointer<Uint8>,
          Pointer<Size>,
        ),
        int Function(
          Pointer<Uint8>,
          int,
          Pointer<Uint32>,
          Pointer<Uint32>,
          Pointer<Uint32>,
          Pointer<Uint8>,
          Pointer<Size>,
        )
      >('zxv_cid_info');
  late final cidToV1 = _l
      .lookupFunction<
        Int32 Function(Pointer<Uint8>, Size, Pointer<Uint8>, Pointer<Size>),
        int Function(Pointer<Uint8>, int, Pointer<Uint8>, Pointer<Size>)
      >('zxv_cid_to_v1');
  late final aipiProviderKey = _l
      .lookupFunction<
        Int32 Function(Pointer<Uint8>, Size, Pointer<Uint8>),
        int Function(Pointer<Uint8>, int, Pointer<Uint8>)
      >('zxv_aipi_provider_key');
  late final aipiFnEncode = _l
      .lookupFunction<
        Int32 Function(
          Uint32,
          Uint32,
          Uint32,
          Uint32,
          Uint32,
          Uint32,
          Uint64,
          Pointer<Uint8>,
          Size,
          Pointer<Uint8>,
          Size,
          Pointer<Utf8>,
          Pointer<Utf8>,
          Pointer<Utf8>,
          Pointer<Uint8>,
          Size,
          Pointer<Size>,
        ),
        int Function(
          int,
          int,
          int,
          int,
          int,
          int,
          int,
          Pointer<Uint8>,
          int,
          Pointer<Uint8>,
          int,
          Pointer<Utf8>,
          Pointer<Utf8>,
          Pointer<Utf8>,
          Pointer<Uint8>,
          int,
          Pointer<Size>,
        )
      >('zxv_aipi_fn_encode');
  late final aipiFnDecode = _l
      .lookupFunction<
        Int32 Function(
          Pointer<Uint8>,
          Size,
          Pointer<Uint32>,
          Pointer<Uint32>,
          Pointer<Uint32>,
          Pointer<Uint32>,
          Pointer<Uint32>,
          Pointer<Uint32>,
          Pointer<Uint64>,
        ),
        int Function(
          Pointer<Uint8>,
          int,
          Pointer<Uint32>,
          Pointer<Uint32>,
          Pointer<Uint32>,
          Pointer<Uint32>,
          Pointer<Uint32>,
          Pointer<Uint32>,
          Pointer<Uint64>,
        )
      >('zxv_aipi_fn_decode');
  late final aipiFnGetCid = _l
      .lookupFunction<
        Int32 Function(Pointer<Uint8>, Size, Uint32, Pointer<Uint8>, Pointer<Size>),
        int Function(Pointer<Uint8>, int, int, Pointer<Uint8>, Pointer<Size>)
      >('zxv_aipi_fn_get_cid');
  late final aipiFnGetName = _l
      .lookupFunction<
        Int32 Function(Pointer<Uint8>, Size, Uint32, Pointer<Utf8>, Size, Pointer<Size>),
        int Function(Pointer<Uint8>, int, int, Pointer<Utf8>, int, Pointer<Size>)
      >('zxv_aipi_fn_get_name');
  late final aipiFnCid = _l
      .lookupFunction<
        Int32 Function(Pointer<Uint8>, Size, Pointer<Uint8>, Pointer<Size>),
        int Function(Pointer<Uint8>, int, Pointer<Uint8>, Pointer<Size>)
      >('zxv_aipi_fn_cid');
  late final aipiCallEncode = _l
      .lookupFunction<
        Int32 Function(Pointer<Uint8>, Size, Pointer<Uint8>, Size, Pointer<Uint8>, Size, Pointer<Size>),
        int Function(Pointer<Uint8>, int, Pointer<Uint8>, int, Pointer<Uint8>, int, Pointer<Size>)
      >('zxv_aipi_call_encode');
  late final aipiCallDecode = _l
      .lookupFunction<
        Int32 Function(Pointer<Uint8>, Size, Pointer<Size>, Pointer<Size>, Pointer<Size>, Pointer<Size>),
        int Function(Pointer<Uint8>, int, Pointer<Size>, Pointer<Size>, Pointer<Size>, Pointer<Size>)
      >('zxv_aipi_call_decode');
  late final aipiCallCid = _l
      .lookupFunction<
        Int32 Function(Pointer<Uint8>, Size, Pointer<Uint8>, Size, Pointer<Uint8>, Pointer<Size>),
        int Function(Pointer<Uint8>, int, Pointer<Uint8>, int, Pointer<Uint8>, Pointer<Size>)
      >('zxv_aipi_call_cid');
  late final aipiClaimEncode = _l
      .lookupFunction<
        Int32 Function(
          Pointer<Uint8>,
          Size,
          Pointer<Uint8>,
          Pointer<Uint8>,
          Size,
          Pointer<Uint8>,
          Size,
          Pointer<Uint8>,
          Size,
          Pointer<Size>,
        ),
        int Function(
          Pointer<Uint8>,
          int,
          Pointer<Uint8>,
          Pointer<Uint8>,
          int,
          Pointer<Uint8>,
          int,
          Pointer<Uint8>,
          int,
          Pointer<Size>,
        )
      >('zxv_aipi_claim_encode');
  late final aipiClaimDecode = _l
      .lookupFunction<
        Int32 Function(
          Pointer<Uint8>,
          Size,
          Pointer<Size>,
          Pointer<Size>,
          Pointer<Size>,
          Pointer<Size>,
          Pointer<Size>,
          Pointer<Size>,
          Pointer<Size>,
        ),
        int Function(
          Pointer<Uint8>,
          int,
          Pointer<Size>,
          Pointer<Size>,
          Pointer<Size>,
          Pointer<Size>,
          Pointer<Size>,
          Pointer<Size>,
          Pointer<Size>,
        )
      >('zxv_aipi_claim_decode');
  late final aipiCreate = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Size, Uint32, Pointer<Uint8>, Pointer<Pointer<Void>>),
        int Function(Pointer<Void>, int, int, Pointer<Uint8>, Pointer<Pointer<Void>>)
      >('zxv_aipi_create');
  late final aipiDestroy = _l.lookupFunction<Void Function(Pointer<Void>), void Function(Pointer<Void>)>(
    'zxv_aipi_destroy',
  );
  late final aipiPut = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Void>, Pointer<Uint8>, Size, Int32, Pointer<Uint8>, Pointer<Size>),
        int Function(Pointer<Void>, Pointer<Void>, Pointer<Uint8>, int, int, Pointer<Uint8>, Pointer<Size>)
      >('zxv_aipi_put');
  late final aipiGet = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Uint8>, Size, Pointer<Uint8>, Size, Pointer<Size>),
        int Function(Pointer<Void>, Pointer<Uint8>, int, Pointer<Uint8>, int, Pointer<Size>)
      >('zxv_aipi_get');
  late final aipiPublishFn = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Void>, Pointer<Uint8>, Size, Pointer<Uint8>, Pointer<Size>),
        int Function(Pointer<Void>, Pointer<Void>, Pointer<Uint8>, int, Pointer<Uint8>, Pointer<Size>)
      >('zxv_aipi_publish_fn');
  late final aipiLoadFn = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Uint8>, Size, Pointer<Uint8>, Size, Pointer<Size>),
        int Function(Pointer<Void>, Pointer<Uint8>, int, Pointer<Uint8>, int, Pointer<Size>)
      >('zxv_aipi_load_fn');
  late final aipiPlan = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Uint8>, Size, Pointer<Uint8>, Size, Int32, Pointer<Int32>),
        int Function(Pointer<Void>, Pointer<Uint8>, int, Pointer<Uint8>, int, int, Pointer<Int32>)
      >('zxv_aipi_plan');
  late final aipiInfo = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Uint32>, Pointer<Uint64>, Pointer<Uint32>, Pointer<Uint32>),
        int Function(Pointer<Void>, Pointer<Uint32>, Pointer<Uint64>, Pointer<Uint32>, Pointer<Uint32>)
      >('zxv_aipi_info');
  late final coinFace = _l.lookupFunction<Int32 Function(Uint32, Pointer<Uint64>), int Function(int, Pointer<Uint64>)>(
    'zxv_coin_face',
  );
  late final coinRung = _l.lookupFunction<Int32 Function(Uint64, Pointer<Uint32>), int Function(int, Pointer<Uint32>)>(
    'zxv_coin_rung',
  );
  late final coinSplit = _l
      .lookupFunction<
        Int32 Function(Uint64, Pointer<Uint32>, Size, Pointer<Size>),
        int Function(int, Pointer<Uint32>, int, Pointer<Size>)
      >('zxv_coin_split');
  late final tokenFace = _l
      .lookupFunction<
        Int32 Function(Uint32, Pointer<Uint64>, Pointer<Int32>),
        int Function(int, Pointer<Uint64>, Pointer<Int32>)
      >('zxv_token_face');
  late final tokenTier = _l.lookupFunction<Int32 Function(Uint64, Pointer<Uint32>), int Function(int, Pointer<Uint32>)>(
    'zxv_token_tier',
  );
  late final tokenValue = _l
      .lookupFunction<
        Int32 Function(Pointer<Uint64>, Pointer<Uint64>, Pointer<Uint64>),
        int Function(Pointer<Uint64>, Pointer<Uint64>, Pointer<Uint64>)
      >('zxv_token_value');
  late final tokenCanonical = _l
      .lookupFunction<Int32 Function(Uint64, Uint32, Pointer<Uint64>), int Function(int, int, Pointer<Uint64>)>(
        'zxv_token_canonical',
      );
  late final tokenIsCanonical = _l
      .lookupFunction<
        Int32 Function(Pointer<Uint64>, Uint32, Pointer<Int32>),
        int Function(Pointer<Uint64>, int, Pointer<Int32>)
      >('zxv_token_is_canonical');
  late final tokenCompress = _l
      .lookupFunction<Int32 Function(Pointer<Uint64>, Uint32), int Function(Pointer<Uint64>, int)>(
        'zxv_token_compress',
      );
  late final tokenFuse = _l
      .lookupFunction<Int32 Function(Pointer<Uint64>, Uint32, Uint32), int Function(Pointer<Uint64>, int, int)>(
        'zxv_token_fuse',
      );
  late final tokenSplit = _l
      .lookupFunction<Int32 Function(Pointer<Uint64>, Uint32), int Function(Pointer<Uint64>, int)>('zxv_token_split');
  late final tokenCodeBits = _l
      .lookupFunction<Int32 Function(Uint64, Pointer<Uint32>), int Function(int, Pointer<Uint32>)>(
        'zxv_token_code_bits',
      );
  late final tokenCodePut = _l
      .lookupFunction<
        Int32 Function(Pointer<Uint8>, Size, Pointer<Uint32>, Uint64),
        int Function(Pointer<Uint8>, int, Pointer<Uint32>, int)
      >('zxv_token_code_put');
  late final tokenCodeGet = _l
      .lookupFunction<
        Int32 Function(Pointer<Uint8>, Size, Pointer<Uint32>, Pointer<Uint64>),
        int Function(Pointer<Uint8>, int, Pointer<Uint32>, Pointer<Uint64>)
      >('zxv_token_code_get');
  late final tokenWalletCreate = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Uint32, Uint32, Pointer<Pointer<Void>>),
        int Function(Pointer<Void>, int, int, Pointer<Pointer<Void>>)
      >('zxv_token_wallet_create');
  late final tokenWalletDestroy = _l.lookupFunction<Void Function(Pointer<Void>), void Function(Pointer<Void>)>(
    'zxv_token_wallet_destroy',
  );
  late final tokenWalletMint = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Void>, Uint64),
        int Function(Pointer<Void>, Pointer<Void>, int)
      >('zxv_token_wallet_mint');
  late final tokenWalletSpend = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Void>, Uint64),
        int Function(Pointer<Void>, Pointer<Void>, int)
      >('zxv_token_wallet_spend');
  late final tokenWalletTransfer = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Void>, Pointer<Void>, Uint64),
        int Function(Pointer<Void>, Pointer<Void>, Pointer<Void>, int)
      >('zxv_token_wallet_transfer');
  late final tokenWalletSetOctane = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Void>, Uint32),
        int Function(Pointer<Void>, Pointer<Void>, int)
      >('zxv_token_wallet_set_octane');
  late final tokenWalletInfo = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Uint32>, Pointer<Uint32>, Pointer<Uint64>, Pointer<Uint64>),
        int Function(Pointer<Void>, Pointer<Uint32>, Pointer<Uint32>, Pointer<Uint64>, Pointer<Uint64>)
      >('zxv_token_wallet_info');
  late final tokenWalletCounts = _l
      .lookupFunction<Int32 Function(Pointer<Void>, Pointer<Uint64>), int Function(Pointer<Void>, Pointer<Uint64>)>(
        'zxv_token_wallet_counts',
      );
  late final feeAssure = _l.lookupFunction<Int32 Function(Uint64, Pointer<Uint64>), int Function(int, Pointer<Uint64>)>(
    'zxv_fee_assure',
  );
  late final feeAssureCarry = _l
      .lookupFunction<
        Int32 Function(Uint64, Uint64, Pointer<Uint64>, Pointer<Uint64>),
        int Function(int, int, Pointer<Uint64>, Pointer<Uint64>)
      >('zxv_fee_assure_carry');
  late final feeSplit = _l.lookupFunction<Int32 Function(Uint64, Pointer<Uint64>), int Function(int, Pointer<Uint64>)>(
    'zxv_fee_split',
  );
  late final feePolicyDefault = _l
      .lookupFunction<
        Int32 Function(
          Pointer<Uint32>,
          Pointer<Int32>,
          Pointer<Uint64>,
          Pointer<Uint64>,
          Pointer<Uint64>,
          Pointer<Uint64>,
          Pointer<Uint64>,
          Pointer<Uint64>,
        ),
        int Function(
          Pointer<Uint32>,
          Pointer<Int32>,
          Pointer<Uint64>,
          Pointer<Uint64>,
          Pointer<Uint64>,
          Pointer<Uint64>,
          Pointer<Uint64>,
          Pointer<Uint64>,
        )
      >('zxv_fee_policy_default');
  late final vhubCreate = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Uint8>, Pointer<Pointer<Void>>),
        int Function(Pointer<Void>, Pointer<Uint8>, Pointer<Pointer<Void>>)
      >('zxv_vhub_create');
  late final vhubDestroy = _l.lookupFunction<Void Function(Pointer<Void>), void Function(Pointer<Void>)>(
    'zxv_vhub_destroy',
  );
  late final vhubAddCcy = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Void>, Uint8, Pointer<Utf8>, Uint8, Pointer<Uint8>, Uint64, Uint64),
        int Function(Pointer<Void>, Pointer<Void>, int, Pointer<Utf8>, int, Pointer<Uint8>, int, int)
      >('zxv_vhub_add_ccy');
  late final vhubCcyInfo = _l
      .lookupFunction<
        Int32 Function(
          Pointer<Void>,
          Pointer<Void>,
          Uint8,
          Pointer<Uint32>,
          Pointer<Uint32>,
          Pointer<Uint32>,
          Pointer<Uint64>,
          Pointer<Uint64>,
          Pointer<Uint64>,
        ),
        int Function(
          Pointer<Void>,
          Pointer<Void>,
          int,
          Pointer<Uint32>,
          Pointer<Uint32>,
          Pointer<Uint32>,
          Pointer<Uint64>,
          Pointer<Uint64>,
          Pointer<Uint64>,
        )
      >('zxv_vhub_ccy_info');
  late final vhubPegMsg = _l
      .lookupFunction<
        Int32 Function(Uint8, Uint64, Uint64, Uint64, Pointer<Uint8>),
        int Function(int, int, int, int, Pointer<Uint8>)
      >('zxv_vhub_peg_msg');
  late final vhubSetPeg = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Void>, Uint8, Uint64, Uint64, Uint64, Pointer<Uint8>),
        int Function(Pointer<Void>, Pointer<Void>, int, int, int, int, Pointer<Uint8>)
      >('zxv_vhub_set_peg');
  late final vhubOpenAccount = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Void>, Uint8, Uint32, Pointer<Uint32>),
        int Function(Pointer<Void>, Pointer<Void>, int, int, Pointer<Uint32>)
      >('zxv_vhub_open_account');
  late final vhubIssue = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Void>, Uint32, Uint64),
        int Function(Pointer<Void>, Pointer<Void>, int, int)
      >('zxv_vhub_issue');
  late final vhubBalance = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Void>, Uint32, Pointer<Uint64>, Pointer<Uint64>, Pointer<Int64>),
        int Function(Pointer<Void>, Pointer<Void>, int, Pointer<Uint64>, Pointer<Uint64>, Pointer<Int64>)
      >('zxv_vhub_balance');
  late final vhubAddLimit = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Void>, Uint8, Uint64),
        int Function(Pointer<Void>, Pointer<Void>, int, int)
      >('zxv_vhub_add_limit');
  late final vhubNextEpoch = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Void>, Pointer<Uint64>),
        int Function(Pointer<Void>, Pointer<Void>, Pointer<Uint64>)
      >('zxv_vhub_next_epoch');
  late final vhubQuote = _l
      .lookupFunction<
        Int32 Function(
          Pointer<Void>,
          Pointer<Void>,
          Uint8,
          Uint8,
          Uint64,
          Pointer<Uint64>,
          Pointer<Uint64>,
          Pointer<Uint64>,
        ),
        int Function(Pointer<Void>, Pointer<Void>, int, int, int, Pointer<Uint64>, Pointer<Uint64>, Pointer<Uint64>)
      >('zxv_vhub_quote');
  late final vhubSwap = _l
      .lookupFunction<
        Int32 Function(
          Pointer<Void>,
          Pointer<Void>,
          Uint8,
          Uint8,
          Uint32,
          Uint32,
          Uint64,
          Uint64,
          Pointer<Uint64>,
          Pointer<Uint64>,
        ),
        int Function(Pointer<Void>, Pointer<Void>, int, int, int, int, int, int, Pointer<Uint64>, Pointer<Uint64>)
      >('zxv_vhub_swap');
  late final vhubCheck = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Void>, Pointer<Uint64>),
        int Function(Pointer<Void>, Pointer<Void>, Pointer<Uint64>)
      >('zxv_vhub_check');
  late final freightHeaderInit = _l
      .lookupFunction<
        Int32 Function(Uint64, Uint8, Uint8, Uint64, Pointer<Uint8>, Size, Pointer<Uint8>),
        int Function(int, int, int, int, Pointer<Uint8>, int, Pointer<Uint8>)
      >('zxv_freight_header_init');
  late final freightHeaderSerialize = _l
      .lookupFunction<
        Int32 Function(Uint8, Uint8, Uint16, Uint16, Uint16, Uint64, Uint64, Pointer<Uint8>, Pointer<Uint8>),
        int Function(int, int, int, int, int, int, int, Pointer<Uint8>, Pointer<Uint8>)
      >('zxv_freight_header_serialize');
  late final freightHeaderParse = _l
      .lookupFunction<
        Int32 Function(
          Pointer<Uint8>,
          Pointer<Uint8>,
          Pointer<Uint8>,
          Pointer<Uint8>,
          Pointer<Uint16>,
          Pointer<Uint16>,
          Pointer<Uint16>,
          Pointer<Uint64>,
          Pointer<Uint64>,
          Pointer<Uint8>,
        ),
        int Function(
          Pointer<Uint8>,
          Pointer<Uint8>,
          Pointer<Uint8>,
          Pointer<Uint8>,
          Pointer<Uint16>,
          Pointer<Uint16>,
          Pointer<Uint16>,
          Pointer<Uint64>,
          Pointer<Uint64>,
          Pointer<Uint8>,
        )
      >('zxv_freight_header_parse');
  late final freightHeaderCheck = _l.lookupFunction<Int32 Function(Pointer<Uint8>), int Function(Pointer<Uint8>)>(
    'zxv_freight_header_check',
  );
  late final freightEncode = _l
      .lookupFunction<
        Int32 Function(Pointer<Uint8>, Pointer<Uint8>, Size, Pointer<Uint8>),
        int Function(Pointer<Uint8>, Pointer<Uint8>, int, Pointer<Uint8>)
      >('zxv_freight_encode');
  late final freightDecode = _l
      .lookupFunction<
        Int32 Function(Pointer<Uint8>, Pointer<Uint8>, Size, Pointer<Uint8>, Size, Pointer<Size>),
        int Function(Pointer<Uint8>, Pointer<Uint8>, int, Pointer<Uint8>, int, Pointer<Size>)
      >('zxv_freight_decode');
  late final freightSetCount = _l
      .lookupFunction<Int32 Function(Uint64, Uint8, Pointer<Uint32>), int Function(int, int, Pointer<Uint32>)>(
        'zxv_freight_set_count',
      );
  late final freightSplit = _l
      .lookupFunction<
        Int32 Function(Uint64, Uint8, Uint8, Uint64, Pointer<Uint8>, Size, Uint32, Pointer<Uint8>, Pointer<Uint8>),
        int Function(int, int, int, int, Pointer<Uint8>, int, int, Pointer<Uint8>, Pointer<Uint8>)
      >('zxv_freight_split');
  late final freightJoinCreate = _l
      .lookupFunction<Int32 Function(Uint64, Pointer<Pointer<Void>>), int Function(int, Pointer<Pointer<Void>>)>(
        'zxv_freight_join_create',
      );
  late final freightJoinDestroy = _l.lookupFunction<Void Function(Pointer<Void>), void Function(Pointer<Void>)>(
    'zxv_freight_join_destroy',
  );
  late final freightJoinAdd = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Uint8>, Pointer<Uint8>, Size),
        int Function(Pointer<Void>, Pointer<Uint8>, Pointer<Uint8>, int)
      >('zxv_freight_join_add');
  late final freightJoinStatus = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Uint32>, Pointer<Uint32>, Pointer<Int32>, Pointer<Uint64>),
        int Function(Pointer<Void>, Pointer<Uint32>, Pointer<Uint32>, Pointer<Int32>, Pointer<Uint64>)
      >('zxv_freight_join_status');
  late final freightJoinResult = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Uint8>, Size, Pointer<Size>),
        int Function(Pointer<Void>, Pointer<Uint8>, int, Pointer<Size>)
      >('zxv_freight_join_result');
  late final freightOpsPeek = _l
      .lookupFunction<
        Int32 Function(Pointer<Uint8>, Size, Pointer<Uint64>, Pointer<Int32>),
        int Function(Pointer<Uint8>, int, Pointer<Uint64>, Pointer<Int32>)
      >('zxv_freight_ops_peek');
  late final freightOpsExpand = _l
      .lookupFunction<
        Int32 Function(
          Pointer<Uint8>,
          Size,
          Uint64,
          Pointer<NativeFunction<FreightResolveC>>,
          Pointer<Void>,
          Pointer<Uint8>,
          Size,
          Pointer<Size>,
        ),
        int Function(
          Pointer<Uint8>,
          int,
          int,
          Pointer<NativeFunction<FreightResolveC>>,
          Pointer<Void>,
          Pointer<Uint8>,
          int,
          Pointer<Size>,
        )
      >('zxv_freight_ops_expand');
  late final freightGenRun = _l
      .lookupFunction<
        Int32 Function(Uint8, Pointer<Uint8>, Size, Pointer<Uint8>, Size),
        int Function(int, Pointer<Uint8>, int, Pointer<Uint8>, int)
      >('zxv_freight_gen_run');
  late final freightOpsEncode = _l
      .lookupFunction<
        Int32 Function(Pointer<Uint8>, Size, Int32, Pointer<Uint8>, Size, Pointer<Size>),
        int Function(Pointer<Uint8>, int, int, Pointer<Uint8>, int, Pointer<Size>)
      >('zxv_freight_ops_encode');
  late final freightOpsBound = _l
      .lookupFunction<Int32 Function(Uint64, Int32, Pointer<Uint64>), int Function(int, int, Pointer<Uint64>)>(
        'zxv_freight_ops_bound',
      );
  late final freightOpsWriterCreate = _l
      .lookupFunction<
        Int32 Function(Uint64, Pointer<Uint8>, Uint64, Pointer<Pointer<Void>>),
        int Function(int, Pointer<Uint8>, int, Pointer<Pointer<Void>>)
      >('zxv_freight_ops_writer_create');
  late final freightOpsWriterDestroy = _l.lookupFunction<Void Function(Pointer<Void>), void Function(Pointer<Void>)>(
    'zxv_freight_ops_writer_destroy',
  );
  late final freightOpsPutLiteral = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Uint8>, Size),
        int Function(Pointer<Void>, Pointer<Uint8>, int)
      >('zxv_freight_ops_put_literal');
  late final freightOpsPutCopy = _l
      .lookupFunction<Int32 Function(Pointer<Void>, Uint64, Uint64), int Function(Pointer<Void>, int, int)>(
        'zxv_freight_ops_put_copy',
      );
  late final freightOpsPutCidref = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Uint8>, Size, Uint64),
        int Function(Pointer<Void>, Pointer<Uint8>, int, int)
      >('zxv_freight_ops_put_cidref');
  late final freightOpsPutSeed = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Uint8, Pointer<Uint8>, Size, Uint64),
        int Function(Pointer<Void>, int, Pointer<Uint8>, int, int)
      >('zxv_freight_ops_put_seed');
  late final freightOpsWriterFinish = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Uint8>, Size, Pointer<Size>),
        int Function(Pointer<Void>, Pointer<Uint8>, int, Pointer<Size>)
      >('zxv_freight_ops_writer_finish');
  late final w4AgentCreate = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Uint8>, Pointer<Utf8>, Pointer<Void>, Uint64, Pointer<Pointer<Void>>),
        int Function(Pointer<Void>, Pointer<Uint8>, Pointer<Utf8>, Pointer<Void>, int, Pointer<Pointer<Void>>)
      >('zxv_w4_agent_create');
  late final w4AgentDestroy = _l.lookupFunction<Void Function(Pointer<Void>), void Function(Pointer<Void>)>(
    'zxv_w4_agent_destroy',
  );
  late final w4AgentInfo = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Uint8>, Pointer<Uint8>, Pointer<Uint8>),
        int Function(Pointer<Void>, Pointer<Uint8>, Pointer<Uint8>, Pointer<Uint8>)
      >('zxv_w4_agent_info');
  late final w4AgentCard = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Uint8>, Size, Pointer<Size>),
        int Function(Pointer<Void>, Pointer<Uint8>, int, Pointer<Size>)
      >('zxv_w4_agent_card');
  late final w4CardVerify = _l
      .lookupFunction<
        Int32 Function(Pointer<Uint8>, Size, Pointer<Uint8>, Pointer<Uint8>, Pointer<Utf8>, Pointer<Uint64>),
        int Function(Pointer<Uint8>, int, Pointer<Uint8>, Pointer<Uint8>, Pointer<Utf8>, Pointer<Uint64>)
      >('zxv_w4_card_verify');
  late final w4DirCreate = _l
      .lookupFunction<Int32 Function(Uint32, Pointer<Pointer<Void>>), int Function(int, Pointer<Pointer<Void>>)>(
        'zxv_w4_dir_create',
      );
  late final w4DirDestroy = _l.lookupFunction<Void Function(Pointer<Void>), void Function(Pointer<Void>)>(
    'zxv_w4_dir_destroy',
  );
  late final w4DirAdd = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Uint8>, Size),
        int Function(Pointer<Void>, Pointer<Uint8>, int)
      >('zxv_w4_dir_add');
  late final w4DirFind = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Uint8>, Pointer<Uint8>, Size, Pointer<Size>),
        int Function(Pointer<Void>, Pointer<Uint8>, Pointer<Uint8>, int, Pointer<Size>)
      >('zxv_w4_dir_find');
  late final w4DirCount = _l
      .lookupFunction<Int32 Function(Pointer<Void>, Pointer<Uint32>), int Function(Pointer<Void>, Pointer<Uint32>)>(
        'zxv_w4_dir_count',
      );
  late final w4ManifestCreate = _l
      .lookupFunction<
        Int32 Function(Uint32, Uint64, Uint64, Pointer<Pointer<Void>>),
        int Function(int, int, int, Pointer<Pointer<Void>>)
      >('zxv_w4_manifest_create');
  late final w4ManifestDestroy = _l.lookupFunction<Void Function(Pointer<Void>), void Function(Pointer<Void>)>(
    'zxv_w4_manifest_destroy',
  );
  late final w4ManifestAddTool = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Utf8>, Uint64, Uint32, Uint32, Uint32, Uint32),
        int Function(Pointer<Void>, Pointer<Utf8>, int, int, int, int, int)
      >('zxv_w4_manifest_add_tool');
  late final w4ManifestSign = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Void>, Pointer<Void>),
        int Function(Pointer<Void>, Pointer<Void>, Pointer<Void>)
      >('zxv_w4_manifest_sign');
  late final w4ManifestVerify = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Uint8>, Uint64),
        int Function(Pointer<Void>, Pointer<Uint8>, int)
      >('zxv_w4_manifest_verify');
  late final w4ManifestEncode = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Uint8>, Size, Pointer<Size>),
        int Function(Pointer<Void>, Pointer<Uint8>, int, Pointer<Size>)
      >('zxv_w4_manifest_encode');
  late final w4ManifestDecode = _l
      .lookupFunction<
        Int32 Function(Pointer<Uint8>, Size, Pointer<Pointer<Void>>),
        int Function(Pointer<Uint8>, int, Pointer<Pointer<Void>>)
      >('zxv_w4_manifest_decode');
  late final w4ManifestToJson = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Utf8>, Size, Pointer<Size>),
        int Function(Pointer<Void>, Pointer<Utf8>, int, Pointer<Size>)
      >('zxv_w4_manifest_to_json');
  late final w4ManifestInfo = _l
      .lookupFunction<
        Int32 Function(
          Pointer<Void>,
          Pointer<Uint8>,
          Pointer<Uint8>,
          Pointer<Uint32>,
          Pointer<Uint64>,
          Pointer<Uint64>,
          Pointer<Uint32>,
          Pointer<Int32>,
        ),
        int Function(
          Pointer<Void>,
          Pointer<Uint8>,
          Pointer<Uint8>,
          Pointer<Uint32>,
          Pointer<Uint64>,
          Pointer<Uint64>,
          Pointer<Uint32>,
          Pointer<Int32>,
        )
      >('zxv_w4_manifest_info');
  late final w4ManifestTool = _l
      .lookupFunction<
        Int32 Function(
          Pointer<Void>,
          Uint32,
          Pointer<Utf8>,
          Pointer<Uint64>,
          Pointer<Uint32>,
          Pointer<Uint32>,
          Pointer<Uint32>,
          Pointer<Uint32>,
        ),
        int Function(
          Pointer<Void>,
          int,
          Pointer<Utf8>,
          Pointer<Uint64>,
          Pointer<Uint32>,
          Pointer<Uint32>,
          Pointer<Uint32>,
          Pointer<Uint32>,
        )
      >('zxv_w4_manifest_tool');
  late final w4ManifestFind = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Utf8>, Pointer<Uint32>),
        int Function(Pointer<Void>, Pointer<Utf8>, Pointer<Uint32>)
      >('zxv_w4_manifest_find');
  late final w4ActionDigest = _l
      .lookupFunction<
        Int32 Function(Pointer<Utf8>, Pointer<Uint8>, Pointer<Uint8>, Uint64, Pointer<Uint8>, Size, Pointer<Uint8>),
        int Function(Pointer<Utf8>, Pointer<Uint8>, Pointer<Uint8>, int, Pointer<Uint8>, int, Pointer<Uint8>)
      >('zxv_w4_action_digest');
  late final w4ConsentIssue = _l
      .lookupFunction<
        Int32 Function(
          Pointer<Void>,
          Pointer<Void>,
          Pointer<Uint8>,
          Pointer<Uint8>,
          Uint32,
          Uint64,
          Uint64,
          Uint64,
          Pointer<Uint8>,
          Pointer<Uint8>,
        ),
        int Function(
          Pointer<Void>,
          Pointer<Void>,
          Pointer<Uint8>,
          Pointer<Uint8>,
          int,
          int,
          int,
          int,
          Pointer<Uint8>,
          Pointer<Uint8>,
        )
      >('zxv_w4_consent_issue');
  late final w4ConsentInfo = _l
      .lookupFunction<
        Int32 Function(
          Pointer<Uint8>,
          Pointer<Uint8>,
          Pointer<Uint8>,
          Pointer<Uint8>,
          Pointer<Uint32>,
          Pointer<Uint64>,
          Pointer<Uint64>,
          Pointer<Uint64>,
          Pointer<Uint8>,
        ),
        int Function(
          Pointer<Uint8>,
          Pointer<Uint8>,
          Pointer<Uint8>,
          Pointer<Uint8>,
          Pointer<Uint32>,
          Pointer<Uint64>,
          Pointer<Uint64>,
          Pointer<Uint64>,
          Pointer<Uint8>,
        )
      >('zxv_w4_consent_info');
  late final w4ConsentLogCreate = _l
      .lookupFunction<Int32 Function(Uint32, Pointer<Pointer<Void>>), int Function(int, Pointer<Pointer<Void>>)>(
        'zxv_w4_consent_log_create',
      );
  late final w4ConsentLogDestroy = _l.lookupFunction<Void Function(Pointer<Void>), void Function(Pointer<Void>)>(
    'zxv_w4_consent_log_destroy',
  );
  late final w4ConsentCheck = _l
      .lookupFunction<
        Int32 Function(
          Pointer<Void>,
          Pointer<Uint8>,
          Pointer<Uint8>,
          Pointer<Uint8>,
          Pointer<Uint8>,
          Uint32,
          Uint64,
          Uint64,
        ),
        int Function(Pointer<Void>, Pointer<Uint8>, Pointer<Uint8>, Pointer<Uint8>, Pointer<Uint8>, int, int, int)
      >('zxv_w4_consent_check');
  late final w4ConsentRevoke = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Uint8>, Uint64),
        int Function(Pointer<Void>, Pointer<Uint8>, int)
      >('zxv_w4_consent_revoke');
  late final w4ConsentLogPrune = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Uint64, Pointer<Uint32>),
        int Function(Pointer<Void>, int, Pointer<Uint32>)
      >('zxv_w4_consent_log_prune');
  late final w4EnvSeal = _l
      .lookupFunction<
        Int32 Function(
          Pointer<Void>,
          Pointer<Void>,
          Int32,
          Pointer<Uint8>,
          Uint64,
          Uint32,
          Pointer<Utf8>,
          Uint32,
          Pointer<Uint8>,
          Size,
          Pointer<Uint8>,
          Uint64,
          Pointer<Uint8>,
          Size,
          Pointer<Size>,
        ),
        int Function(
          Pointer<Void>,
          Pointer<Void>,
          int,
          Pointer<Uint8>,
          int,
          int,
          Pointer<Utf8>,
          int,
          Pointer<Uint8>,
          int,
          Pointer<Uint8>,
          int,
          Pointer<Uint8>,
          int,
          Pointer<Size>,
        )
      >('zxv_w4_env_seal');
  late final w4EnvReply = _l
      .lookupFunction<
        Int32 Function(
          Pointer<Void>,
          Pointer<Void>,
          Pointer<Uint8>,
          Size,
          Int32,
          Uint32,
          Uint32,
          Pointer<Uint8>,
          Size,
          Uint64,
          Pointer<Uint8>,
          Size,
          Pointer<Size>,
        ),
        int Function(
          Pointer<Void>,
          Pointer<Void>,
          Pointer<Uint8>,
          int,
          int,
          int,
          int,
          Pointer<Uint8>,
          int,
          int,
          Pointer<Uint8>,
          int,
          Pointer<Size>,
        )
      >('zxv_w4_env_reply');
  late final w4EnvDecode = _l
      .lookupFunction<
        Int32 Function(
          Pointer<Uint8>,
          Size,
          Pointer<Int32>,
          Pointer<Uint32>,
          Pointer<Uint8>,
          Pointer<Uint8>,
          Pointer<Uint64>,
          Pointer<Uint64>,
          Pointer<Uint64>,
          Pointer<Uint32>,
          Pointer<Utf8>,
          Pointer<Uint32>,
        ),
        int Function(
          Pointer<Uint8>,
          int,
          Pointer<Int32>,
          Pointer<Uint32>,
          Pointer<Uint8>,
          Pointer<Uint8>,
          Pointer<Uint64>,
          Pointer<Uint64>,
          Pointer<Uint64>,
          Pointer<Uint32>,
          Pointer<Utf8>,
          Pointer<Uint32>,
        )
      >('zxv_w4_env_decode');
  late final w4EnvPayload = _l
      .lookupFunction<
        Int32 Function(Pointer<Uint8>, Size, Pointer<Uint8>, Size, Pointer<Size>),
        int Function(Pointer<Uint8>, int, Pointer<Uint8>, int, Pointer<Size>)
      >('zxv_w4_env_payload');
  late final w4EnvConsent = _l
      .lookupFunction<
        Int32 Function(Pointer<Uint8>, Size, Pointer<Uint8>),
        int Function(Pointer<Uint8>, int, Pointer<Uint8>)
      >('zxv_w4_env_consent');
  late final w4EnvVerify = _l
      .lookupFunction<
        Int32 Function(Pointer<Uint8>, Size, Pointer<Uint8>),
        int Function(Pointer<Uint8>, int, Pointer<Uint8>)
      >('zxv_w4_env_verify');
  late final paradoxSignature = _l
      .lookupFunction<
        Int32 Function(Pointer<Uint8>, Size, Pointer<Uint8>),
        int Function(Pointer<Uint8>, int, Pointer<Uint8>)
      >('zxv_paradox_signature');
  late final paradoxKindOfTag = _l
      .lookupFunction<Int32 Function(Uint8, Pointer<Int32>), int Function(int, Pointer<Int32>)>(
        'zxv_paradox_kind_of_tag',
      );
  late final paradoxAction = _l
      .lookupFunction<Int32 Function(Int32, Uint8, Pointer<Int32>), int Function(int, int, Pointer<Int32>)>(
        'zxv_paradox_action',
      );
  late final paradoxOutcomeAction = _l
      .lookupFunction<Int32 Function(Int32, Pointer<Int32>), int Function(int, Pointer<Int32>)>(
        'zxv_paradox_outcome_action',
      );
  late final paradoxRepairVerify = _l.lookupFunction<Int32 Function(Pointer<Uint8>), int Function(Pointer<Uint8>)>(
    'zxv_paradox_repair_verify',
  );
  late final paradoxKindName = _l.lookupFunction<Pointer<Utf8> Function(Int32), Pointer<Utf8> Function(int)>(
    'zxv_paradox_kind_name',
  );
  late final paradoxOutcomeName = _l.lookupFunction<Pointer<Utf8> Function(Int32), Pointer<Utf8> Function(int)>(
    'zxv_paradox_outcome_name',
  );
  late final paradoxNodeStateName = _l.lookupFunction<Pointer<Utf8> Function(Int32), Pointer<Utf8> Function(int)>(
    'zxv_paradox_node_state_name',
  );
  late final paradoxCreate = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Uint32, Uint32, Pointer<Pointer<Void>>),
        int Function(Pointer<Void>, int, int, Pointer<Pointer<Void>>)
      >('zxv_paradox_create');
  late final paradoxDestroy = _l.lookupFunction<Void Function(Pointer<Void>), void Function(Pointer<Void>)>(
    'zxv_paradox_destroy',
  );
  late final paradoxClassify = _l
      .lookupFunction<
        Int32 Function(
          Pointer<Void>,
          Uint8,
          Pointer<Uint32>,
          Int32,
          Pointer<Uint8>,
          Pointer<Int32>,
          Pointer<Uint8>,
          Pointer<Int32>,
        ),
        int Function(
          Pointer<Void>,
          int,
          Pointer<Uint32>,
          int,
          Pointer<Uint8>,
          Pointer<Int32>,
          Pointer<Uint8>,
          Pointer<Int32>,
        )
      >('zxv_paradox_classify');
  late final paradoxRegistryAdd = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Void>, Pointer<Uint8>, Int32, Uint8, Uint8, Int32, Pointer<Uint32>),
        int Function(Pointer<Void>, Pointer<Void>, Pointer<Uint8>, int, int, int, int, Pointer<Uint32>)
      >('zxv_paradox_registry_add');
  late final paradoxRegistryFind = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Uint8>, Pointer<Uint32>),
        int Function(Pointer<Void>, Pointer<Uint8>, Pointer<Uint32>)
      >('zxv_paradox_registry_find');
  late final paradoxRegistryCount = _l
      .lookupFunction<Int32 Function(Pointer<Void>, Pointer<Uint32>), int Function(Pointer<Void>, Pointer<Uint32>)>(
        'zxv_paradox_registry_count',
      );
  late final paradoxRegistryAt = _l
      .lookupFunction<
        Int32 Function(
          Pointer<Void>,
          Uint32,
          Pointer<Uint8>,
          Pointer<Int32>,
          Pointer<Uint8>,
          Pointer<Uint8>,
          Pointer<Uint64>,
        ),
        int Function(
          Pointer<Void>,
          int,
          Pointer<Uint8>,
          Pointer<Int32>,
          Pointer<Uint8>,
          Pointer<Uint8>,
          Pointer<Uint64>,
        )
      >('zxv_paradox_registry_at');
  late final paradoxLogCount = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Int32, Pointer<Uint32>),
        int Function(Pointer<Void>, int, Pointer<Uint32>)
      >('zxv_paradox_log_count');
  late final paradoxLogHead = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Int32, Pointer<Uint8>),
        int Function(Pointer<Void>, int, Pointer<Uint8>)
      >('zxv_paradox_log_head');
  late final paradoxLogRecord = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Int32, Uint32, Pointer<Uint8>, Size, Pointer<Size>),
        int Function(Pointer<Void>, int, int, Pointer<Uint8>, int, Pointer<Size>)
      >('zxv_paradox_log_record');
  late final paradoxLogVerify = _l
      .lookupFunction<Int32 Function(Pointer<Void>, Int32), int Function(Pointer<Void>, int)>('zxv_paradox_log_verify');
  late final paradoxPolicyCreate = _l
      .lookupFunction<
        Int32 Function(
          Pointer<Void>,
          Uint32,
          Pointer<Int64>,
          Pointer<Int64>,
          Pointer<Int64>,
          Uint32,
          Pointer<Pointer<Void>>,
        ),
        int Function(Pointer<Void>, int, Pointer<Int64>, Pointer<Int64>, Pointer<Int64>, int, Pointer<Pointer<Void>>)
      >('zxv_paradox_policy_create');
  late final paradoxPolicyDestroy = _l.lookupFunction<Void Function(Pointer<Void>), void Function(Pointer<Void>)>(
    'zxv_paradox_policy_destroy',
  );
  late final paradoxConstructive = _l
      .lookupFunction<
        Int32 Function(
          Pointer<Void>,
          Pointer<Void>,
          Pointer<Void>,
          Int32,
          Uint8,
          Pointer<Int64>,
          Uint32,
          Uint32,
          Pointer<Int32>,
        ),
        int Function(Pointer<Void>, Pointer<Void>, Pointer<Void>, int, int, Pointer<Int64>, int, int, Pointer<Int32>)
      >('zxv_paradox_constructive');
  late final paradoxPolicyValues = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Int64>, Pointer<Int64>, Uint32, Pointer<Int32>, Pointer<Uint32>),
        int Function(Pointer<Void>, Pointer<Int64>, Pointer<Int64>, int, Pointer<Int32>, Pointer<Uint32>)
      >('zxv_paradox_policy_values');
  late final paradoxPolicyBounds = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Int64>, Pointer<Int64>, Uint32, Pointer<Uint32>),
        int Function(Pointer<Void>, Pointer<Int64>, Pointer<Int64>, int, Pointer<Uint32>)
      >('zxv_paradox_policy_bounds');
  late final paradoxPolicyProposal = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Int64>, Uint32, Pointer<Uint8>, Size, Pointer<Size>),
        int Function(Pointer<Void>, Pointer<Int64>, int, Pointer<Uint8>, int, Pointer<Size>)
      >('zxv_paradox_policy_proposal');
  late final paradoxDestructive = _l
      .lookupFunction<
        Int32 Function(
          Pointer<Void>,
          Pointer<Void>,
          Int32,
          Uint8,
          Uint32,
          Uint32,
          Uint32,
          Uint32,
          Pointer<Uint64>,
          Uint32,
          Int32,
          Pointer<Uint8>,
          Pointer<Uint8>,
          Uint32,
          Pointer<Uint8>,
          Uint32,
          Uint32,
          Pointer<Int32>,
          Pointer<Uint8>,
        ),
        int Function(
          Pointer<Void>,
          Pointer<Void>,
          int,
          int,
          int,
          int,
          int,
          int,
          Pointer<Uint64>,
          int,
          int,
          Pointer<Uint8>,
          Pointer<Uint8>,
          int,
          Pointer<Uint8>,
          int,
          int,
          Pointer<Int32>,
          Pointer<Uint8>,
        )
      >('zxv_paradox_destructive');
  late final paradoxHoldCreate = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Uint32, Uint32, Pointer<Pointer<Void>>),
        int Function(Pointer<Void>, int, int, Pointer<Pointer<Void>>)
      >('zxv_paradox_hold_create');
  late final paradoxHoldDestroy = _l.lookupFunction<Void Function(Pointer<Void>), void Function(Pointer<Void>)>(
    'zxv_paradox_hold_destroy',
  );
  late final paradoxHoldAdd = _l
      .lookupFunction<Int32 Function(Pointer<Void>, Pointer<Uint8>), int Function(Pointer<Void>, Pointer<Uint8>)>(
        'zxv_paradox_hold_add',
      );
  late final paradoxHoldEvidence = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Uint8>, Int32),
        int Function(Pointer<Void>, Pointer<Uint8>, int)
      >('zxv_paradox_hold_evidence');
  late final paradoxHoldInfo = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Uint32>, Pointer<Int32>, Pointer<Int32>),
        int Function(Pointer<Void>, Pointer<Uint32>, Pointer<Int32>, Pointer<Int32>)
      >('zxv_paradox_hold_info');
  late final paradoxHoldBranch = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Uint32, Pointer<Uint8>, Pointer<Uint32>, Pointer<Uint32>),
        int Function(Pointer<Void>, int, Pointer<Uint8>, Pointer<Uint32>, Pointer<Uint32>)
      >('zxv_paradox_hold_branch');
  late final paradoxNeutral = _l
      .lookupFunction<
        Int32 Function(
          Pointer<Void>,
          Pointer<Void>,
          Pointer<Void>,
          Int32,
          Uint8,
          Int32,
          Pointer<Int32>,
          Pointer<Int32>,
        ),
        int Function(Pointer<Void>, Pointer<Void>, Pointer<Void>, int, int, int, Pointer<Int32>, Pointer<Int32>)
      >('zxv_paradox_neutral');
  late final paradoxSpecial = _l
      .lookupFunction<
        Int32 Function(
          Pointer<Void>,
          Pointer<Void>,
          Int32,
          Uint8,
          Pointer<Uint8>,
          Pointer<Uint8>,
          Pointer<Int32>,
          Pointer<Uint32>,
        ),
        int Function(
          Pointer<Void>,
          Pointer<Void>,
          int,
          int,
          Pointer<Uint8>,
          Pointer<Uint8>,
          Pointer<Int32>,
          Pointer<Uint32>,
        )
      >('zxv_paradox_special');
  late final paradoxDraft = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Void>, Uint32, Int32, Uint8, Uint8, Pointer<Uint8>),
        int Function(Pointer<Void>, Pointer<Void>, int, int, int, int, Pointer<Uint8>)
      >('zxv_paradox_draft');
  late final paradoxReview = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Void>, Pointer<Uint8>, Int32, Int32, Pointer<Int32>),
        int Function(Pointer<Void>, Pointer<Void>, Pointer<Uint8>, int, int, Pointer<Int32>)
      >('zxv_paradox_review');
  late final paradoxGraphCreate = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Uint32, Pointer<Pointer<Void>>),
        int Function(Pointer<Void>, int, Pointer<Pointer<Void>>)
      >('zxv_paradox_graph_create');
  late final paradoxGraphDestroy = _l.lookupFunction<Void Function(Pointer<Void>), void Function(Pointer<Void>)>(
    'zxv_paradox_graph_destroy',
  );
  late final paradoxGraphAddNode = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Uint8>, Int32, Uint8, Uint32, Int32),
        int Function(Pointer<Void>, Pointer<Uint8>, int, int, int, int)
      >('zxv_paradox_graph_add_node');
  late final paradoxGraphAddDep = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Uint8>, Pointer<Uint8>),
        int Function(Pointer<Void>, Pointer<Uint8>, Pointer<Uint8>)
      >('zxv_paradox_graph_add_dep');
  late final paradoxGraphUpdate = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Uint8>, Int32, Uint8, Uint32, Int32),
        int Function(Pointer<Void>, Pointer<Uint8>, int, int, int, int)
      >('zxv_paradox_graph_update');
  late final paradoxGraphPass = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Void>, Pointer<Void>, Pointer<Int32>),
        int Function(Pointer<Void>, Pointer<Void>, Pointer<Void>, Pointer<Int32>)
      >('zxv_paradox_graph_pass');
  late final paradoxGraphState = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Uint8>, Pointer<Int32>),
        int Function(Pointer<Void>, Pointer<Uint8>, Pointer<Int32>)
      >('zxv_paradox_graph_state');
  late final paradoxGraphNodeAt = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Uint32, Pointer<Uint8>, Pointer<Int32>, Pointer<Int32>),
        int Function(Pointer<Void>, int, Pointer<Uint8>, Pointer<Int32>, Pointer<Int32>)
      >('zxv_paradox_graph_node_at');
  late final paradoxGraphInfo = _l
      .lookupFunction<
        Int32 Function(
          Pointer<Void>,
          Pointer<Uint32>,
          Pointer<Uint32>,
          Pointer<Uint32>,
          Pointer<Int32>,
          Pointer<Int32>,
          Pointer<Uint8>,
        ),
        int Function(
          Pointer<Void>,
          Pointer<Uint32>,
          Pointer<Uint32>,
          Pointer<Uint32>,
          Pointer<Int32>,
          Pointer<Int32>,
          Pointer<Uint8>,
        )
      >('zxv_paradox_graph_info');
}

Zxv15Lib? _z15;

/// The 1.5 entry points; loads the library on first use.
Zxv15Lib get zxv15 => _z15 ??= Zxv15Lib(zxvLib.lib);
