// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

/// The ZXV SDK for Dart and Flutter, over libzxv (ABI 1.3) with dart:ffi.
///
/// ```dart
/// final ctx = ZxvContext.create();
/// final root = ctx.rootCapability(Permission.values.toSet());
/// ctx.enableCurrency(root, 'NGN');
/// final ledger = Ledger(root);
/// ...
/// ctx.close(); // closes root and everything else made from ctx
/// ```
library;

export 'src/budgets.dart' show Budgets, Budget, BudgetStatus, Resource, CancelToken, BudgetWaitCancelled;
export 'src/context.dart' show ZxvContext, Capability, CapabilityInfo, Permission, FatalPolicy;
export 'src/crypto.dart' show Hashes, SecretKey, Ed25519, Ed25519KeyPair;
export 'src/errors.dart';
export 'src/hexagram.dart' show Hexagram, HexBase, HexPolarity, HexPhase, HexAction;
export 'src/ledger.dart' show Ledger, Account, AccountKind, Balance, JournalEntry;
export 'src/quarantine_log.dart' show QuarantineLog, QuarantineRecord;
export 'src/rational.dart' show Rational;
export 'src/truth.dart' show Truth;
export 'src/zxv.dart' show Zxv, ZxvVersion, Feature;
