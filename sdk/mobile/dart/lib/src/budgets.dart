// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
import 'dart:async';
import 'dart:ffi';

import 'package:ffi/ffi.dart';

import 'bindings.dart';
import 'context.dart';
import 'errors.dart';
import 'native.dart' as n;
import 'rational.dart';

/// What a budget measures (`ZXV_RES_*`).
enum Resource {
  computeCycles(0),
  memory(1),
  bandwidth(2),
  energy(3),
  storage(4),

  /// One model token = one event.
  tokens(12);

  const Resource(this.code);
  final int code;
}

/// Cancels a pending [Budget.consumeAsync].
final class CancelToken {
  final Completer<void> _c = Completer<void>();
  bool get isCancelled => _c.isCompleted;
  void cancel() {
    if (!_c.isCompleted) _c.complete();
  }
}

/// [Budget.consumeAsync] was cancelled through its [CancelToken].
final class BudgetWaitCancelled implements Exception {
  const BudgetWaitCancelled();
  @override
  String toString() => 'BudgetWaitCancelled';
}

/// Remaining, used, and the replenishment epoch libzxv reports.
final class BudgetStatus {
  const BudgetStatus(this.remaining, this.used, this.epoch);
  final Rational remaining, used;
  final int epoch;
}

/// A registry of exact rational resource budgets (`zxv_rmag`) for one
/// context. Every call passes [capability], which needs `BUDGET`. Closed
/// with its context, or by [close].
final class Budgets {
  Budgets._(this.capability, Pointer<Void> p) {
    handle = n.NativeHandle(
      'budget registry',
      p,
      zxvLib.rmagDestroy,
      capability.context.state,
      capability.context.handle,
    );
    n.handleFinalizer.attach(this, handle, detach: this);
  }

  final Capability capability;
  late final n.NativeHandle handle;

  // Fires after every release or limit change made through this registry,
  // which owns its rmag handle, so every replenishment passes here.
  final StreamController<void> _replenished = StreamController<void>.broadcast(sync: true);

  n.ContextState get _st => capability.context.state;

  /// A new registry. Needs `BUDGET`.
  factory Budgets.create(Capability cap) => using((a) {
    final o = a<Pointer<Void>>();
    n.check(zxvLib.rmagCreateCap(cap.handle.ptr, o), cap.context.state);
    return Budgets._(cap, o.value);
  });

  /// A budget with an exact limit (>= 0). `enforce: false` only counts.
  Budget budget(String name, Resource resource, Rational limit, {bool enforce = true}) => using((a) {
    final o = a<Uint32>();
    n.check(
      zxvLib.rmagBudget(
        capability.handle.ptr,
        handle.ptr,
        n.cstr(a, name, 'name'),
        resource.code,
        limit.numerator,
        limit.denominator,
        enforce ? 1 : 0,
        o,
      ),
      _st,
    );
    return Budget._(this, o.value, name, resource);
  });

  void close() {
    n.handleFinalizer.detach(this);
    handle.close();
    _replenished.close();
  }
}

/// One budget in a [Budgets] registry.
final class Budget {
  Budget._(this.registry, this.index, this.name, this.resource);

  final Budgets registry;
  final int index;
  final String name;
  final Resource resource;
  final List<void Function(Budget, Rational)> _listeners = [];

  int _call(int Function(Pointer<Void>, Pointer<Void>, int, int, int) f, Rational r) =>
      f(registry.capability.handle.ptr, registry.handle.ptr, index, r.numerator, r.denominator);

  /// Consumes `amount` (> 0). [BudgetExhaustedException] (nothing consumed)
  /// or a USAGE error for an invalid amount (`ZXV_E_RMAG_INVALID`).
  void consume(Rational amount) {
    final rc = _call(zxvLib.rmagConsume, amount);
    if (rc == -19) {
      for (final l in List.of(_listeners)) {
        l(this, amount);
      }
    }
    n.check(rc, registry._st);
  }

  /// Consumes if it fits; false (nothing consumed) on exhaustion.
  bool tryConsume(Rational amount) {
    try {
      consume(amount);
      return true;
    } on BudgetExhaustedException {
      return false;
    }
  }

  /// Completes once `amount` is consumed: on exhaustion it waits for a
  /// release or a larger limit made through this registry, then retries.
  /// Fails at once on an invalid amount or any other error; `timeout`
  /// bounds the whole wait ([TimeoutException]); `cancel` stops it
  /// ([BudgetWaitCancelled]).
  Future<void> consumeAsync(Rational amount, {Duration? timeout, CancelToken? cancel}) async {
    final deadline = timeout == null ? null : DateTime.now().add(timeout);
    while (true) {
      if (cancel?.isCancelled ?? false) throw const BudgetWaitCancelled();
      // Subscribe BEFORE trying, so a release between a refused consume and
      // the wait is never missed.
      final next = registry._replenished.stream.first;
      try {
        consume(amount);
        unawaited(next.then((_) {}, onError: (_) {}));
        return;
      } on BudgetExhaustedException {
        final waits = <Future<Object?>>[next.then((_) => null)];
        if (cancel != null) waits.add(cancel._c.future.then((_) => const BudgetWaitCancelled()));
        if (deadline != null) {
          final left = deadline.difference(DateTime.now());
          waits.add(
            Future<Object?>.delayed(
              left.isNegative ? Duration.zero : left,
              () => TimeoutException('budget $name not replenished', timeout),
            ),
          );
        }
        final r = await Future.any(waits);
        if (r is Exception) throw r;
      }
    }
  }

  /// Gives back `amount` (clamped at zero used) and wakes waiters.
  void release(Rational amount) {
    n.check(_call(zxvLib.rmagRelease, amount), registry._st);
    registry._replenished.add(null);
  }

  /// Replaces the limit (>= 0) and wakes waiters.
  void setLimit(Rational limit) {
    n.check(_call(zxvLib.rmagSetLimit, limit), registry._st);
    registry._replenished.add(null);
  }

  BudgetStatus status() => using((a) {
    final o = a<Int64>(4);
    final ep = a<Uint64>();
    n.check(zxvLib.rmagStatus(registry.handle.ptr, index, o, o + 1, o + 2, o + 3, ep), registry._st);
    return BudgetStatus(Rational.of(o[0], o[1]), Rational.of(o[2], o[3]), ep.value);
  });

  Rational get remaining => status().remaining;
  Rational get used => status().used;

  /// Calls `handler` for every refused consume, before the exception is thrown.
  /// Call the returned function to stop.
  void Function() onExhausted(void Function(Budget, Rational) handler) {
    _listeners.add(handler);
    return () => _listeners.remove(handler);
  }

  @override
  String toString() => 'Budget($name, ${resource.name})';
}
