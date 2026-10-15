// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

// dart:ffi declarations of the ABI 1.6 group "vino" (Vino Embed: the play
// lane, payment requests, receipts and the daily meter), with the exact C
// types of sdk/libzxv/include/zxv_api.h. Internal.
//
// Checked against the header by test/abi_drift_test.dart like bindings.dart
// and bindings15.dart. Lookups are lazy (`late final`). uint32 parameters
// are declared Uint32 and every caller range-checks first (native.dart u32):
// Dart truncates an out-of-range int. uint64 values are Dart ints with their
// bits kept (a value at or above 2^63 reads as negative).
import 'dart:ffi';

import 'package:ffi/ffi.dart';

import 'bindings.dart';

/// The ABI 1.6 vino entry points of the loaded library.
final class Zxv16Lib {
  Zxv16Lib(this._l);
  final DynamicLibrary _l;

  // ---- the play lane ----
  late final vinoPlayCreate = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Pointer<Void>>),
        int Function(Pointer<Void>, Pointer<Pointer<Void>>)
      >('zxv_vino_play_create');
  late final vinoPlayDestroy = _l.lookupFunction<Void Function(Pointer<Void>), void Function(Pointer<Void>)>(
    'zxv_vino_play_destroy',
  );
  late final vinoPlayOpen = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Void>, Uint32, Int32, Pointer<Uint32>),
        int Function(Pointer<Void>, Pointer<Void>, int, int, Pointer<Uint32>)
      >('zxv_vino_play_open');
  late final vinoPlayBack = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Void>, Uint32, Uint64),
        int Function(Pointer<Void>, Pointer<Void>, int, int)
      >('zxv_vino_play_back');
  late final vinoPlayUnback = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Void>, Uint32, Uint64),
        int Function(Pointer<Void>, Pointer<Void>, int, int)
      >('zxv_vino_play_unback');
  late final vinoIssue = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Void>, Uint32, Uint32, Uint64, Int32, Pointer<Uint64>),
        int Function(Pointer<Void>, Pointer<Void>, int, int, int, int, Pointer<Uint64>)
      >('zxv_vino_issue');
  late final vinoTransfer = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Void>, Uint64, Uint32, Uint32, Int32),
        int Function(Pointer<Void>, Pointer<Void>, int, int, int, int)
      >('zxv_vino_transfer');
  late final vinoSetRail = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Void>, Uint64, Uint32, Int32),
        int Function(Pointer<Void>, Pointer<Void>, int, int, int)
      >('zxv_vino_set_rail');
  late final vinoRedeem = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Void>, Uint64, Uint32),
        int Function(Pointer<Void>, Pointer<Void>, int, int)
      >('zxv_vino_redeem');
  late final vinoVoucherInfo = _l
      .lookupFunction<
        Int32 Function(
          Pointer<Void>,
          Pointer<Void>,
          Uint64,
          Pointer<Uint64>,
          Pointer<Uint32>,
          Pointer<Uint32>,
          Pointer<Int32>,
          Pointer<Int32>,
        ),
        int Function(
          Pointer<Void>,
          Pointer<Void>,
          int,
          Pointer<Uint64>,
          Pointer<Uint32>,
          Pointer<Uint32>,
          Pointer<Int32>,
          Pointer<Int32>,
        )
      >('zxv_vino_voucher_info');
  late final vinoAccountInfo = _l
      .lookupFunction<
        Int32 Function(
          Pointer<Void>,
          Pointer<Void>,
          Uint32,
          Pointer<Uint32>,
          Pointer<Int32>,
          Pointer<Uint64>,
          Pointer<Uint64>,
          Pointer<Uint32>,
        ),
        int Function(
          Pointer<Void>,
          Pointer<Void>,
          int,
          Pointer<Uint32>,
          Pointer<Int32>,
          Pointer<Uint64>,
          Pointer<Uint64>,
          Pointer<Uint32>,
        )
      >('zxv_vino_account_info');
  late final vinoVouchers = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Void>, Uint32, Pointer<Uint64>, Size, Pointer<Size>),
        int Function(Pointer<Void>, Pointer<Void>, int, Pointer<Uint64>, int, Pointer<Size>)
      >('zxv_vino_vouchers');
  late final vinoPlayCheck = _l
      .lookupFunction<Int32 Function(Pointer<Void>, Pointer<Void>), int Function(Pointer<Void>, Pointer<Void>)>(
        'zxv_vino_play_check',
      );

  // ---- payment requests (pure) ----
  late final vinoRequestEncode = _l
      .lookupFunction<
        Int32 Function(
          Int32,
          Uint32,
          Pointer<Uint8>,
          Pointer<Uint8>,
          Uint64,
          Uint64,
          Uint64,
          Pointer<Utf8>,
          Pointer<Uint8>,
          Pointer<Uint8>,
        ),
        int Function(
          int,
          int,
          Pointer<Uint8>,
          Pointer<Uint8>,
          int,
          int,
          int,
          Pointer<Utf8>,
          Pointer<Uint8>,
          Pointer<Uint8>,
        )
      >('zxv_vino_request_encode');
  late final vinoRequestDecode = _l
      .lookupFunction<
        Int32 Function(
          Pointer<Uint8>,
          Pointer<Int32>,
          Pointer<Uint32>,
          Pointer<Uint8>,
          Pointer<Uint8>,
          Pointer<Uint64>,
          Pointer<Uint64>,
          Pointer<Uint64>,
          Pointer<Utf8>,
          Pointer<Uint8>,
        ),
        int Function(
          Pointer<Uint8>,
          Pointer<Int32>,
          Pointer<Uint32>,
          Pointer<Uint8>,
          Pointer<Uint8>,
          Pointer<Uint64>,
          Pointer<Uint64>,
          Pointer<Uint64>,
          Pointer<Utf8>,
          Pointer<Uint8>,
        )
      >('zxv_vino_request_decode');

  // ---- receipts ----
  late final vinoReceiptMsg = _l
      .lookupFunction<
        Int32 Function(Pointer<Uint8>, Pointer<Uint8>, Uint64, Pointer<Uint8>),
        int Function(Pointer<Uint8>, Pointer<Uint8>, int, Pointer<Uint8>)
      >('zxv_vino_receipt_msg');
  late final vinoReceiptSign = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Uint8>, Pointer<Uint8>, Pointer<Uint8>, Uint64, Pointer<Uint8>),
        int Function(Pointer<Void>, Pointer<Uint8>, Pointer<Uint8>, Pointer<Uint8>, int, Pointer<Uint8>)
      >('zxv_vino_receipt_sign');
  late final vinoReceiptVerify = _l
      .lookupFunction<
        Int32 Function(Pointer<Uint8>, Pointer<Uint8>, Uint64, Pointer<Uint8>, Uint64),
        int Function(Pointer<Uint8>, Pointer<Uint8>, int, Pointer<Uint8>, int)
      >('zxv_vino_receipt_verify');

  // ---- the daily meter ----
  late final vinoMeterCreate = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Uint64, Pointer<Pointer<Void>>),
        int Function(Pointer<Void>, int, Pointer<Pointer<Void>>)
      >('zxv_vino_meter_create');
  late final vinoMeterDestroy = _l.lookupFunction<Void Function(Pointer<Void>), void Function(Pointer<Void>)>(
    'zxv_vino_meter_destroy',
  );
  late final vinoMeterAdmit = _l
      .lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Void>, Uint64, Uint64, Pointer<Uint64>),
        int Function(Pointer<Void>, Pointer<Void>, int, int, Pointer<Uint64>)
      >('zxv_vino_meter_admit');
}

Zxv16Lib? _z16;

/// The 1.6 vino entry points; loads the library on first use.
Zxv16Lib get zxv16 => _z16 ??= Zxv16Lib(zxvLib.lib);
