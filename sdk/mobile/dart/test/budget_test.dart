// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
import 'dart:async';

import 'package:test/test.dart';
import 'package:zxv/zxv.dart';

void main() {
  late ZxvContext ctx;
  late Budgets budgets;
  setUp(() {
    ctx = ZxvContext.create();
    budgets = Budgets.create(ctx.rootCapability({Permission.BUDGET}));
  });
  tearDown(() => ctx.close());

  test('consume, release and exhaustion', () {
    final tokens = budgets.budget('tokens', Resource.tokens, Rational.of(10));
    var denied = 0;
    final stop = tokens.onExhausted((_, _) => denied++);
    tokens.consume(Rational.of(7, 2));
    expect(tokens.remaining, Rational.of(13, 2));
    expect(
      () => tokens.consume(Rational.of(7)),
      throwsA(isA<BudgetExhaustedException>().having((x) => x.status, 'status', -19)),
    );
    expect(denied, 1);
    expect(tokens.tryConsume(Rational.of(7)), isFalse);
    stop();
    expect(tokens.used, Rational.of(7, 2));
    expect(
      () => tokens.consume(Rational.of(-1)),
      throwsA(isA<ZxvUsageException>().having((x) => x.status, 'status', -20)),
    );
    final e0 = tokens.status().epoch;
    tokens.release(Rational.one);
    expect(tokens.status().epoch, greaterThan(e0));
    tokens.consume(Rational.of(7));
  });

  test('budgets need BUDGET', () {
    final c = ZxvContext.create();
    addTearDown(c.close);
    expect(() => Budgets.create(c.rootCapability({Permission.LEDGER_READ})), throwsA(isA<CapabilityDeniedException>()));
  });

  test('consumeAsync waits for a release, then completes', () async {
    final b = budgets.budget('energy', Resource.energy, Rational.one);
    b.consume(Rational.one);
    var done = false;
    final f = b.consumeAsync(Rational.one, timeout: const Duration(seconds: 5)).then((_) => done = true);
    await Future<void>.delayed(const Duration(milliseconds: 20));
    expect(done, isFalse);
    b.release(Rational.one);
    await f;
    expect(b.used, Rational.one);
    final f2 = b.consumeAsync(Rational.of(2));
    await Future<void>.delayed(const Duration(milliseconds: 10));
    b.setLimit(Rational.of(3));
    await f2;
    expect(b.used, Rational.of(3));
  });

  test('consumeAsync times out, cancels, and fails fast on bad amounts', () async {
    final b = budgets.budget('cpu', Resource.computeCycles, Rational.one);
    await expectLater(
      b.consumeAsync(Rational.of(2), timeout: const Duration(milliseconds: 30)),
      throwsA(isA<TimeoutException>()),
    );
    final token = CancelToken();
    final f = b.consumeAsync(Rational.of(2), cancel: token);
    token.cancel();
    await expectLater(f, throwsA(isA<BudgetWaitCancelled>()));
    await expectLater(b.consumeAsync(Rational.of(-1)), throwsA(isA<ZxvUsageException>()));
    expect(b.used, Rational.zero);
  });
}
