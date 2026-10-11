// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

// dart run example/main.dart   (with ZXV_LIBRARY_PATH set on a desktop)
import 'package:zxv/zxv.dart';

void main() {
  final ctx = ZxvContext.create();
  try {
    final root = ctx.rootCapability(Permission.all);
    ctx.enableCurrency(root, 'NGN');
    final ledger = Ledger(root);
    final bank = ledger.openAccount('bank', 'NGN', AccountKind.issuer);
    final alice = ledger.openAccount('alice', 'NGN');
    ledger.post(bank, alice, 12550, 'NGN', 'top-up-1');
    print('alice: ${ledger.balance(alice)}');

    final fee = Rational.parse('0.015') * Rational.of(12550);
    print('fee: $fee (exact)');

    final tag = Hexagram.of(HexBase.TRUE, HexPolarity.NEGENTROPIC, HexPhase.POTENTIAL);
    print('${tag.name} -> ${tag.action.actionName}');

    final reader = root.attenuate({Permission.LEDGER_READ});
    try {
      Ledger(reader).post(alice, bank, 1, 'NGN', 'nope');
    } on CapabilityDeniedException catch (e) {
      print('reader cannot post: ${e.statusName}');
    }
  } finally {
    ctx.close();
  }
}
