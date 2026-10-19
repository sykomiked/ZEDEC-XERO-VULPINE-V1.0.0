// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:zxv_companion/src/ui/zxv_bar.dart';

Widget _host(Widget w) => MaterialApp(home: Scaffold(body: SizedBox(width: 200, child: w)));

void main() {
  testWidgets('a value shows as a percentage', (t) async {
    await t.pumpWidget(_host(const ZxvBar(value: 0.25)));
    expect(find.bySemanticsLabel('Progress'), findsOneWidget);
    expect(t.getSemantics(find.byType(ZxvBar)).value, '25%');
  });

  testWidgets('no value keeps running, and stops once a value arrives', (t) async {
    await t.pumpWidget(_host(const ZxvBar()));
    expect(t.getSemantics(find.byType(ZxvBar)).value, 'working');
    await t.pump(const Duration(milliseconds: 700));
    expect(t.binding.hasScheduledFrame, isTrue);
    await t.pumpWidget(_host(const ZxvBar(value: 1)));
    await t.pumpAndSettle();
    expect(t.getSemantics(find.byType(ZxvBar)).value, '100%');
  });
}
